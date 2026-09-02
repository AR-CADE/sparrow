
rapidjson_inc  = include_directories('rapidjson/include')
text_input_inc  = include_directories('text_input')
platform_inc  = include_directories('.')

platform_sources = [
  'client_wrapper/binary_messenger.cpp',
  'client_wrapper/incoming_message_dispatcher.cpp',
  'client_wrapper/standard_codec.cc',
  'client_wrapper/json_message_codec.cc',
  'client_wrapper/json_method_codec.cc',
  'client_wrapper/engine_method_result.cpp',

  'text_input/text_input_model.cc',
]

platform_dependencies = [wayland_server, wlroots, xkbcommon, libinput,
                       pixman, drm, egl, glesv2, 
                       backtrace, xcb,  embedder]

debug_arguments = [
 '-MP' ,
 '-DWLR_USE_UNSTABLE',
  '-Wall',
   '-Wextra',
    '-Werror',
     '-Wno-unused-parameter',
      '-Wno-unused-variable',
       '-Wno-invalid-offsetof' ,
       '-Wno-unknown-pragmas',
       '-Wno-deprecated-declarations'
  ]

# First build a static library of all sources, so that it can be reused
# in tests
libplatform_sta = static_library('libplatform', platform_sources,
    dependencies: [platform_dependencies],
    include_directories: [rapidjson_inc, platform_inc, text_input_inc, sparrow_conf_inc, sparrow_api_inc],
    cpp_args: debug_arguments,
    install: false)

libplatform = declare_dependency(link_with: libplatform_sta,
    include_directories: [rapidjson_inc, platform_inc, text_input_inc, sparrow_conf_inc, sparrow_api_inc],
    dependencies: platform_dependencies)
