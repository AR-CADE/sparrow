
sparrow_sources = [
  'core.cpp',
  'output.cpp',

  'renderer/renderer.cpp',
  'renderer/shaders.cpp',

  'input/seat.cpp',
  'input/touch.cpp',
  'input/keyboard.cpp',
  'input/pointer.cpp',
  
  'flutter/platform/engine.cpp',
  'flutter/platform/cursor.cpp',
  'flutter/platform/isolate.cpp',
  'flutter/platform/messages.cpp',
  'flutter/platform/task.cpp',
  'flutter/platform/text_input.cpp',

  'flutter/platform/engine/callbacks/seat_callback.cpp',
  'flutter/platform/engine/callbacks/surface_callback.cpp',

  'flutter/platform/engine/messages/seat_message.cpp',
  'flutter/platform/engine/messages/sub_surface_message.cpp',
  'flutter/platform/engine/messages/surface_message.cpp',
  'flutter/platform/engine/messages/decoration_message.cpp',
  'flutter/platform/engine/messages/popup_message.cpp',
  'flutter/platform/engine/messages/output_message.cpp',

  'surface/decoration.cpp',
  'surface/popup.cpp',
  'surface/sub_surface.cpp',
  'surface/surface.cpp',
  'surface/view.cpp',

  'util/dispatcher.cpp',
  'util/handle_map.cpp',
  'util/realtime.cpp',
  'util/udmabuf.cpp',
  'util/rethreading/callable_queue.cpp',
]

sparrow_dependencies = [
  wayland_server,
  wlroots,
  xkbcommon,
  libinput,
  pixman,
  drm,
  egl,
  glesv2,
  sparrow_protos,
  backtrace,
  xcb,
  embedder,
  libplatform,
]

sparrow_inc = [
  sparrow_conf_inc,
  sparrow_api_inc,
  platform_inc,
  rapidjson_inc,
  text_input_inc,
]

libsparrow_sta = static_library('libsparrow', sparrow_sources,
    dependencies: [sparrow_dependencies],
    include_directories: sparrow_inc,
    install: false)

libsparrow = declare_dependency(link_whole: libsparrow_sta,
    include_directories: sparrow_inc,
    dependencies: sparrow_dependencies)

# Generate main executable
executable('sparrow', ['main.cpp'],
    dependencies: [libsparrow],
    build_rpath: '$ORIGIN/shell/lib:$ORIGIN/../subprojects/wlroots',
    install_rpath: '$ORIGIN/shell/lib:$ORIGIN/../lib',
    install: true)


