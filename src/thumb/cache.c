#include "thumb/cache.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util/fs.h"

#define CACHE_MAGIC 0x53575443u // "SWTC"
#define CACHE_VERSION 2u
#define CACHE_FORMAT_ARGB8888 0u

struct cache_header {
	uint32_t magic;
	uint32_t version;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t format;
	uint32_t source_path_length;
	uint32_t target_w;
	uint32_t target_h;
	int64_t mtime_sec;
	int64_t source_size;
	uint32_t mtime_nsec;
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
	struct matuwall_cache_key *out) {
	*out = (struct matuwall_cache_key){0};
	size_t path_length = strlen(source_path);
	if (path_length == 0 || path_length > UINT32_MAX) {
		return false;
	}
	struct stat info;
	if (stat(source_path, &info) != 0 || info.st_size < 0 ||
		info.st_mtim.tv_nsec < 0 ||
		info.st_mtim.tv_nsec >= 1000000000L) {
		return false;
	}

	uint64_t hash = 0xcbf29ce484222325ull;
	hash = fnv1a(hash, source_path, path_length);
	hash = fnv1a(hash, &info.st_mtime, sizeof(info.st_mtime));
	hash = fnv1a(hash, &info.st_size, sizeof(info.st_size));
	hash = fnv1a(hash, &target_w, sizeof(target_w));
	hash = fnv1a(hash, &target_h, sizeof(target_h));

	if ((size_t)snprintf(out->filename, sizeof(out->filename), "%s/%016llx",
		    cache->directory,
		    (unsigned long long)hash) >= sizeof(out->filename)) {
		return false;
	}
	out->source_path = source_path;
	out->source_path_length = (uint32_t)path_length;
	out->mtime_sec = (int64_t)info.st_mtim.tv_sec;
	out->mtime_nsec = (uint32_t)info.st_mtim.tv_nsec;
	out->source_size = (int64_t)info.st_size;
	out->target_w = target_w;
	out->target_h = target_h;
	return true;
}

// --- read ---

static bool header_ok(const struct cache_header *h,
	const struct matuwall_cache_key *key, off_t file_size) {
	if (h->magic != CACHE_MAGIC || h->version != CACHE_VERSION ||
		h->format != CACHE_FORMAT_ARGB8888) {
		return false;
	}
	if (h->source_path_length != key->source_path_length ||
		h->mtime_sec != key->mtime_sec ||
		h->mtime_nsec != key->mtime_nsec ||
		h->source_size != key->source_size ||
		h->target_w != key->target_w || h->target_h != key->target_h ||
		h->width != key->target_w || h->height != key->target_h) {
		return false;
	}
	if (!matuwall_image_dimensions_ok(h->width, h->height)) {
		return false;
	}
	if (h->stride != h->width * sizeof(uint32_t)) {
		return false;
	}
	uint64_t expected = sizeof(*h) + h->source_path_length +
			    (uint64_t)h->stride * h->height;
	return file_size >= 0 && (uint64_t)file_size == expected;
}

static bool source_path_matches(
	FILE *fp, const struct matuwall_cache_key *key) {
	char bytes[256];
	for (size_t offset = 0; offset < key->source_path_length;) {
		size_t remaining = key->source_path_length - offset;
		size_t count =
			remaining < sizeof(bytes) ? remaining : sizeof(bytes);
		if (fread(bytes, 1, count, fp) != count ||
			memcmp(bytes, key->source_path + offset, count) != 0) {
			return false;
		}
		offset += count;
	}
	return true;
}

bool matuwall_cache_read(
	const struct matuwall_cache_key *key, struct matuwall_image *img) {
	*img = (struct matuwall_image){0};

	// entries are only ever renamed-in regular files
	int fd = open(
		key->filename, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0) {
		return false;
	}
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		close(fd);
		return false;
	}
	FILE *fp = fdopen(fd, "rb");
	if (fp == NULL) {
		close(fd);
		return false;
	}

	struct cache_header header;
	if (fread(&header, sizeof(header), 1, fp) != 1 ||
		!header_ok(&header, key, st.st_size) ||
		!source_path_matches(fp, key)) {
		fclose(fp);
		return false;
	}

	size_t count = (size_t)header.width * header.height;
	if (count == 0 || count > MATUWALL_IMAGE_MAX_PIXELS) {
		fclose(fp);
		return false;
	}
	// count is capped above, so this cannot overflow; the explicit bound on
	// the allocation size itself also keeps static analysis happy
	size_t bytes = count * sizeof(uint32_t);
	if (bytes > (size_t)MATUWALL_IMAGE_MAX_PIXELS * sizeof(uint32_t)) {
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

struct matuwall_cache *matuwall_cache_create(void) {
	struct matuwall_cache *cache = calloc(1, sizeof(*cache));
	if (cache == NULL ||
		!matuwall_cache_dir(
			cache->directory, sizeof(cache->directory)) ||
		!matuwall_fs_make_dirs(cache->directory, 0755)) {
		free(cache);
		return NULL;
	}
	return cache;
}

void matuwall_cache_destroy(struct matuwall_cache *cache) {
	free(cache);
}

static bool source_metadata_unchanged(const struct matuwall_cache_key *key) {
	struct stat info;
	return stat(key->source_path, &info) == 0 &&
	       (int64_t)info.st_mtim.tv_sec == key->mtime_sec &&
	       (uint32_t)info.st_mtim.tv_nsec == key->mtime_nsec &&
	       (int64_t)info.st_size == key->source_size;
}

void matuwall_cache_write(const struct matuwall_cache_key *key,
	const struct matuwall_image *img) {
	if (img->pixels == NULL || img->width != key->target_w ||
		img->height != key->target_h ||
		!matuwall_image_dimensions_ok(img->width, img->height) ||
		!source_metadata_unchanged(key)) {
		return;
	}
	char tmp[sizeof(key->filename) + sizeof(".tmpXXXXXX")];
	if ((size_t)snprintf(tmp, sizeof(tmp), "%s.tmpXXXXXX", key->filename) >=
		sizeof(tmp)) {
		return;
	}
	int fd = mkstemp(tmp);
	if (fd < 0) {
		return;
	}

	struct cache_header header;
	memset(&header, 0, sizeof(header));
	header.magic = CACHE_MAGIC;
	header.version = CACHE_VERSION;
	header.width = img->width;
	header.height = img->height;
	header.stride = img->width * (uint32_t)sizeof(uint32_t);
	header.format = CACHE_FORMAT_ARGB8888;
	header.source_path_length = key->source_path_length;
	header.target_w = key->target_w;
	header.target_h = key->target_h;
	header.mtime_sec = key->mtime_sec;
	header.source_size = key->source_size;
	header.mtime_nsec = key->mtime_nsec;
	size_t count = (size_t)img->width * img->height;

	FILE *fp = fdopen(fd, "wb");
	if (fp == NULL) {
		close(fd);
		unlink(tmp);
		return;
	}

	bool ok = fwrite(&header, sizeof(header), 1, fp) == 1 &&
		  fwrite(key->source_path, 1, key->source_path_length, fp) ==
			  key->source_path_length &&
		  fwrite(img->pixels, sizeof(uint32_t), count, fp) == count;
	// A torn thumbnail must never be visible: only publish a full file
	if (fclose(fp) != 0 || !ok || rename(tmp, key->filename) != 0) {
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
