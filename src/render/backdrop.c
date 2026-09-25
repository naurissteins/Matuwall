#include "render/backdrop.h"

#include <stdlib.h>
#include <string.h>

#include "render/draw.h"
#include "render/image.h"

// The preview backdrop behind the panel. Once every buffer holds the full
// image, only the patch under the panel is needed to repaint it

static bool same_rect(struct matuwall_damage a, struct matuwall_damage b) {
	return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

static bool patch_matches(const struct matuwall_backdrop_patch *patch,
	const struct matuwall_buffer *buffer, struct matuwall_damage rect,
	uint64_t generation) {
	return patch != NULL && patch->valid &&
	       patch->generation == generation &&
	       patch->buffer_width == buffer->width &&
	       patch->buffer_height == buffer->height &&
	       same_rect(patch->rect, rect);
}

// painted buffer of this exact frame shape, safe to read while busy
static bool sibling_matches(const struct matuwall_buffer *sibling,
	const struct matuwall_buffer *buffer, struct matuwall_damage rect,
	uint64_t generation) {
	return sibling != NULL && sibling != buffer && sibling->frame_valid &&
	       sibling->backdrop_generation == generation &&
	       sibling->width == buffer->width &&
	       sibling->height == buffer->height &&
	       same_rect(sibling->overlay_damage, rect);
}

static bool damage_inside(
	struct matuwall_damage inner, struct matuwall_damage outer) {
	return inner.x0 >= outer.x0 && inner.y0 >= outer.y0 &&
	       inner.x1 <= outer.x1 && inner.y1 <= outer.y1;
}

// taken before the panel is drawn, while the buffer holds pure backdrop
static void capture_patch(struct matuwall_backdrop_patch *patch,
	const struct matuwall_buffer *buffer, struct matuwall_damage rect,
	uint64_t generation) {
	size_t width = (size_t)(rect.x1 - rect.x0);
	size_t height = (size_t)(rect.y1 - rect.y0);
	size_t count = width * height;
	patch->valid = false;
	if (count == 0) {
		return;
	}
	if (count > patch->capacity) {
		uint32_t *pixels =
			realloc(patch->pixels, count * sizeof(uint32_t));
		if (pixels == NULL) {
			return;
		}
		patch->pixels = pixels;
		patch->capacity = count;
	}
	for (size_t row = 0; row < height; row++) {
		memcpy(patch->pixels + row * width,
			buffer->data +
				(size_t)(rect.y0 + (int32_t)row) *
					buffer->width +
				(size_t)rect.x0,
			width * sizeof(uint32_t));
	}
	*patch = (struct matuwall_backdrop_patch){
		.pixels = patch->pixels,
		.capacity = patch->capacity,
		.rect = rect,
		.buffer_width = buffer->width,
		.buffer_height = buffer->height,
		.generation = generation,
		.valid = true,
		.missed = patch->missed,
	};
}

static void restore_patch(struct matuwall_buffer *buffer,
	const struct matuwall_backdrop_patch *patch,
	struct matuwall_damage damage) {
	size_t patch_width = (size_t)(patch->rect.x1 - patch->rect.x0);
	size_t count = (size_t)(damage.x1 - damage.x0);
	for (int32_t y = damage.y0; y < damage.y1; y++) {
		memcpy(buffer->data + (size_t)y * buffer->width +
				(size_t)damage.x0,
			patch->pixels +
				(size_t)(y - patch->rect.y0) * patch_width +
				(size_t)(damage.x0 - patch->rect.x0),
			count * sizeof(uint32_t));
	}
}

bool matuwall_backdrop_draw(struct matuwall_buffer *buffer,
	const struct matuwall_backdrop *backdrop, struct matuwall_damage damage,
	struct matuwall_damage panel, uint64_t generation, bool full) {
	struct matuwall_clip damaged = {
		damage.x0, damage.y0, damage.x1, damage.y1};
	struct matuwall_backdrop_patch *patch = backdrop->patch;
	if (backdrop->image != NULL) {
		matuwall_draw_image_cover_clipped(buffer, &damaged,
			backdrop->image, backdrop->width, backdrop->height);
		if (full && patch != NULL &&
			!patch_matches(patch, buffer, panel, generation)) {
			capture_patch(patch, buffer, panel, generation);
		}
		return true;
	}
	if (patch_matches(patch, buffer, panel, generation)) {
		if (!full && damage_inside(damage, patch->rect)) {
			restore_patch(buffer, patch, damage);
			return true;
		}
		if (full && sibling_matches(backdrop->sibling, buffer, panel,
				    generation)) {
			memcpy(buffer->data, backdrop->sibling->data,
				(size_t)buffer->stride * buffer->height);
			restore_patch(buffer, patch, panel);
			return true;
		}
	}
	// nothing holds these pixels any more; the owner decodes them again
	if (patch != NULL && generation != 0) {
		patch->missed = true;
	}
	matuwall_draw_clear_clipped(buffer, &damaged, 0);
	return false;
}
