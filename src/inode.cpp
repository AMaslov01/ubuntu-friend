#include "inode.h"

#define FUSE_USE_VERSION 317

#include <dirent.h>
#include <fuse_lowlevel.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "http.h"
#include "util.h"

struct entry_info {
  uint64_t entry_type;
  uint64_t ino;
};

struct entry {
  uint64_t entry_type; 
  uint64_t ino;
  char name[256];
};

struct entries {
  uint64_t entries_count;
  struct entry entries[16];
};

struct file_buffer {
  char* data;
  size_t size;
};

void networkfs_init(void* userdata, struct fuse_conn_info* conn) {
  (void)userdata;
  conn->want |= FUSE_CAP_EXPORT_SUPPORT;
  fprintf(stdout, "capable = %u\n", conn->capable);
}

void networkfs_destroy(void* private_data) {
  // Token string, which was allocated in main.
  free(private_data);
}

void networkfs_lookup(fuse_req_t req, fuse_ino_t parent, const char* name) {
  const char* token = (const char*)fuse_req_userdata(req);
  char response[1024] = {};
  char ino_str[21];
  ino_to_string(ino_str, parent);
  std::vector<std::pair<std::string, std::string>> args;
  args.emplace_back("parent", ino_str);
  args.emplace_back("name", name);
  int64_t result = networkfs_http_call(token, "lookup", response, sizeof(response), args);
  if (result != NFS_SUCCESS) {
    fuse_reply_err(req, ENOENT);
  } else {
    struct entry_info entry;
    memcpy(&entry, response, sizeof(entry_info));
    struct fuse_entry_param e;
    memset(&e, 0, sizeof(e));
    e.ino = entry.ino;
    e.attr_timeout = 0;
    e.entry_timeout = 0;
    e.attr.st_ino = entry.ino;
    e.attr.st_mode = entry.entry_type == DT_DIR ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    e.attr.st_nlink = entry.entry_type == DT_DIR ? 2 : 1;
    e.attr.st_size = 0;
    fuse_reply_entry(req, &e);
  }
}

void networkfs_getattr(fuse_req_t req, fuse_ino_t ino,
                       struct fuse_file_info* fi) {
  const char* token = (const char*)fuse_req_userdata(req);
  
  char response[sizeof(struct entries)] = {};  // Need space for list response
  char ino_str[21];
  ino_to_string(ino_str, ino);
  
  std::vector<std::pair<std::string, std::string>> args;
  args.emplace_back("inode", ino_str);
  
  struct stat stbuf = {};
  stbuf.st_ino = ino;
  
  // First, check if we have an open file handle with size info
  if (fi != nullptr && fi->fh != 0) {
    struct file_buffer* fb = (struct file_buffer*)fi->fh;
    stbuf.st_mode = S_IFREG | 0644;
    stbuf.st_nlink = 1;
    stbuf.st_size = fb->size;
    fuse_reply_attr(req, &stbuf, 1.0);
    return;
  }
  
  // Try to list the inode as a directory to determine its type
  int64_t result = networkfs_http_call(token, "list", response, sizeof(response), args);
  
  
  if (result == NFS_SUCCESS) {
    // It's a directory
    stbuf.st_mode = S_IFDIR | 0755;
    stbuf.st_nlink = 2;
    stbuf.st_size = 0;
    fuse_reply_attr(req, &stbuf, 1.0);
  } else if (result == NFS_ENOTDIR) {
    // It's a file (not a directory)
    stbuf.st_mode = S_IFREG | 0644;
    stbuf.st_nlink = 1;
    stbuf.st_size = 0;  // Size will be updated when file is opened/read
    fuse_reply_attr(req, &stbuf, 1.0);
  } else {
    fuse_reply_err(req, ENOENT);
  }
}

void networkfs_iterate(fuse_req_t req, fuse_ino_t i_ino, size_t size, off_t off,
                       struct fuse_file_info* fi) {
  (void)fi;
  const char* token = (const char*)fuse_req_userdata(req);
  
  char ino_str[21];
  ino_to_string(ino_str, i_ino);
  
  char response[sizeof(struct entries)] = {};
  std::vector<std::pair<std::string, std::string>> args;
  args.emplace_back("inode", ino_str);
  int64_t result = networkfs_http_call(token, "list", response, sizeof(response), args);
  if (result != NFS_SUCCESS) {
    fuse_reply_err(req, ENOENT);
  } else {
    struct entries dir_entries;
    memcpy(&dir_entries, response, sizeof(struct entries));
    
    char* buf = static_cast<char*>(malloc(size));
    size_t buf_pos = 0;
    
    for (size_t i = off; i < dir_entries.entries_count; i++) {
      struct entry* e = &dir_entries.entries[i];
      
      struct stat stbuf = {};
      stbuf.st_ino = e->ino;
      stbuf.st_mode = (e->entry_type == DT_DIR) ? (S_IFDIR | 0755) : (S_IFREG | 0644);
      stbuf.st_nlink = (e->entry_type == DT_DIR) ? 2 : 1;
      stbuf.st_size = 0;
      
      size_t entry_size = fuse_add_direntry(req, nullptr, 0, e->name, &stbuf, i + 1);
      if (buf_pos + entry_size > size) {
        break;
      }
      
      buf_pos += fuse_add_direntry(req, buf + buf_pos, size - buf_pos, e->name, &stbuf, i + 1);
    }
    
    fuse_reply_buf(req, buf, buf_pos);
    free(buf);
  }
}

void networkfs_create(fuse_req_t req, fuse_ino_t parent, const char* name,
                      mode_t mode, struct fuse_file_info* fi) {
  (void)mode;
  const char* token = (const char*)fuse_req_userdata(req);
  char response[1024] = {};
  char parent_str[21];
  ino_to_string(parent_str, parent);
  
  std::vector<std::pair<std::string, std::string>> args;
  args.emplace_back("parent", parent_str);
  args.emplace_back("name", name);
  args.emplace_back("type", "file");
  
  int64_t result = networkfs_http_call(token, "create", response, sizeof(response), args);
  if (result != NFS_SUCCESS) {
    // Map error codes: 5=EEXIST, 7=ENOSPC (too many entries)
    int err = (result == 5) ? EEXIST : (result == 7) ? ENOSPC : EIO;
    fuse_reply_err(req, err);
  } else {
    // Response structure: [ino: 8 bytes] (status already stripped by http_call)
    uint64_t ino;
    memcpy(&ino, response, sizeof(uint64_t));
    
    // Allocate file buffer for new empty file
    struct file_buffer* fb = new (std::nothrow) file_buffer;
    if (fb == nullptr) {
      fuse_reply_err(req, ENOMEM);
      return;
    }
    fb->data = nullptr;
    fb->size = 0;
    fi->fh = (uint64_t)fb;
    
    struct fuse_entry_param e;
    memset(&e, 0, sizeof(e));
    e.ino = ino;
    e.attr_timeout = 0;
    e.entry_timeout = 0;
    e.attr.st_ino = ino;
    e.attr.st_mode = S_IFREG | 0644;
    e.attr.st_nlink = 1;
    e.attr.st_size = 0;
    fuse_reply_create(req, &e, fi);
  }
}

void networkfs_unlink(fuse_req_t req, fuse_ino_t parent, const char* name) {
  const char* token = (const char*)fuse_req_userdata(req);
  char response[1024] = {};
  char parent_str[21];
  ino_to_string(parent_str, parent);
  
  std::vector<std::pair<std::string, std::string>> args;
  args.emplace_back("parent", parent_str);
  args.emplace_back("name", name);
  
  int64_t result = networkfs_http_call(token, "unlink", response, sizeof(response), args);
  if (result != NFS_SUCCESS) {
    // Map error codes: 4=ENOENT (not found), 2=EISDIR (is a directory)
    int err = (result == 4) ? ENOENT : (result == 2) ? EISDIR : EIO;
    fuse_reply_err(req, err);
  } else {
    fuse_reply_err(req, 0);
  }
}

void networkfs_mkdir(fuse_req_t req, fuse_ino_t parent, const char* name,
                     mode_t mode) {
  (void)mode;
  const char* token = (const char*)fuse_req_userdata(req);
  char response[1024] = {};
  char parent_str[21];
  ino_to_string(parent_str, parent);
  
  std::vector<std::pair<std::string, std::string>> args;
  args.emplace_back("parent", parent_str);
  args.emplace_back("name", name);
  args.emplace_back("type", "directory");
  
  int64_t result = networkfs_http_call(token, "create", response, sizeof(response), args);
  if (result != NFS_SUCCESS) {
    // Map error codes: 5=EEXIST, 7=ENOSPC (too many entries)
    int err = (result == 5) ? EEXIST : (result == 7) ? ENOSPC : EIO;
    fuse_reply_err(req, err);
  } else {
    // Response structure: [ino: 8 bytes] (status already stripped by http_call)
    uint64_t ino;
    memcpy(&ino, response, sizeof(uint64_t));
    
    struct fuse_entry_param e;
    memset(&e, 0, sizeof(e));
    e.ino = ino;
    e.attr_timeout = 0;
    e.entry_timeout = 0;
    e.attr.st_ino = ino;
    e.attr.st_mode = S_IFDIR | 0755;
    e.attr.st_nlink = 2;
    e.attr.st_size = 0;
    fuse_reply_entry(req, &e);
  }
}

void networkfs_rmdir(fuse_req_t req, fuse_ino_t parent, const char* name) {
  const char* token = (const char*)fuse_req_userdata(req);
  char response[1024] = {};
  char parent_str[21];
  ino_to_string(parent_str, parent);
  
  std::vector<std::pair<std::string, std::string>> args;
  args.emplace_back("parent", parent_str);
  args.emplace_back("name", name);
  
  int64_t result = networkfs_http_call(token, "rmdir", response, sizeof(response), args);
  if (result != NFS_SUCCESS) {
    // Map error codes: 4=ENOENT (not found), 8=ENOTEMPTY (not empty)
    int err = (result == 4) ? ENOENT : (result == 8) ? ENOTEMPTY : EIO;
    fuse_reply_err(req, err);
  } else {
    fuse_reply_err(req, 0);
  }
}

void networkfs_open(fuse_req_t req, fuse_ino_t i_ino, fuse_file_info* fi) {
  const char* token = (const char*)fuse_req_userdata(req);
  
  char ino_str[21];
  ino_to_string(ino_str, i_ino);
  
  // Allocate file buffer
  struct file_buffer* fb = new (std::nothrow) file_buffer;
  if (fb == nullptr) {
    fuse_reply_err(req, ENOMEM);
    return;
  }
  
  // Check if O_TRUNC flag is set - if so, start with empty file
  if (fi->flags & O_TRUNC) {
    // Truncate: start with empty buffer
    fb->data = nullptr;
    fb->size = 0;
  } else {
    // Read file content from server
    char response[1024] = {};
    std::vector<std::pair<std::string, std::string>> args;
    args.emplace_back("inode", ino_str);
    
    int64_t result = networkfs_http_call(token, "read", response, sizeof(response), args);
    
    if (result == NFS_SUCCESS) {
      // Parse response: [content_length: 8 bytes][content: up to 512 bytes] (status already stripped)
      uint64_t size = *((uint64_t*)(response));
      
      fb->size = size;
      if (size > 0) {
        fb->data = (char*)malloc(size);
        if (fb->data == nullptr) {
          delete fb;
          fuse_reply_err(req, ENOMEM);
          return;
        }
        // Copy content (skip content_length = 8 bytes)
        memcpy(fb->data, response + sizeof(uint64_t), size);
      } else {
        fb->data = nullptr;
      }
    } else {
      // File might be newly created or empty - start with empty buffer
      fb->data = nullptr;
      fb->size = 0;
    }
  }
  
  fi->fh = (uint64_t)fb;
  fuse_reply_open(req, fi);
}

void networkfs_release(fuse_req_t req, fuse_ino_t ino,
                       struct fuse_file_info* fi) {
  (void)ino;
  struct file_buffer* fb = (struct file_buffer*)fi->fh;
  if (fb != nullptr) {
    if (fb->data != nullptr) {
      free(fb->data);
    }
    delete fb;
  }
  fuse_reply_err(req, 0);
}

void networkfs_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                    struct fuse_file_info* fi) {
  (void)ino;
  struct file_buffer* fb = (struct file_buffer*)fi->fh;
  
  if (fb == nullptr) {
    fuse_reply_err(req, EIO);
    return;
  }
  
  size_t bytes_to_read = size;
  if ((size_t)off >= fb->size) {
    fuse_reply_buf(req, nullptr, 0);
    return;
  }
  
  if ((size_t)off + size > fb->size) {
    bytes_to_read = fb->size - off;
  }
  
  fuse_reply_buf(req, fb->data + off, bytes_to_read);
}

void networkfs_write(fuse_req_t req, fuse_ino_t ino, const char* buffer,
                     size_t size, off_t off, struct fuse_file_info* fi) {
  (void)ino;
  struct file_buffer* fb = (struct file_buffer*)fi->fh;
  
  if (fb == nullptr) {
    fuse_reply_err(req, EIO);
    return;
  }
  
  size_t new_size = off + size;
  
  // Expand buffer if needed
  if (new_size > fb->size) {
    char* new_data = (char*)realloc(fb->data, new_size);
    if (new_data == nullptr) {
      fuse_reply_err(req, ENOMEM);
      return;
    }
    // Zero out the gap if writing beyond current size
    if ((size_t)off > fb->size) {
      memset(new_data + fb->size, 0, off - fb->size);
    }
    fb->data = new_data;
    fb->size = new_size;
  }
  
  memcpy(fb->data + off, buffer, size);
  fuse_reply_write(req, size);
}

void networkfs_flush(fuse_req_t req, fuse_ino_t ino,
                     struct fuse_file_info* fi) {
  const char* token = (const char*)fuse_req_userdata(req);
  struct file_buffer* fb = (struct file_buffer*)fi->fh;
  
  if (fb == nullptr) {
    fuse_reply_err(req, 0);
    return;
  }
  
  char ino_str[21];
  ino_to_string(ino_str, ino);
  
  // Prepare content for write
  std::string content;
  if (fb->data != nullptr && fb->size > 0) {
    content = std::string(fb->data, fb->size);
  }
  
  std::vector<std::pair<std::string, std::string>> args;
  args.emplace_back("inode", ino_str);
  args.emplace_back("content", content);
  
  char response[1024] = {};
  int64_t result = networkfs_http_call(token, "write", response, sizeof(response), args);
  
  if (result != NFS_SUCCESS) {
    fuse_reply_err(req, EIO);
  } else {
    fuse_reply_err(req, 0);
  }
}

void networkfs_fsync(fuse_req_t req, fuse_ino_t ino, int datasync,
                     struct fuse_file_info* fi) {
  (void)datasync;
  const char* token = (const char*)fuse_req_userdata(req);
  struct file_buffer* fb = (struct file_buffer*)fi->fh;
  
  if (fb == nullptr) {
    fuse_reply_err(req, 0);
    return;
  }
  
  char ino_str[21];
  ino_to_string(ino_str, ino);
  
  // Prepare content for write
  std::string content;
  if (fb->data != nullptr && fb->size > 0) {
    content = std::string(fb->data, fb->size);
  }
  
  std::vector<std::pair<std::string, std::string>> args;
  args.emplace_back("inode", ino_str);
  args.emplace_back("content", content);
  
  char response[1024] = {};
  int64_t result = networkfs_http_call(token, "write", response, sizeof(response), args);
  
  if (result != NFS_SUCCESS) {
    fuse_reply_err(req, EIO);
  } else {
    fuse_reply_err(req, 0);
  }
}

void networkfs_setattr(fuse_req_t req, fuse_ino_t ino, struct stat* attr,
  int to_set, struct fuse_file_info* fi) {
  const char* token = (const char*)fuse_req_userdata(req);
  
  if (to_set & FUSE_SET_ATTR_SIZE) {
    // Handle truncate
    if (fi != nullptr && fi->fh != 0) {
      // File is open, truncate the buffer
      struct file_buffer* fb = (struct file_buffer*)fi->fh;
      
      size_t new_size = attr->st_size;
      if (new_size != fb->size) {
        char* new_data = (char*)realloc(fb->data, new_size);
        if (new_data == nullptr && new_size > 0) {
          fuse_reply_err(req, ENOMEM);
          return;
        }
        
        // Zero out new space if expanding
        if (new_size > fb->size) {
          memset(new_data + fb->size, 0, new_size - fb->size);
        }
        
        fb->data = new_data;
        fb->size = new_size;
      }
    } else {
      // File is not open yet, need to truncate on server
      // For now, we'll handle truncate to 0 (most common case)
      if (attr->st_size == 0) {
        char ino_str[21];
        ino_to_string(ino_str, ino);
        
        std::vector<std::pair<std::string, std::string>> args;
        args.emplace_back("inode", ino_str);
        args.emplace_back("content", "");
        
        char response[1024] = {};
        int64_t result = networkfs_http_call(token, "write", response, sizeof(response), args);
        
        if (result != NFS_SUCCESS) {
          fuse_reply_err(req, EIO);
          return;
        }
      }
      // For non-zero truncate without open file, we'd need to read, truncate, write
      // For now, just proceed - the size will be handled when file is actually accessed
    }
    
    // Return updated attributes
    struct stat stbuf = {};
    stbuf.st_ino = ino;
    stbuf.st_mode = S_IFREG | 0644;
    stbuf.st_nlink = 1;
    stbuf.st_size = attr->st_size;
    fuse_reply_attr(req, &stbuf, 1.0);
  } else {
    // For other attributes, just return current state
    struct stat stbuf = {};
    stbuf.st_ino = ino;
    stbuf.st_mode = S_IFREG | 0644;
    stbuf.st_nlink = 1;
    stbuf.st_size = 0;
    fuse_reply_attr(req, &stbuf, 1.0);
  }
}


void networkfs_link(fuse_req_t req, fuse_ino_t ino, fuse_ino_t newparent,
                    const char* name) {
  const char* token = (const char*)fuse_req_userdata(req);
  char response[1024] = {};
  char ino_str[21];
  char newparent_str[21];
  ino_to_string(ino_str, ino);
  ino_to_string(newparent_str, newparent);
  
  std::vector<std::pair<std::string, std::string>> args;
  args.emplace_back("source", ino_str);
  args.emplace_back("parent", newparent_str);
  args.emplace_back("name", name);
  
  int64_t result = networkfs_http_call(token, "link", response, sizeof(response), args);
  if (result != NFS_SUCCESS) {
    fuse_reply_err(req, EEXIST);
  } else {
    struct fuse_entry_param e;
    memset(&e, 0, sizeof(e));
    e.ino = ino;
    e.attr_timeout = 0;
    e.entry_timeout = 0;
    e.attr.st_ino = ino;
    e.attr.st_mode = S_IFREG | 0644;
    e.attr.st_nlink = 2;  // At least 2 links now
    e.attr.st_size = 0;
    fuse_reply_entry(req, &e);
  }
}

void networkfs_forget(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup) {
  (void)ino;
  (void)nlookup;
  fuse_reply_none(req);
}

void networkfs_access(fuse_req_t req, fuse_ino_t ino, int mask) {
  (void)ino;
  (void)mask;
  // Always allow access - we're not implementing permission checking
  fuse_reply_err(req, 0);
}

void networkfs_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi) {
  (void)ino;
  (void)fi;
  // Just return success - no special handling needed for opening directories
  fuse_reply_open(req, fi);
}

void networkfs_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi) {
  (void)ino;
  (void)fi;
  // No cleanup needed for directories
  fuse_reply_err(req, 0);
}

const struct fuse_lowlevel_ops networkfs_oper = {
    .init = networkfs_init,
    .destroy = networkfs_destroy,
    .lookup = networkfs_lookup,
    .forget = networkfs_forget,
    .getattr = networkfs_getattr,
    .setattr = networkfs_setattr,
    .mkdir = networkfs_mkdir,
    .unlink = networkfs_unlink,
    .rmdir = networkfs_rmdir,
    .link = networkfs_link,
    .open = networkfs_open,
    .read = networkfs_read,
    .write = networkfs_write,
    .flush = networkfs_flush,
    .release = networkfs_release,
    .fsync = networkfs_fsync,
    .opendir = networkfs_opendir,
    .readdir = networkfs_iterate,
    .releasedir = networkfs_releasedir,
    .access = networkfs_access,
    .create = networkfs_create,
};