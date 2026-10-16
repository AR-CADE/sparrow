project('perfetto', 'cpp',
  version: '46.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=0',
    'werror=false',
  ]
)

perfetto_inc = include_directories('.')

perfetto_lib = static_library('perfetto',
  'perfetto.cc',
  include_directories: perfetto_inc,
  cpp_args: ['-Wno-deprecated-declarations', '-Wno-unused-parameter', '-Wno-attributes'],
  install: false,
)

perfetto_dep = declare_dependency(
  include_directories: perfetto_inc,
  link_with: perfetto_lib,
)
