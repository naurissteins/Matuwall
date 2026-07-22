#include <stdio.h>
#include <string.h>

static void usage(FILE *out) {
	fputs("usage: sweetwall [options]\n"
	      "\n"
	      "options:\n"
	      "  -h, --help     show this help and exit\n"
	      "  -V, --version  show version and exit\n",
		out);
}

int main(int argc, char *argv[]) {
	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
			usage(stdout);
			return 0;
		}

		if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
			puts("sweetwall " SWEETWALL_VERSION);
			return 0;
		}

		fprintf(stderr, "sweetwall: unknown option '%s'\n", arg);
		usage(stderr);
		return 2;
	}

	// TODO: load config, map the layer surface, run the event loop
	return 0;
}
