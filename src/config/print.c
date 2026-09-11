#include "config/print.h"

#include <stdint.h>

static void print_quoted(FILE *out, const char *value) {
	fputc('"', out);
	for (const unsigned char *p = (const unsigned char *)value; *p != '\0';
		p++) {
		switch (*p) {
		case '"':
			fputs("\\\"", out);
			break;
		case '\\':
			fputs("\\\\", out);
			break;
		case '\b':
			fputs("\\b", out);
			break;
		case '\t':
			fputs("\\t", out);
			break;
		case '\n':
			fputs("\\n", out);
			break;
		case '\f':
			fputs("\\f", out);
			break;
		case '\r':
			fputs("\\r", out);
			break;
		default:
			if (*p < 0x20 || *p == 0x7f) {
				fprintf(out, "\\u%04x", *p);
			} else {
				fputc(*p, out);
			}
			break;
		}
	}
	fputc('"', out);
}

static void print_color(
	FILE *out, const char *name, struct sweetwall_color color) {
	fprintf(out, "%s = \"#%02x%02x%02x%02x\"\n", name, color.r, color.g,
		color.b, color.a);
}

static void print_hooks(FILE *out, const struct sweetwall_config *config) {
	fputs("[hooks]\non_apply = [\n", out);
	for (size_t i = 0; i < config->on_apply_count; i++) {
		fputs("  ", out);
		print_quoted(out, config->on_apply[i]);
		fputs(",\n", out);
	}
	fputs("]\n", out);
}

bool sweetwall_config_print(FILE *out, const struct sweetwall_config *config) {
	fputs("[general]\ndirectory = ", out);
	print_quoted(out, config->directory);
	fputs("\nbackend = ", out);
	print_quoted(out, config->backend);

	fputs("\n\n[window]\npreview = ", out);
	fputs(config->preview ? "true\n" : "false\n", out);
	fputs("position = ", out);
	print_quoted(out, sweetwall_position_name(config->position));
	fputc('\n', out);
	print_color(out, "background", config->background);
	fprintf(out, "margin = %u\nradius = %u\n", config->layout.margin,
		config->panel_radius);

	fprintf(out,
		"\n[grid]\ncolumns = %u\nvisible_rows = %u\nspacing = %u\n"
		"radius = %u\nring_width = %u\n",
		config->layout.columns, config->visible_rows,
		config->layout.spacing, config->layout.radius,
		config->ring_width);
	fprintf(out, "\n[thumbnail]\nwidth = %u\nheight = %u\n",
		config->layout.tile_width, config->layout.tile_height);

	fputs("\n[colors]\n", out);
	print_color(out, "tile", config->tile);
	print_color(out, "ring", config->ring);
	print_color(out, "spinner", config->spinner);
	fputc('\n', out);
	print_hooks(out, config);
	return ferror(out) == 0;
}
