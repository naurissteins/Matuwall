scdoc = find_program('scdoc', required: false)

if scdoc.found()
	man_pages = {
		'sweetwall.1.scd': 'man1',
	}
	mandir = get_option('mandir')

	foreach source, section : man_pages
		custom_target(
			source.substring(0, -4),
			input: source,
			output: source.substring(0, -4),
			command: scdoc,
			feed: true,
			capture: true,
			install: true,
			install_dir: mandir / section,
		)
	endforeach
endif
