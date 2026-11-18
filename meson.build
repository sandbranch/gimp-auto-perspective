project('gimp-auto-perspective', 'c',
  version: '0.0.1',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 0.60.0',
  default_options: ['c_std=gnu11', 'warning_level=2', 'buildtype=debugoptimized'],
)

cc = meson.get_compiler('c')
math_dep = cc.find_library('m', required: false)

# the core: line detection and fitting; no GIMP, no GEGL
core_lib = static_library('perspective-core',
  'src/core/perspective.c',
  'src/core/lsd.c',
  'src/core/model.c',
  'src/core/simplex.c',
  'src/core/ransac.c',
  'src/core/crop.c',
  dependencies: math_dep,
)
core_dep = declare_dependency(link_with: core_lib,
  include_directories: include_directories('src/core'),
  dependencies: math_dep)

# the core on the command line (PFM files), for the tests
auto_perspective_cli = executable('auto-perspective-cli', 'tools/auto-perspective-cli.c',
  dependencies: core_dep,
  install: false,
)

# unit tests of the core: meson test, or tests/check.sh (also under ASan)
foreach t : ['core']
  test(t, executable('test-' + t, 'tests/unit/test-' + t + '.c',
                     'tests/ref/ashift-homography.c',
                     dependencies: core_dep),
       timeout: 300)
endforeach

gimp_dep = dependency('gimp-3.0', version: '>= 3.0.0', required: get_option('plugin'))
gimpui_dep = dependency('gimpui-3.0', version: '>= 3.0.0', required: get_option('plugin'))

if gimp_dep.found() and gimpui_dep.found()
  plugindir = get_option('plugindir')
  if plugindir == ''
    plugindir = gimp_dep.get_variable(pkgconfig: 'gimplibdir') / 'plug-ins'
  endif
  # GIMP 3 loads a plug-in from a folder of its own name
  executable('gimp-auto-perspective',
    'src/plugin/main.c',
    dependencies: [core_dep, gimp_dep, gimpui_dep],
    c_args: ['-Wno-unused-parameter'],
    install: true,
    install_dir: plugindir / 'gimp-auto-perspective',
  )
endif
