project(
	'sweetwall',
	'c',
	version: '0.1.0',
	meson_version: '>= 0.60.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
	],
)

add_project_arguments(
	'-DSWEETWALL_VERSION="' + meson.project_version() + '"',
	# memfd_create, sigaction, and the wider POSIX surface
	'-D_GNU_SOURCE',
	language: 'c',
)

fs = import('fs')

wayland_client = dependency('wayland-client')
wayland_protos = dependency('wayland-protocols')
xkbcommon = dependency('xkbcommon')
libm = meson.get_compiler('c').find_library('m', required: false)

wayland_scanner_dep = dependency('wayland-scanner', native: true)
wayland_scanner = find_program(
	wayland_scanner_dep.get_variable('wayland_scanner'),
	native: true,
)

subdir('protocol')

sweetwall_inc = include_directories('src')

executable(
	'sweetwall',
	'src/main.c',
	'src/app.c',
	'src/grid/layout.c',
	'src/render/draw.c',
	'src/render/frame.c',
	'src/scan/dirscan.c',
	'src/wayland/registry.c',
	'src/wayland/layer.c',
	'src/wayland/seat.c',
	'src/wayland/shm.c',
	include_directories: sweetwall_inc,
	dependencies: [wayland_client, xkbcommon, libm, sweetwall_protocols_dep],
	install: true,
)

subdir('doc')

# tests/ is local-only
if fs.exists('tests/meson.build')
	subdir('tests')
endif
