project(
  'os-2025-networkfs',
  'cpp',
  meson_version : '>= 1.3.0',
  default_options: [
    'cpp_std=gnu++20',
    'warning_level=3'
  ],
)

libfuse_dep = dependency('fuse3', required: true)

cpp_httplib_proj = subproject('cpp-httplib', default_options: {'compile': true})
cpp_httplib_dep = cpp_httplib_proj.get_variable('cpp_httplib_dep')

dependencies = [
  libfuse_dep,
  cpp_httplib_dep,
]

exe = executable(
  'networkfs',
  'src/main.cpp', 'src/inode.cpp', 'src/http.cpp',
  dependencies : dependencies,
)

gtest_dep = dependency('gtest')

test_dependencies = [
  gtest_dep,
  cpp_httplib_dep,
]

test_sources = [
  'tests/base.cpp',
  'tests/encoding.cpp',
  'tests/file.cpp',
  'tests/link.cpp',
  'tests/lib/nfs.cpp',
  'tests/lib/util.cpp',
  'tests/lib/main.cpp',
]

test_exe = executable(
  'networkfs-test',
  test_sources,
  dependencies : test_dependencies,
)

test('networkfs-test', test_exe, depends: exe, protocol: 'gtest', is_parallel: false, timeout: 180)
