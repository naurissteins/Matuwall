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
libpng = dependency('libpng')
libjpeg = dependency('libjpeg')
libwebp = dependency('libwebp')
threads = dependency('threads')
libm = meson.get_compiler('c').find_library('m', required: false)

# jpeg.c decodes straight into JCS_EXT_BGRA, a libjpeg-turbo extension
cc = meson.get_compiler('c')
if not cc.has_header_symbol(
	'jpeglib.h',
	'JCS_EXT_BGRA',
	prefix: '#include <stdio.h>',
	dependencies: libjpeg,
)
	error('libjpeg-turbo is required: jpeglib.h does not define JCS_EXT_BGRA')
endif

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
	'src/backend/registry.c',
	'src/backend/sweetbg.c',
	'src/config/config.c',
	'src/config/toml.c',
	'src/grid/layout.c',
	'src/grid/navigate.c',
	'src/hooks/hooks.c',
	'src/render/draw.c',
	'src/render/frame.c',
	'src/render/spinner.c',
	'src/scan/dirscan.c',
	'src/thumb/cache.c',
	'src/thumb/decode.c',
	'src/thumb/scale.c',
	'src/thumb/worker.c',
	'src/wayland/registry.c',
	'src/wayland/layer.c',
	'src/wayland/pointer.c',
	'src/wayland/seat.c',
	'src/wayland/shm.c',
	include_directories: sweetwall_inc,
	dependencies: [
		wayland_client,
		xkbcommon,
		libpng,
		libjpeg,
		libwebp,
		threads,
		libm,
		sweetwall_protocols_dep,
	],
	install: true,
)

subdir('doc')

# tests/ is local-only
if fs.exists('tests/meson.build')
	subdir('tests')
endif
