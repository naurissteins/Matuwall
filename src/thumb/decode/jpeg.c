#include "thumb/decode/format.h"

#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <jpeglib.h>

#include "thumb/scale.h"

struct jpeg_guard {
	struct jpeg_error_mgr base;
	jmp_buf jmp;
};

static void jpeg_on_error(j_common_ptr cinfo) {
	struct jpeg_guard *guard = (struct jpeg_guard *)cinfo->err;
	longjmp(guard->jmp, 1);
}

static void jpeg_on_message(j_common_ptr cinfo) {
	(void)cinfo;
}

static void jpeg_scale_for_target(struct jpeg_decompress_struct *cinfo,
	uint32_t target_w, uint32_t target_h) {
	static const unsigned int denominators[] = {8, 4, 2};
	if (target_w == 0 || target_h == 0) {
		return;
	}

	// Keep enough decoded pixels for the final cover crop
	unsigned int denominator = 1;
	for (size_t i = 0; i < sizeof(denominators) / sizeof(denominators[0]);
		i++) {
		cinfo->scale_num = 1;
		cinfo->scale_denom = denominators[i];
		jpeg_calc_output_dimensions(cinfo);
		if (cinfo->output_width >= target_w &&
			cinfo->output_height >= target_h) {
			denominator = denominators[i];
			break;
		}
	}
	cinfo->scale_num = 1;
	cinfo->scale_denom = denominator;
	jpeg_calc_output_dimensions(cinfo);
	if (cinfo->output_width == target_w &&
		cinfo->output_height == target_h) {
		return;
	}

	// M/8 lacks SIMD, so it only wins when it also removes the scale pass
	for (unsigned int num = 7; num > 0; num--) {
		cinfo->scale_num = num;
		cinfo->scale_denom = 8;
		jpeg_calc_output_dimensions(cinfo);
		if (cinfo->output_width == target_w &&
			cinfo->output_height == target_h) {
			return;
		}
	}
	cinfo->scale_num = 1;
	cinfo->scale_denom = denominator;
}

// multi-scan files keep every source coefficient, whatever the DCT scale
static uint64_t jpeg_coefficient_bytes(struct jpeg_decompress_struct *cinfo) {
	if (!jpeg_has_multiple_scans(cinfo)) {
		return 0;
	}
	uint64_t bytes = 0;
	for (int ci = 0; ci < cinfo->num_components; ci++) {
		const jpeg_component_info *comp = &cinfo->comp_info[ci];
		uint64_t h = (uint64_t)comp->h_samp_factor;
		uint64_t v = (uint64_t)comp->v_samp_factor;
		uint64_t columns = (comp->width_in_blocks + h - 1) / h * h;
		uint64_t rows = (comp->height_in_blocks + v - 1) / v * v;
		bytes += columns * rows * DCTSIZE2 * sizeof(JCOEF);
	}
	return bytes;
}

// --- progressive input ---

static bool idct_is_1x1(const jpeg_component_info *comp) {
#if JPEG_LIB_VERSION >= 70
	return comp->DCT_h_scaled_size == 1 && comp->DCT_v_scaled_size == 1;
#else
	return comp->DCT_scaled_size == 1;
#endif
}

static bool dc_only_output(const struct jpeg_decompress_struct *cinfo) {
	for (int ci = 0; ci < cinfo->num_components; ci++) {
		if (!idct_is_1x1(&cinfo->comp_info[ci])) {
			return false;
		}
	}
	return true;
}

static bool dc_exact(const struct jpeg_decompress_struct *cinfo) {
	if (cinfo->coef_bits == NULL) {
		return false;
	}
	for (int ci = 0; ci < cinfo->num_components; ci++) {
		if (cinfo->coef_bits[ci][0] != 0) {
			return false;
		}
	}
	return true;
}

static bool consume_scans(
	struct jpeg_decompress_struct *cinfo, const atomic_bool *stop) {
	bool dc_only = dc_only_output(cinfo);
	for (;;) {
		if (stop_requested(stop)) {
			return false;
		}
		int status = jpeg_consume_input(cinfo);
		if (status == JPEG_REACHED_EOI) {
			break;
		}
		// a stdio source never suspends, so this is only a loop guard
		if (status == JPEG_SUSPENDED) {
			return false;
		}
		// only a completed scan makes every started scan's data present
		if (status == JPEG_SCAN_COMPLETED && dc_only &&
			dc_exact(cinfo)) {
			// smoothing would guess the missing AC and rewrite DC
			cinfo->do_block_smoothing = FALSE;
			break;
		}
	}
	return jpeg_start_output(cinfo, cinfo->input_scan_number);
}

// --- output ---

// heap state, so the error longjmp can still free it
struct jpeg_stream {
	uint8_t *row;
	struct matuwall_row_scaler scaler;
};

static void jpeg_stream_free(struct jpeg_stream *stream) {
	free(stream->row);
	matuwall_row_scaler_finish(&stream->scaler);
	free(stream);
}

static bool read_jpeg_direct(struct jpeg_decompress_struct *cinfo,
	struct matuwall_image *img, const atomic_bool *stop) {
	while (cinfo->output_scanline < img->height) {
		JSAMPROW row =
			(JSAMPROW)(img->pixels +
				   (size_t)cinfo->output_scanline * img->width);
		if (stop_requested(stop) ||
			jpeg_read_scanlines(cinfo, &row, 1) != 1) {
			return false;
		}
	}
	// buffered input already ended, or stopped early on purpose, finishing
	// would read and decode every remaining scan
	if (!cinfo->buffered_image) {
		jpeg_finish_decompress(cinfo);
	}
	return true;
}

// rows below the cover crop are never decoded
static bool read_jpeg_streamed(struct jpeg_decompress_struct *cinfo,
	struct matuwall_image *img, struct jpeg_stream *stream,
	const atomic_bool *stop) {
	stream->row = malloc((size_t)cinfo->output_width * 3);
	if (stream->row == NULL ||
		!matuwall_row_scaler_init(&stream->scaler, cinfo->output_width,
			cinfo->output_height, img->width, img->height,
			img->pixels)) {
		return false;
	}
	while (!matuwall_row_scaler_done(&stream->scaler)) {
		uint32_t y = cinfo->output_scanline;
		JSAMPROW row = stream->row;
		if (stop_requested(stop) || y >= cinfo->output_height ||
			jpeg_read_scanlines(cinfo, &row, 1) != 1) {
			return false;
		}
		matuwall_row_scaler_push(&stream->scaler, y, stream->row);
	}
	return true;
}

bool matuwall_decode_jpeg(
	FILE *fp, struct matuwall_image *img, const struct decode_job *job) {
	struct jpeg_stream *stream = calloc(1, sizeof(*stream));
	if (stream == NULL) {
		return false;
	}
	struct jpeg_decompress_struct cinfo = {0};
	struct jpeg_guard guard;
	cinfo.err = jpeg_std_error(&guard.base);
	guard.base.error_exit = jpeg_on_error;
	guard.base.output_message = jpeg_on_message;

	if (setjmp(guard.jmp)) {
		jpeg_destroy_decompress(&cinfo);
		jpeg_stream_free(stream);
		matuwall_image_free(img);
		return false;
	}

	jpeg_create_decompress(&cinfo);
	// backstop for the estimate below: libjpeg fails instead of growing
	cinfo.mem->max_memory_to_use = (long)DECODE_MEMORY_LIMIT;
	jpeg_stdio_src(&cinfo, fp);
	jpeg_read_header(&cinfo, TRUE);

	bool ok = matuwall_image_dimensions_ok(
		cinfo.image_width, cinfo.image_height);
	if (ok) {
		jpeg_scale_for_target(&cinfo, job->target_w, job->target_h);
	}
	bool direct = cinfo.output_width == job->target_w &&
		      cinfo.output_height == job->target_h;
	uint64_t peak = jpeg_coefficient_bytes(&cinfo) + target_bytes(job);
	if (!direct) {
		peak += (uint64_t)cinfo.output_width * 3 +
			matuwall_row_scaler_bytes(job->target_w);
	}
	ok = ok && reserve(job, peak);
	if (ok) {
		cinfo.out_color_space = direct ? JCS_EXT_BGRA : JCS_RGB;
		// normal mode absorbs all scans in one uncancellable call
		// buffering baseline would add a whole-image coefficient array
		cinfo.buffered_image = jpeg_has_multiple_scans(&cinfo);
		jpeg_start_decompress(&cinfo);
		ok = !cinfo.buffered_image || consume_scans(&cinfo, job->stop);
	}
	if (ok) {
		img->width = job->target_w;
		img->height = job->target_h;
		img->pixels = alloc_target(job);
		ok = img->pixels != NULL &&
		     (direct ? read_jpeg_direct(&cinfo, img, job->stop)
			     : read_jpeg_streamed(
				       &cinfo, img, stream, job->stop));
	}

	jpeg_destroy_decompress(&cinfo);
	jpeg_stream_free(stream);
	if (!ok) {
		matuwall_image_free(img);
	}
	return ok;
}
