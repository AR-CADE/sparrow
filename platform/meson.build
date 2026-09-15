
rapidjson_inc  = include_directories('rapidjson/include')
text_input_inc  = include_directories('text_input')
platform_inc  = include_directories('.')
platform_flutter_inc = include_directories('include')

# Pigeon auto-generated sources (compiled separately to suppress unavoidable
# clang PGO hash mismatch on compiler-generated lambdas/destructors).
pigeon_sources = [
  'pigeon/compositor/cpp/messages.g.cpp',
  'pigeon/runner/cpp/messages.g.cpp',
]

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

libplatform_arguments = [
 '-MP' ,
 '-DWLR_USE_UNSTABLE',
  '-Wall',
   '-Wextra',
    '-Werror',
     '-Wno-unused-parameter',
      '-Wno-unused-variable',
       '-Wno-invalid-offsetof' ,
       '-Wno-unknown-pragmas',
       '-Wno-unused-template',
       '-Wno-deprecated-declarations'
  ]

# Pigeon auto-generated code: suppress -Wprofile-instr-out-of-date because
# clang's -fprofile-instr-generate alters the CFG hash of some C++ lambdas
# and destructor cleanup paths, causing unavoidable hash mismatches with
# -fprofile-instr-use. This is a clang toolchain limitation, not a source
# staleness issue. The warning remains active for all hand-written code.
pigeon_args = libplatform_arguments + ['-Wno-profile-instr-out-of-date', '-Wno-unused-function',
       '-Wno-unused-private-field']

pigeon_objs = static_library('pigeon_generated', pigeon_sources,
    dependencies: [platform_dependencies],
    include_directories: [rapidjson_inc, platform_inc, platform_flutter_inc, text_input_inc, sparrow_conf_inc, sparrow_api_inc],
    cpp_args: pigeon_args,
    install: false)

# First build a static library of all sources, so that it can be reused
# in tests
libplatform_sta = static_library('libplatform', platform_sources,
    dependencies: [platform_dependencies],
    include_directories: [rapidjson_inc, platform_inc, platform_flutter_inc, text_input_inc, sparrow_conf_inc, sparrow_api_inc],
    cpp_args: libplatform_arguments,
    link_with: pigeon_objs,
    install: false)

libplatform = declare_dependency(link_with: libplatform_sta,
    include_directories: [rapidjson_inc, platform_inc, platform_flutter_inc, text_input_inc, sparrow_conf_inc, sparrow_api_inc],
    dependencies: platform_dependencies)
