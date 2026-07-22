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
	language: 'c',
)

fs = import('fs')

sweetwall_inc = include_directories('src')

executable(
	'sweetwall',
	'src/main.c',
	include_directories: sweetwall_inc,
	install: true,
)

subdir('doc')

# tests/ is local-only
if fs.exists('tests/meson.build')
	subdir('tests')
endif
