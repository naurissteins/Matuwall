#include "thumb/cache.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CACHE_MAGIC 0x53575443u // "SWTC"
#define CACHE_VERSION 1u
#define CACHE_FORMAT_ARGB8888 0u

// Keep cached pixel allocations within the decoder's maximum
#define MAX_PIXELS (1u << 26)

struct cache_header {
	uint32_t magic;
	uint32_t version;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t format;
};

struct matuwall_cache {
	char directory[512];
};

// --- key derivation ---

static uint64_t fnv1a(uint64_t hash, const void *data, size_t len) {
	const uint8_t *bytes = data;
	for (size_t i = 0; i < len; i++) {
		hash ^= bytes[i];
		hash *= 0x100000001b3ull;
	}
	return hash;
}

bool matuwall_cache_dir(char *out, size_t out_size) {
	const char *xdg = getenv("XDG_CACHE_HOME");
	if (xdg != NULL && xdg[0] == '/') {
		return (size_t)snprintf(out, out_size, "%s/matuwall/thumbs",
			       xdg) < out_size;
	}
	const char *home = getenv("HOME");
	if (home == NULL) {
		return false;
	}
	return (size_t)snprintf(out, out_size, "%s/.cache/matuwall/thumbs",
		       home) < out_size;
}

bool matuwall_cache_key(const struct matuwall_cache *cache,
	const char *source_path, uint32_t target_w, uint32_t target_h,
	char *out, size_t out_size) {
	struct stat info;
	if (stat(source_path, &info) != 0) {
		return false;
	}

	uint64_t hash = 0xcbf29ce484222325ull;
	hash = fnv1a(hash, source_path, strlen(source_path));
	hash = fnv1a(hash, &info.st_mtime, sizeof(info.st_mtime));
	hash = fnv1a(hash, &info.st_size, sizeof(info.st_size));
	hash = fnv1a(hash, &target_w, sizeof(target_w));
	hash = fnv1a(hash, &target_h, sizeof(target_h));

	return (size_t)snprintf(out, out_size, "%s/%016llx", cache->directory,
		       (unsigned long long)hash) < out_size;
}

// --- read ---

static bool header_ok(const struct cache_header *h, off_t file_size) {
	if (h->magic != CACHE_MAGIC || h->version != CACHE_VERSION ||
		h->format != CACHE_FORMAT_ARGB8888) {
		return false;
	}
	if (!matuwall_image_dimensions_ok(h->width, h->height)) {
		return false;
	}
	if (h->stride != h->width * sizeof(uint32_t)) {
		return false;
	}
	uint64_t expected = sizeof(*h) + (uint64_t)h->stride * h->height;
	return file_size >= 0 && (uint64_t)file_size == expected;
}

bool matuwall_cache_read(const char *key, struct matuwall_image *img) {
	*img = (struct matuwall_image){0};

	FILE *fp = fopen(key, "rb");
	if (fp == NULL) {
		return false;
	}

	struct stat st;
	struct cache_header header;
	if (fstat(fileno(fp), &st) != 0 ||
		fread(&header, sizeof(header), 1, fp) != 1 ||
		!header_ok(&header, st.st_size)) {
		fclose(fp);
		return false;
	}

	size_t count = (size_t)header.width * header.height;
	if (count == 0 || count > MAX_PIXELS) {
		fclose(fp);
		return false;
	}
	// count is capped above, so this cannot overflow; the explicit bound on
	// the allocation size itself also keeps static analysis happy
	size_t bytes = count * sizeof(uint32_t);
	if (bytes > (size_t)MAX_PIXELS * sizeof(uint32_t)) {
		fclose(fp);
		return false;
	}
	uint32_t *pixels = malloc(bytes);
	if (pixels == NULL) {
		fclose(fp);
		return false;
	}
	if (fread(pixels, sizeof(uint32_t), count, fp) != count) {
		free(pixels);
		fclose(fp);
		return false;
	}

	fclose(fp);
	img->width = header.width;
	img->height = header.height;
	img->pixels = pixels;
	return true;
}

// --- write ---

static bool make_cache_dir(char *dir) {
	for (char *p = dir + 1; *p != '\0'; p++) {
		if (*p != '/') {
			continue;
		}
		*p = '\0';
		if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
			return false;
		}
		*p = '/';
	}
	return mkdir(dir, 0755) == 0 || errno == EEXIST;
}

struct matuwall_cache *matuwall_cache_create(void) {
	struct matuwall_cache *cache = calloc(1, sizeof(*cache));
	if (cache == NULL ||
		!matuwall_cache_dir(
			cache->directory, sizeof(cache->directory)) ||
		!make_cache_dir(cache->directory)) {
		free(cache);
		return NULL;
	}
	return cache;
}

void matuwall_cache_destroy(struct matuwall_cache *cache) {
	free(cache);
}

void matuwall_cache_write(const char *key, const struct matuwall_image *img) {
	char tmp[576];
	if ((size_t)snprintf(tmp, sizeof(tmp), "%s.tmpXXXXXX", key) >=
		sizeof(tmp)) {
		return;
	}
	int fd = mkstemp(tmp);
	if (fd < 0) {
		return;
	}

	struct cache_header header = {
		.magic = CACHE_MAGIC,
		.version = CACHE_VERSION,
		.width = img->width,
		.height = img->height,
		.stride = img->width * (uint32_t)sizeof(uint32_t),
		.format = CACHE_FORMAT_ARGB8888,
	};
	size_t count = (size_t)img->width * img->height;

	FILE *fp = fdopen(fd, "wb");
	if (fp == NULL) {
		close(fd);
		unlink(tmp);
		return;
	}

	bool ok = fwrite(&header, sizeof(header), 1, fp) == 1 &&
		  fwrite(img->pixels, sizeof(uint32_t), count, fp) == count;
	// A torn thumbnail must never be visible: only publish a full file
	if (fclose(fp) != 0 || !ok || rename(tmp, key) != 0) {
		unlink(tmp);
	}
}

// --- maintenance ---

bool matuwall_cache_clear(size_t *removed, char *err, size_t err_size) {
	*removed = 0;
	char path[512];
	if (!matuwall_cache_dir(path, sizeof(path))) {
		snprintf(err, err_size, "cannot resolve the cache directory");
		return false;
	}

	int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) {
		if (errno == ENOENT) {
			return true;
		}
		snprintf(err, err_size, "%s: %s", path, strerror(errno));
		return false;
	}

	DIR *dir = fdopendir(fd);
	if (dir == NULL) {
		int saved = errno;
		close(fd);
		snprintf(err, err_size, "%s: %s", path, strerror(saved));
		return false;
	}

	int failure = 0;
	errno = 0;
	for (struct dirent *entry = readdir(dir); entry != NULL;
		entry = readdir(dir)) {
		if (strcmp(entry->d_name, ".") == 0 ||
			strcmp(entry->d_name, "..") == 0) {
			continue;
		}

		struct stat info;
		if (fstatat(fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) !=
			0) {
			if (errno == ENOENT) {
				errno = 0;
				continue;
			}
			failure = errno;
			break;
		}
		if (!S_ISREG(info.st_mode) && !S_ISLNK(info.st_mode)) {
			continue;
		}
		if (unlinkat(fd, entry->d_name, 0) != 0) {
			if (errno == ENOENT) {
				errno = 0;
				continue;
			}
			failure = errno;
			break;
		}
		(*removed)++;
		errno = 0;
	}
	if (failure == 0 && errno != 0) {
		failure = errno;
	}
	if (closedir(dir) != 0 && failure == 0) {
		failure = errno;
	}

	if (failure != 0) {
		snprintf(err, err_size, "%s: %s", path, strerror(failure));
		return false;
	}
	return true;
}
