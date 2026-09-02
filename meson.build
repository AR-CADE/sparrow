project(
	'sparrow',
	'c',
	'cpp',
	version: '0.2.0',
	license: 'MIT',
	meson_version: '>=0.64.0',
	default_options: [
		'cpp_std=c++26',
		'c_std=c23',
		'warning_level=2',
		'werror=false',
	],
)

cpp = meson.get_compiler('cpp')

version = '"@0@"'.format(meson.project_version())
add_project_arguments('-DSPARROW_VERSION=@0@'.format(version), language: 'cpp')

is_asan = get_option('asan') or get_option('b_sanitize').contains('address')
is_ubsan = get_option('ubsan') or get_option('b_sanitize').contains('undefined')
is_tsan = get_option('tsan') or get_option('b_sanitize').contains('thread')
is_valgrind = get_option('valgrind') or get_option('b_sanitize').contains('memory')
is_debug = get_option('disable_optimizations') or get_option('buildtype') == 'debug' or get_option('buildtype') == 'debugoptimized' or is_asan or is_tsan or is_valgrind

if is_valgrind
  add_project_arguments(['-DHAS_VALGRIND'], language: ['cpp', 'c'])
endif

if is_asan
  add_project_arguments(['-fsanitize=address', '-fno-omit-frame-pointer', '-fno-optimize-sibling-calls', '-g3', '-DHAS_ASAN'], language: ['cpp', 'c'])
  add_project_link_arguments(['-fsanitize=address'], language: ['cpp', 'c'])
endif

if is_ubsan
  add_project_arguments(['-fsanitize=undefined'], language: ['cpp', 'c'])
  add_project_link_arguments(['-fsanitize=undefined'], language: ['cpp', 'c'])
endif

if is_tsan
  add_project_arguments(['-fsanitize=thread', '-fno-omit-frame-pointer', '-fno-optimize-sibling-calls', '-g3', '-DHAS_TSAN'], language: ['cpp', 'c'])
  add_project_link_arguments(['-fsanitize=thread'], language: ['cpp', 'c'])
endif

if is_debug
  add_project_arguments(['-O0', '-g3', '-fno-omit-frame-pointer', '-DSPARROW_DEBUG_BUILD'], language: ['cpp', 'c'])
  add_project_link_arguments(['-Wl,--disable-new-dtags', '-Wl,-rpath,$ORIGIN/shell/lib'], language: ['cpp', 'c'])
  add_project_arguments(['-Wall', '-Wextra', '-Wpedantic', '-Wno-gnu-zero-variadic-macro-arguments'], language: ['cpp', 'c'])
else
  add_project_arguments(['-O3'], language: ['cpp', 'c'])
  add_project_arguments(['-march=native'], language: ['cpp', 'c'])
  add_project_arguments(['-mtune=native'], language: ['cpp', 'c'])
  add_project_arguments(['-fomit-frame-pointer'], language: ['cpp', 'c'])
  add_project_arguments(['-funroll-loops'], language: ['cpp', 'c'])
  add_project_arguments(['-fno-common'], language: ['cpp', 'c'])
  add_project_arguments(['-ftree-vectorize'], language: ['cpp', 'c'])
  add_project_arguments(['-ffunction-sections', '-fdata-sections'], language: ['cpp', 'c'])
  add_project_link_arguments(['-Wl,--gc-sections', '-Wl,--disable-new-dtags', '-Wl,-rpath,$ORIGIN/shell/lib'], language: ['cpp', 'c'])
  add_project_arguments(['-Wno-missing-designated-field-initializers'], language: ['cpp', 'c'])

  if get_option('lto') and not is_asan and not is_tsan
    add_project_arguments(['-flto'], language: ['cpp', 'c'])
    add_project_link_arguments(['-flto'], language: ['cpp', 'c'])
    add_project_link_arguments(['-Wl,--as-needed'], language: ['cpp', 'c'])
    add_project_link_arguments(['-Wl,-O2'], language: ['cpp', 'c'])
    add_project_link_arguments(['-Wl,--strip-all'], language: ['cpp', 'c'])
  endif

  if get_option('profiling')
    add_project_arguments(['-fprofile-instr-generate'], language: ['cpp', 'c'])
    add_project_link_arguments(['-fprofile-instr-generate'], language: ['cpp', 'c'])
  elif get_option('pgo')
    fs = import('fs')
    profdata_path = meson.project_source_root() / 'sparrow.profdata'
    if fs.exists(profdata_path)
      add_project_arguments(['-fprofile-instr-use=' + profdata_path, '-Wno-profile-instr-unprofiled', '-Wno-profile-instr-out-of-date'], language: ['cpp', 'c'])
      add_project_link_arguments(['-fprofile-instr-use=' + profdata_path], language: ['cpp', 'c'])
    endif
  endif
endif

add_project_link_arguments(['-pthread'], language: ['cpp', 'c'])

sparrow_api_inc  = include_directories('src/api')

wayland_server = dependency('wayland-server')
wayland_client = dependency('wayland-client')
wayland_protos = dependency('wayland-protocols', version: '>=1.37')
drm            = dependency('libdrm')
egl            = dependency('egl')
glesv2         = dependency('glesv2')
libinput       = dependency('libinput', version: '>=1.7.0')
pixman         = dependency('pixman-1')
xkbcommon      = dependency('xkbcommon')
embedder       = subproject('flutter_embedder').get_variable('embedder_dep')

add_project_arguments('-DFLUTTER_COMPOSITOR', language : ['c', 'cpp'])

add_project_arguments('-DWLR_USE_UNSTABLE', language : ['c', 'cpp'])

if get_option('enable_dmabuf')
  add_project_arguments('-DUSE_DMABUF', language : ['c', 'cpp'])
endif

if get_option('enable_impeller')
  add_project_arguments('-DENABLE_IMPELLER', language : ['c', 'cpp'])
endif

if get_option('enable_damage_history')
  add_project_arguments('-DDAMAGE_HISTORY', language : ['c', 'cpp'])
endif

if get_option('enable_vulkan')
  vulkan = dependency('vulkan', required: true)
  add_project_arguments('-DENABLE_VULKAN', language : ['c', 'cpp'])
else
  vulkan = dependency('', required: false)
endif

# add_project_arguments('-Dwerror=false', language : ['c', 'cpp'])

wlroots = subproject(
  'wlroots',
  default_options: ['examples=false', 'werror=false'],
).get_variable('wlroots')

# rapidjson = subproject(
#  'rapidjson',
#  default_options: ['werror=false'],
#).get_variable('rapidjson_dep')

wlroots_features = {
  'xwayland': false,
  'drm_backend': false,
  'gles2_renderer': false,
  'vulkan_renderer': false,
  'libinput_backend': false,
  'session': false,
  'x11_backend': false,
}

foreach name, _ : wlroots_features
  var_name = 'have_' + name.underscorify()
  wlroots_features += { name: wlroots.get_variable(pkgconfig: var_name, internal: var_name) == 'true' }
endforeach

required_wlroots_features= [
  'drm_backend',
  'session',
  'libinput_backend',
]
missing_wlroots_features = []

# Required features based on currently exposed public api
foreach required_feature : required_wlroots_features
  if not wlroots_features[required_feature]
    missing_wlroots_features += required_feature
  endif
endforeach

if missing_wlroots_features.length() > 0
  error('wlroots is missing the following required features: @0@'.format(' '.join(missing_wlroots_features)))
endif

# if not glm.found() and not cpp.check_header('glm/glm.hpp')
#   error('GLM not found, and directly using the header \'glm/glm.hpp\' is not possible.')
# endif

backtrace = cpp.find_library('execinfo', required: false)

# Compile new libinput features only if they are supported with the libinput installed version
if cpp.has_function('libinput_device_config_3fg_drag_set_enabled', dependencies: libinput)
  add_project_arguments('-DHAVE_LIBINPUT_3FG_DRAG=1', language : ['cpp', 'c'])
endif

# Needed on some older compilers
if cpp.has_link_argument('-lc++fs')
  add_project_link_arguments(['-lc++fs'], language: 'cpp')
elif cpp.has_link_argument('-lc++experimental')
  add_project_link_arguments(['-lc++experimental'], language: 'cpp')
elif cpp.has_link_argument('-lstdc++fs')
  add_project_link_arguments(['-lstdc++fs'], language: 'cpp')
endif

if get_option('enable_gles32')
  cpp.check_header('GLES3/gl32.h', dependencies: glesv2, required: true)
  add_project_arguments('-DUSE_GLES32', language : ['cpp', 'c'])
endif

sparrow_conf_inc = include_directories(['.'])

add_project_arguments(['-Wno-unused-parameter'], language: 'cpp')

if get_option('xwayland').enabled() and not wlroots_features['xwayland']
  error('Cannot enable Xwayland in sparrow: wlroots has been built without Xwayland support')
endif

if get_option('xwayland').enabled() or (get_option('xwayland').auto() and wlroots_features['xwayland'])
  xcb = dependency('xcb')
  add_project_arguments('-DHAS_XWAYLAND', language : ['cpp', 'c'])
else
  xcb = dependency('', required: false)
endif

if get_option('enable_vulkan') and not wlroots_features['vulkan_renderer']
  error('Cannot enable Vulkan in sparrow: wlroots has been built without Vulkan renderer support')
endif

if get_option('print_trace')
  print_trace = true
  add_project_arguments('-DPRINT_TRACE', language : ['cpp', 'c'])
else
  print_trace = false
endif

subdir('proto')
subdir('platform')
subdir('src')

summary = [
	'',
	'----------------',
	'sparrow @0@'.format(meson.project_version()),
	'',
    '       xwayland: @0@'.format(wlroots_features['xwayland']),
    '    x11-backend: @0@'.format(wlroots_features['x11_backend']),
    '         vulkan: @0@'.format(get_option('enable_vulkan')),
    '       impeller: @0@'.format(get_option('enable_impeller')),
    '    print trace: @0@'.format(print_trace),
    '          debug: @0@'.format(is_debug),
    '           asan: @0@'.format(is_asan),
    '          ubsan: @0@'.format(is_ubsan),
    '           tsan: @0@'.format(is_tsan),
    '----------------',
    ''
]
message('\n'.join(summary))
