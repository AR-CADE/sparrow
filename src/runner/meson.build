runner_sources = [
  'main.cpp',
  'wayland_window.cpp',
  'vulkan_window.cpp',
  'input_manager.cpp',
  'flutter_runner.cpp',
  'ipc_client.cpp',
  '../flutter/platform/key_mapping.cpp',
]

vulkan_dep = dependency('vulkan', required: false)

runner_dependencies = [
  wayland_client,
  wayland_egl,
  wayland_cursor,
  sparrow_client_protos,
  xkbcommon,
  egl,
  glesv2,
  vulkan_dep,
  embedder,
  libplatform,
  simdjson_dep,
]

runner_inc = [
  include_directories('.'),
  sparrow_conf_inc,
  platform_inc,
  simdjson_inc,
  text_input_inc,
]

runner_cpp_args = []
runner_link_args = []
if get_option('pgo') or pgo_backend.startswith('csir')
  runner_cpp_args += [
    '-fno-profile-instr-use',
    '-fno-profile-use',
    '-fno-profile-instr-generate',
    '-fno-profile-generate',
    '-Wno-profile-instr-out-of-date',
  ]
  runner_link_args += [
    '-fno-profile-instr-use',
    '-fno-profile-use',
    '-fno-profile-instr-generate',
    '-fno-profile-generate',
  ]
endif

executable('sparrow-app-runner',
  runner_sources,
  dependencies: runner_dependencies,
  include_directories: runner_inc,
  cpp_args: runner_cpp_args,
  link_args: runner_link_args,
  build_rpath: '$ORIGIN/../shell/lib:$ORIGIN/shell/lib:$ORIGIN/../../out/shell/lib',
  install_rpath: '$ORIGIN/../lib:$ORIGIN/shell/lib',
  install: true,
)
