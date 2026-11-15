#include "thumb/hue.h"

#include <stddef.h>

#define BINS 36
#define BIN_DEGREES (360 / BINS)
#define MAX_SAMPLES 16384
// below this chroma a pixel counts as grey
#define MIN_CHROMA 32
// fewer colourful samples than 1 in this many makes a grey image
#define GREY_RATIO 20
#define GREY_GROUP (1u << 24)

struct bin {
	uint64_t weight;
	uint64_t hue;
	uint64_t lightness;
};

static uint32_t pixel_hue(int r, int g, int b, int max, int chroma) {
	int hue;
	if (max == r) {
		hue = 60 * (g - b) / chroma;
	} else if (max == g) {
		hue = 120 + 60 * (b - r) / chroma;
	} else {
		hue = 240 + 60 * (r - g) / chroma;
	}
	return (uint32_t)((hue + 360) % 360);
}

// chroma-weighted mean over the peak bin and its neighbours, wrapping
static uint32_t peak_key(const struct bin *bins) {
	size_t peak = 0;
	uint64_t best = 0;
	for (size_t i = 0; i < BINS; i++) {
		uint64_t near = bins[(i + BINS - 1) % BINS].weight +
				bins[i].weight + bins[(i + 1) % BINS].weight;
		if (near > best) {
			best = near;
			peak = i;
		}
	}

	int64_t hue = 0;
	uint64_t lightness = 0;
	for (int offset = -1; offset <= 1; offset++) {
		int slot = (int)peak + offset;
		int64_t shift = slot < 0 ? -360 : slot >= BINS ? 360 : 0;
		const struct bin *bin = &bins[(slot + BINS) % BINS];
		hue += (int64_t)bin->hue + shift * (int64_t)bin->weight;
		lightness += bin->lightness;
	}
	int64_t mean = hue / (int64_t)best;
	uint32_t degrees = (uint32_t)((mean % 360 + 360) % 360);
	return degrees << 8 | (uint32_t)(lightness / best);
}

uint32_t matuwall_hue_key(
	const uint32_t *pixels, uint32_t width, uint32_t height) {
	if (pixels == NULL || width == 0 || height == 0) {
		return MATUWALL_HUE_UNKNOWN;
	}
	uint32_t step = 1;
	while ((uint64_t)(width / step) * (height / step) > MAX_SAMPLES) {
		step++;
	}

	struct bin bins[BINS] = {0};
	uint64_t samples = 0;
	uint64_t colourful = 0;
	uint64_t lightness = 0;
	for (uint32_t y = 0; y < height; y += step) {
		const uint32_t *row = pixels + (size_t)y * width;
		for (uint32_t x = 0; x < width; x += step) {
			int r = (int)(row[x] >> 16 & 0xff);
			int g = (int)(row[x] >> 8 & 0xff);
			int b = (int)(row[x] & 0xff);
			int max = r > g ? (r > b ? r : b) : (g > b ? g : b);
			int min = r < g ? (r < b ? r : b) : (g < b ? g : b);
			int chroma = max - min;
			uint32_t light = (uint32_t)(max + min) / 2;
			samples++;
			lightness += light;
			if (chroma < MIN_CHROMA) {
				continue;
			}
			uint32_t hue = pixel_hue(r, g, b, max, chroma);
			struct bin *bin = &bins[hue / BIN_DEGREES];
			bin->weight += (uint64_t)chroma;
			bin->hue += (uint64_t)chroma * hue;
			bin->lightness += (uint64_t)chroma * light;
			colourful++;
		}
	}

	if (colourful * GREY_RATIO < samples) {
		return GREY_GROUP | (255u - (uint32_t)(lightness / samples));
	}
	return peak_key(bins);
}
