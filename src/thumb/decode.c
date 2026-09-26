#include "thumb/decode.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "thumb/decode/format.h"

#define MAX_DIMENSION 16384u

bool matuwall_image_dimensions_ok(uint32_t width, uint32_t height) {
	if (width == 0 || height == 0 || width > MAX_DIMENSION ||
		height > MAX_DIMENSION) {
		return false;
	}
	return (uint64_t)width * height <= MATUWALL_IMAGE_MAX_PIXELS;
}

// --- dispatch ---

static bool is_png(const uint8_t *sig, size_t n) {
	static const uint8_t magic[8] = {
		0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
	return n >= 8 && memcmp(sig, magic, 8) == 0;
}

static bool is_jpeg(const uint8_t *sig, size_t n) {
	return n >= 3 && sig[0] == 0xff && sig[1] == 0xd8 && sig[2] == 0xff;
}

static bool is_webp(const uint8_t *sig, size_t n) {
	return n >= 12 && memcmp(sig, "RIFF", 4) == 0 &&
	       memcmp(sig + 8, "WEBP", 4) == 0;
}

// O_NONBLOCK keeps a FIFO named like an image from wedging a worker
static FILE *open_regular(const char *path) {
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0) {
		return NULL;
	}
	struct stat info;
	if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
		close(fd);
		return NULL;
	}
	FILE *fp = fdopen(fd, "rb");
	if (fp == NULL) {
		close(fd);
	}
	return fp;
}

bool matuwall_image_decode(struct matuwall_image *img, const char *path,
	uint32_t target_w, uint32_t target_h,
	enum matuwall_decode_purpose purpose, const atomic_bool *stop,
	const struct matuwall_decode_budget *budget) {
	*img = (struct matuwall_image){0};
	if (!decode_dimensions_ok(target_w, target_h, purpose) ||
		stop_requested(stop)) {
		return false;
	}
	const struct decode_job job = {
		.target_w = target_w,
		.target_h = target_h,
		.purpose = purpose,
		.stop = stop,
		.budget = budget,
	};

	FILE *fp = open_regular(path);
	if (fp == NULL) {
		return false;
	}

	uint8_t sig[12];
	size_t got = fread(sig, 1, sizeof(sig), fp);
	if (fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return false;
	}

	bool ok;
	if (is_png(sig, got)) {
		ok = matuwall_decode_png(fp, img, &job);
	} else if (is_jpeg(sig, got)) {
		ok = matuwall_decode_jpeg(fp, img, &job);
	} else if (is_webp(sig, got)) {
		ok = matuwall_decode_webp(fp, img, &job);
	} else {
		ok = false;
	}

	fclose(fp);
	return ok;
}

void matuwall_image_free(struct matuwall_image *img) {
	free(img->pixels);
	img->pixels = NULL;
	img->width = 0;
	img->height = 0;
}
