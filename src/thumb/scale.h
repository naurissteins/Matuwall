#ifndef MATUWALL_THUMB_SCALE_H
#define MATUWALL_THUMB_SCALE_H

#include "thumb/decode.h"

void matuwall_cover_crop(uint32_t src_w, uint32_t src_h, uint32_t out_w,
	uint32_t out_h, uint32_t *crop_x, uint32_t *crop_y, uint32_t *crop_w,
	uint32_t *crop_h);

// source span covered by one output pixel along an axis
struct matuwall_span {
	uint32_t start;
	uint32_t count;
};

struct matuwall_span matuwall_axis_span(
	uint32_t origin, uint32_t extent, uint32_t output, uint32_t index);

bool matuwall_scale_cover(const struct matuwall_image *src, uint32_t out_w,
	uint32_t out_h, struct matuwall_image *out, const atomic_bool *stop);

// cover box filter fed packed RGB source rows in order, one at a time, the
// same boxes and sums as matuwall_scale_cover, without the whole source
struct matuwall_row_scaler {
	uint32_t *pixels;
	uint32_t out_w;
	uint32_t out_h;
	uint32_t crop_y;
	uint32_t crop_h;
	uint32_t next_row;
	struct matuwall_span rows;
	struct matuwall_span *columns;
	uint64_t *sums;
};

// pixels is caller owned and holds out_w * out_h entries
bool matuwall_row_scaler_init(struct matuwall_row_scaler *scaler,
	uint32_t src_w, uint32_t src_h, uint32_t out_w, uint32_t out_h,
	uint32_t *pixels);

void matuwall_row_scaler_push(
	struct matuwall_row_scaler *scaler, uint32_t y, const uint8_t *rgb);

// true once every output row is written; later source rows are unused
bool matuwall_row_scaler_done(const struct matuwall_row_scaler *scaler);

// frees the scaler's own buffers, safe to call twice
void matuwall_row_scaler_finish(struct matuwall_row_scaler *scaler);

// bytes the scaler allocates besides the caller's pixels
uint64_t matuwall_row_scaler_bytes(uint32_t out_w);

#endif
