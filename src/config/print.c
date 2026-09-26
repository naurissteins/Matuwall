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
	FILE *out, const char *name, struct matuwall_color color) {
	fprintf(out, "%s = \"#%02x%02x%02x%02x\"\n", name, color.r, color.g,
		color.b, color.a);
}

static void print_hooks(FILE *out, const struct matuwall_config *config) {
	fputs("[hooks]\non_apply = [\n", out);
	for (size_t i = 0; i < config->on_apply_count; i++) {
		fputs("  ", out);
		print_quoted(out, config->on_apply[i]);
		fputs(",\n", out);
	}
	fputs("]\n", out);
}

static void print_backend_args(FILE *out, const char *backend,
	const struct matuwall_backend_args *args) {
	fprintf(out, "\n[backend.%s]\nargs = [", backend);
	for (size_t i = 0; i < args->count; i++) {
		fputs(i == 0 ? "" : ", ", out);
		print_quoted(out, args->items[i]);
	}
	fputs("]\n", out);
}

bool matuwall_config_print(FILE *out, const struct matuwall_config *config) {
	fputs("[general]\ndirectory = ", out);
	print_quoted(out, config->directory);
	fputs("\nbackend = ", out);
	print_quoted(out, config->backend);
	fputc('\n', out);
	print_backend_args(out, "sweetbg", &config->sweetbg_args);
	print_backend_args(out, "awww", &config->awww_args);

	fputs("\n[window]\npreview = ", out);
	fputs(config->preview ? "true\n" : "false\n", out);
	fputs("close_on_focus_loss = ", out);
	fputs(config->close_on_focus_loss ? "true\n" : "false\n", out);
	fputs("position = ", out);
	print_quoted(out, matuwall_position_name(config->position));
	fputc('\n', out);
	print_color(out, "background", config->background);
	fprintf(out, "margin = %u\nedge_margin = %u\nradius = %u\n",
		config->layout.margin, config->edge_margin,
		config->panel_radius);
	fputs("\n[input]\nmouse = ", out);
	fputs(config->mouse_enabled ? "true\n" : "false\n", out);
	fprintf(out, "\n[animation]\nnavigation_ms = %u\nzoom_percent = %u\n",
		config->navigation_ms, config->zoom_percent);

	fprintf(out,
		"\n[grid]\ncolumns = %u\nvisible_rows = %u\ncarousel = %s\n"
		"edge = \"%s\"\n"
		"spacing = %u\n"
		"radius = %u\nborder_width = %u\nshadow_width = %u\n"
		"ring_width = %u\n",
		config->layout.columns, config->visible_rows,
		config->carousel ? "true" : "false",
		matuwall_edge_name(config->edge), config->layout.spacing,
		config->layout.radius, config->border_width,
		config->shadow_width, config->ring_width);
	fprintf(out, "\n[thumbnail]\nwidth = %u\nheight = %u\n",
		config->layout.tile_width, config->layout.tile_height);

	fputs("\n[colors]\n", out);
	print_color(out, "tile", config->tile);
	print_color(out, "border", config->border);
	print_color(out, "shadow", config->shadow);
	print_color(out, "ring", config->ring);
	print_color(out, "spinner", config->spinner);
	fputc('\n', out);
	print_hooks(out, config);
	return ferror(out) == 0;
}
