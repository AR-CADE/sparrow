runner_sources = [
  'main.cpp',
  'wayland_window.cpp',
  'vulkan_window.cpp',
  'input_manager.cpp',
  'flutter_runner.cpp',
  'ipc_client.cpp',
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
]

runner_inc = [
  include_directories('.'),
  sparrow_conf_inc,
  platform_inc,
  rapidjson_inc,
  text_input_inc,
]

runner_cpp_args = []
if get_option('pgo')
  runner_cpp_args += ['-fno-profile-instr-use', '-Wno-profile-instr-out-of-date']
endif

executable('sparrow-app-runner',
  runner_sources,
  dependencies: runner_dependencies,
  include_directories: runner_inc,
  cpp_args: runner_cpp_args,
  build_rpath: '$ORIGIN/../shell/lib:$ORIGIN/shell/lib:$ORIGIN/../../out/shell/lib',
  install_rpath: '$ORIGIN/../lib:$ORIGIN/shell/lib',
  install: true,
)
