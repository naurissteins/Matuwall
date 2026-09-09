# Generate Wayland client glue with wayland-scanner. xdg-shell is generated
# only so wlr-layer-shell's xdg_popup references resolve at link time;
# sweetwall does not use xdg-shell directly.

wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')

protocols = [
	wl_protocol_dir / 'stable' / 'xdg-shell' / 'xdg-shell.xml',
	wl_protocol_dir / 'stable' / 'viewporter' / 'viewporter.xml',
	wl_protocol_dir / 'staging' / 'fractional-scale' / 'fractional-scale-v1.xml',
	meson.current_source_dir() / 'wlr-layer-shell-unstable-v1.xml',
]

protocol_src = []
protocol_headers = []

foreach xml : protocols
	base = '@0@'.format(xml).split('/')[-1].split('.xml')[0]

	protocol_headers += custom_target(
		base + '-client-protocol.h',
		input: xml,
		output: base + '-client-protocol.h',
		command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
	)

	protocol_src += custom_target(
		base + '-protocol.c',
		input: xml,
		output: base + '-protocol.c',
		command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
	)
endforeach

sweetwall_protocols = static_library(
	'sweetwall-protocols',
	protocol_src,
	protocol_headers,
	dependencies: wayland_client,
)

sweetwall_protocols_dep = declare_dependency(
	link_with: sweetwall_protocols,
	# listing the headers as sources makes their build dir an include path
	sources: protocol_headers,
)
