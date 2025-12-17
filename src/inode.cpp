#include "inode.h"

#define FUSE_USE_VERSION 317
#define TOKEN "c4ca2959-6964-4bce-9944-dda4ae2aa7ab"

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
  unsigned char entry_type;
  fuse_ino_t ino;
};

struct entry {
  unsigned char entry_type; 
  ino_t ino;
  char name[256];
};

struct entries {
  size_t entries_count;
  struct entry entries[16];
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
  char response[1024] = {};
  char ino_str[21];
  ino_to_string(ino_str, parent);
  std::vector<std::pair<std::string, std::string>> args;
  args.emplace_back("parent", ino_str);
  args.emplace_back("name", name);
  if (networkfs_http_call(TOKEN, "lookup", response, sizeof(response), args)) {
    fuse_reply_err(req, ENOENT);
  } else{
    struct entry_info entry;
    memcpy(&entry, &response, sizeof(entry_info));
    struct fuse_entry_param e;
    e.ino = entry.ino;
    e.attr_timeout = 0;
    e.entry_timeout = 0;
    e.attr.st_mode = entry.entry_type == 4 ? S_IFDIR : S_IFREG;
    e.attr.st_size = 0;
    fuse_reply_entry(req, &e);
  }
}

void networkfs_getattr(fuse_req_t req, fuse_ino_t ino,
                       struct fuse_file_info* fi) {
  (void)fi;
  
  char response[1024] = {};
  char ino_str[21];
  ino_to_string(ino_str, ino);
  
  std::vector<std::pair<std::string, std::string>> args;
  args.emplace_back("inode", ino_str);
  
  // Try to list the inode as a directory to determine its type
  int64_t result = networkfs_http_call(TOKEN, "list", response, sizeof(response), args);
  
  struct stat stbuf = {};
  stbuf.st_ino = ino;
  stbuf.st_nlink = 1;
  
  if (result == NFS_SUCCESS) {
    // It's a directory
    stbuf.st_mode = S_IFDIR | 0755;
    stbuf.st_size = 0;
    fuse_reply_attr(req, &stbuf, 1.0);
  } else if (result == NFS_ENOTDIR) {
    // It's a file (not a directory)
    stbuf.st_mode = S_IFREG | 0644;
    stbuf.st_size = 0;  // Size will be updated when file is opened/read
    fuse_reply_attr(req, &stbuf, 1.0);
  } else {
    fuse_reply_err(req, ENOENT);
  }
}

void networkfs_iterate(fuse_req_t req, fuse_ino_t i_ino, size_t size, off_t off,
                       struct fuse_file_info* fi) {
  (void)fi;
  
  char ino_str[21];
  ino_to_string(ino_str, i_ino);
  
  char response[sizeof(struct entries)] = {};
  std::vector<std::pair<std::string, std::string>> args;
  args.emplace_back("inode", ino_str);
  if (networkfs_http_call(TOKEN, "list", response, sizeof(response), args)) {
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
      stbuf.st_mode = (e->entry_type == DT_DIR) ? S_IFDIR : S_IFREG;
      stbuf.st_nlink = 1;
      stbuf.st_size = 0;
      
      size_t entry_size = fuse_add_direntry(req, nullptr, 0, e->name, &stbuf, i + 1);
      
      buf_pos += fuse_add_direntry(req, buf + buf_pos, size - buf_pos, e->name, &stbuf, i + 1);
    }
    
    fuse_reply_buf(req, buf, buf_pos);
    free(buf);
  }
}

void networkfs_create(fuse_req_t req, fuse_ino_t parent, const char* name,
                      mode_t mode, struct fuse_file_info* fi) {
  (void)parent;
  (void)name;
  (void)mode;
  (void)fi;
  // TODO: Implement create
  fuse_reply_err(req, ENOSYS);
}

void networkfs_unlink(fuse_req_t req, fuse_ino_t parent, const char* name) {
  (void)parent;
  (void)name;
  // TODO: Implement unlink
  fuse_reply_err(req, ENOSYS);
}

void networkfs_mkdir(fuse_req_t req, fuse_ino_t parent, const char* name,
                     mode_t mode) {
  (void)parent;
  (void)name;
  (void)mode;
  // TODO: Implement mkdir
  fuse_reply_err(req, ENOSYS);
}

void networkfs_rmdir(fuse_req_t req, fuse_ino_t parent, const char* name) {
  (void)parent;
  (void)name;
  // TODO: Implement rmdir
  fuse_reply_err(req, ENOSYS);
}

void networkfs_open(fuse_req_t req, fuse_ino_t i_ino, fuse_file_info* fi) {
  (void)i_ino;
  (void)fi;
  // TODO: Implement open
  fuse_reply_err(req, ENOSYS);
}

void networkfs_release(fuse_req_t req, fuse_ino_t ino,
                       struct fuse_file_info* fi) {
  (void)ino;
  (void)fi;
  // TODO: Implement release
  fuse_reply_err(req, 0);
}

void networkfs_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                    struct fuse_file_info* fi) {
  (void)ino;
  (void)size;
  (void)off;
  (void)fi;
  // TODO: Implement read
  fuse_reply_err(req, ENOSYS);
}

void networkfs_write(fuse_req_t req, fuse_ino_t ino, const char* buffer,
                     size_t size, off_t off, struct fuse_file_info* fi) {
  (void)ino;
  (void)buffer;
  (void)size;
  (void)off;
  (void)fi;
  // TODO: Implement write
  fuse_reply_err(req, ENOSYS);
}

void networkfs_flush(fuse_req_t req, fuse_ino_t ino,
                     struct fuse_file_info* fi) {
  (void)ino;
  (void)fi;
  // TODO: Implement flush
  fuse_reply_err(req, 0);
}

void networkfs_fsync(fuse_req_t req, fuse_ino_t ino, int datasync,
                     struct fuse_file_info* fi) {
  (void)ino;
  (void)datasync;
  (void)fi;
  // TODO: Implement fsync
  fuse_reply_err(req, 0);
}

void networkfs_setattr(fuse_req_t req, fuse_ino_t ino, struct stat* attr,
  int to_set, struct fuse_file_info* fi) {
(void)ino;
(void)attr;
(void)to_set;
(void)fi;
// TODO: Implement setattr
fuse_reply_err(req, ENOSYS);
}


void networkfs_link(fuse_req_t req, fuse_ino_t ino, fuse_ino_t newparent,
                    const char* name) {
  (void)ino;
  (void)newparent;
  (void)name;
  // TODO: Implement link
  fuse_reply_err(req, ENOSYS);
}

void networkfs_forget(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup) {
  (void)ino;
  (void)nlookup;
  fuse_reply_none(req);
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
    .readdir = networkfs_iterate,
    .create = networkfs_create,
};