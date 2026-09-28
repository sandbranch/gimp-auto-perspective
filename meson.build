project('gimp-upright', 'c',
  version: '0.0.1',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 0.60.0',
  default_options: ['c_std=gnu11', 'warning_level=2', 'buildtype=debugoptimized'],
)

cc = meson.get_compiler('c')
math_dep = cc.find_library('m', required: false)

# the core: line detection and fitting; no GIMP, no GEGL
core_lib = static_library('upright-core',
  'src/core/upright.c',
  dependencies: math_dep,
)
core_dep = declare_dependency(link_with: core_lib,
  include_directories: include_directories('src/core'),
  dependencies: math_dep)

# the core on the command line (PFM files), for the tests
upright_cli = executable('upright-cli', 'tools/upright-cli.c',
  dependencies: core_dep,
  install: false,
)

# unit tests of the core: meson test, or tests/check.sh (also under ASan)
foreach t : ['core']
  test(t, executable('test-' + t, 'tests/unit/test-' + t + '.c',
                     dependencies: core_dep),
       timeout: 300)
endforeach
