#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

#define PL_MPEG_IMPLEMENTATION
#include "third_party/pl_mpeg/pl_mpeg.h"
#include "movie_native.h"

#define MOVIE_RING 4
#define MOVIE_CHUNK 65536

struct imbk_movie {
    FILE *file;
    unsigned long long offset, size, remaining;
    unsigned char input[MOVIE_CHUNK], packet[65535];
    size_t input_pos, input_size;
    plm_video_t *video;
    int precision, tag_match, tag_digits, tag_value;
    int width, height, cw, ch;
    double fps;
    size_t frame_bytes;
    unsigned char *frames;
    int indices[MOVIE_RING], head, count;
    int next_index, ended, restart, stop;
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE changed;
    HANDLE worker;
};

/* The extension is carried in sequence user data, before the first picture.
   Track it across PES boundaries without retaining the elementary stream. */
static void scan_precision(imbk_movie *m, const unsigned char *data, size_t n) {
    static const unsigned char tag[] = "IDCPREC";
    size_t i;
    for (i = 0; i < n; ++i) {
        int c = data[i];
        if (m->tag_match == sizeof(tag)) {
            if (c < '0' || c > '9') {
                m->tag_match = m->tag_digits = m->tag_value = 0;
                continue;
            }
            if (m->tag_digits >= 8) m->tag_value = m->tag_value * 10 + c - '0';
            if (++m->tag_digits == 16) {
                if (m->tag_value <= 3) m->precision = m->tag_value;
                m->tag_match = m->tag_digits = m->tag_value = 0;
            }
        }
        else if (c == tag[m->tag_match]) ++m->tag_match;
        else m->tag_match = c == tag[0] ? 1 : 0;
    }
}

/* All source reads, including header scanning, stay inside the archive entry. */
static int source_byte(imbk_movie *m) {
    if (m->input_pos == m->input_size) {
        size_t n = m->remaining > MOVIE_CHUNK ? MOVIE_CHUNK : (size_t)m->remaining;
        m->input_size = fread(m->input, 1, n, m->file);
        m->input_pos = 0;
        m->remaining -= m->input_size;
        if (!m->input_size) return -1;
    }
    return m->input[m->input_pos++];
}

static size_t source_read(imbk_movie *m, unsigned char *out, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) {
        int c = source_byte(m);
        if (c < 0) break;
        if (out) out[i] = (unsigned char)c;
    }
    return i;
}

/* CRI SofdecStream/CRITAGS, padding, and audio packets are length-delimited.
   Never scan their contents for video start codes. MPEG-1 PES headers include
   stuffing, an optional two-byte STD field, and PTS/DTS or the 0x0f marker. */
static void load_video(plm_buffer_t *buffer, void *user) {
    imbk_movie *m = (imbk_movie *)user;
    unsigned int prefix = 0xffffff;
    for (;;) {
        int c = source_byte(m), id, hi, lo;
        size_t n, got, p;
        if (c < 0) break;
        prefix = ((prefix << 8) | (unsigned int)c) & 0xffffff;
        if (prefix != 1) continue;
        id = source_byte(m);
        prefix = 0xffffff;
        if (id < 0 || id == 0xb9) break;
        if (id == 0xba) {
            c = source_byte(m);
            if (c < 0) break;
            if ((c & 0xf0) == 0x20) {
                if (source_read(m, NULL, 7) != 7) break;
            }
            else if ((c & 0xc0) == 0x40) {
                unsigned char pack[9];
                if (source_read(m, pack, 9) != 9 ||
                    source_read(m, NULL, pack[8] & 7) != (size_t)(pack[8] & 7)) break;
            }
            else break;
            continue;
        }
        if (id < 0xbb) continue;
        hi = source_byte(m); lo = source_byte(m);
        if (hi < 0 || lo < 0) break;
        n = (size_t)((hi << 8) | lo);
        if (id != 0xe0) {
            if (source_read(m, NULL, n) != n) break;
            continue;
        }
        /* Zero-length (unbounded) PES is not used by these MPEG-PS movies. */
        if (!n) break;
        got = source_read(m, m->packet, n);
        p = 0;
        if (got && (m->packet[0] & 0xc0) == 0x80) {
            if (got < 3) break;
            p = 3 + m->packet[2]; /* MPEG-2 PES, also accepted */
        }
        else {
            while (p < got && m->packet[p] == 0xff) ++p;
            if (p < got && (m->packet[p] & 0xc0) == 0x40) p += 2;
            if (p >= got) break;
            c = m->packet[p];
            if ((c & 0xf0) == 0x20) p += 5;
            else if ((c & 0xf0) == 0x30) p += 10;
            else if (c == 0x0f) ++p;
            else break;
        }
        if (p > got) break;
        if (got > p) {
            scan_precision(m, m->packet + p, got - p);
            plm_buffer_write(buffer, m->packet + p, got - p);
            /* Preserve available video even when the final PES is truncated. */
            if (got != n) plm_buffer_signal_end(buffer);
            return;
        }
        if (got != n) break;
    }
    plm_buffer_signal_end(buffer);
}

static int reset_video(imbk_movie *m) {
    plm_buffer_t *buffer;
    if (m->video) plm_video_destroy(m->video);
    m->video = NULL;
    if (_fseeki64(m->file, (__int64)m->offset, SEEK_SET)) return 0;
    clearerr(m->file);
    m->remaining = m->size;
    m->input_pos = m->input_size = 0;
    m->precision = m->tag_match = m->tag_digits = m->tag_value = 0;
    buffer = plm_buffer_create_with_capacity(MOVIE_CHUNK);
    if (!buffer) return 0;
    plm_buffer_set_load_callback(buffer, load_video, m);
    m->video = plm_video_create_with_buffer(buffer, 1);
    if (!m->video) { plm_buffer_destroy(buffer); return 0; }
    return plm_video_has_header(m->video);
}

static void copy_plane(unsigned char *dst, size_t pitch,
                       const unsigned char *src, size_t stride, int w, int h) {
    int row;
    for (row = 0; row < h; ++row) {
        memcpy(dst, src, (size_t)w);
        dst += pitch; src += stride;
    }
}

/* TMPGEnc MPEG-1 IDCPREC extension: upstream pl_mpeg assumes 8-bit intra
   DC precision. These five routines are adapted from the included MIT-licensed
   pl_mpeg.h (Dominic Szablewski), with only call routing, predictor resets and
   the DC shift changed, plus full DC-size VLCs and final-reference flushing.
   Keep this local so the vendor header remains intact.
   AC coefficients, motion compensation, IDCT and display ordering stay in PLM. */
static plm_frame_t *movie_plm_video_decode(imbk_movie *m);
static void movie_plm_video_decode_picture(plm_video_t *self, int precision);
static void movie_plm_video_decode_slice(plm_video_t *self, int slice, int precision);
static void movie_plm_video_decode_macroblock(plm_video_t *self, int precision);
static void movie_plm_video_decode_block(plm_video_t *self, int block, int precision);

/* Full DC-size VLCs (up to 11 bits). Upstream's MPEG-1 tables stop at 8. */
static int movie_dc_size(plm_buffer_t *buffer, int chroma) {
    int code = plm_buffer_read(buffer, 2), size;
    if (chroma) {
        if (code < 3) return code;
        size = 3;
    }
    else {
        if (code < 2) return code + 1;
        if (code == 2) return plm_buffer_read(buffer, 1) ? 3 : 0;
        size = 4;
    }
    while (size < 11 && plm_buffer_read(buffer, 1)) ++size;
    return size;
}

static plm_frame_t *movie_plm_video_decode(imbk_movie *m) {
    plm_video_t *self = m->video;
	if (!plm_video_has_header(self)) {
		return NULL;
	}
	
	plm_frame_t *frame = NULL;
	do {
		if (self->start_code != PLM_START_PICTURE) {
			self->start_code = plm_buffer_find_start_code(self->buffer, PLM_START_PICTURE);
			
			if (self->start_code == -1) {
				/* Flush the pending reference even if B pictures came after it
                   in decode order. Upstream drops this final display frame. */
				if (
					self->has_reference_frame &&
					!self->assume_no_b_frames &&
					plm_buffer_has_ended(self->buffer)
				) {
					self->has_reference_frame = FALSE;
					frame = &self->frame_backward;
					break;
				}

				return NULL;
			}
		}

		// Make sure we have a full picture in the buffer before attempting to
		// decode it. Sadly, this can only be done by seeking for the start code
		// of the next picture. Also, if we didn't find the start code for the
		// next picture, but the source has ended, we assume that this last
		// picture is in the buffer.
		if (
			plm_buffer_has_start_code(self->buffer, PLM_START_PICTURE) == -1 &&
			!plm_buffer_has_ended(self->buffer)
		) {
			return NULL;
		}
		plm_buffer_discard_read_bytes(self->buffer);
		
		/* Lookahead above also loads sequence user data before this picture. */
		movie_plm_video_decode_picture(self, m->precision);

		if (self->assume_no_b_frames) {
			frame = &self->frame_backward;
		}
		else if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_B) {
			frame = &self->frame_current;
		}
		else if (self->has_reference_frame) {
			frame = &self->frame_forward;
		}
		else {
			self->has_reference_frame = TRUE;
		}
	} while (!frame);
	
	frame->time = self->time;
	self->frames_decoded++;
	self->time = (double)self->frames_decoded / self->framerate;
	
	return frame;
}

static void movie_plm_video_decode_picture(plm_video_t *self, int precision) {
	plm_buffer_skip(self->buffer, 10); // skip temporalReference
	self->picture_type = plm_buffer_read(self->buffer, 3);
	plm_buffer_skip(self->buffer, 16); // skip vbv_delay

	// D frames or unknown coding type
	if (self->picture_type <= 0 || self->picture_type > PLM_VIDEO_PICTURE_TYPE_B) {
		return;
	}

	// Forward full_px, f_code
	if (
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE ||
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_B
	) {
		self->motion_forward.full_px = plm_buffer_read(self->buffer, 1);
		int f_code = plm_buffer_read(self->buffer, 3);
		if (f_code == 0) {
			// Ignore picture with zero f_code
			return;
		}
		self->motion_forward.r_size = f_code - 1;
	}

	// Backward full_px, f_code
	if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_B) {
		self->motion_backward.full_px = plm_buffer_read(self->buffer, 1);
		int f_code = plm_buffer_read(self->buffer, 3);
		if (f_code == 0) {
			// Ignore picture with zero f_code
			return;
		}
		self->motion_backward.r_size = f_code - 1;
	}

	plm_frame_t frame_temp = self->frame_forward;
	if (
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_INTRA ||
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE
	) {
		self->frame_forward = self->frame_backward;
	}


	// Find first slice start code; skip extension and user data
	do {
		self->start_code = plm_buffer_next_start_code(self->buffer);
	} while (
		self->start_code == PLM_START_EXTENSION || 
		self->start_code == PLM_START_USER_DATA
	);

	// Decode all slices
	while (PLM_START_IS_SLICE(self->start_code)) {
		movie_plm_video_decode_slice(self, self->start_code & 0x000000FF, precision);
		if (self->macroblock_address >= self->mb_size - 1) {
			break;
		}
		self->start_code = plm_buffer_next_start_code(self->buffer);
	}

	// If this is a reference picture rotate the prediction pointers
	if (
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_INTRA ||
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE
	) {
		self->frame_backward = self->frame_current;
		self->frame_current = frame_temp;
	}
}

static void movie_plm_video_decode_slice(plm_video_t *self, int slice, int precision) {
	self->slice_begin = TRUE;
	self->macroblock_address = (slice - 1) * self->mb_width - 1;

	// Reset motion vectors and DC predictors
	self->motion_backward.h = self->motion_forward.h = 0;
	self->motion_backward.v = self->motion_forward.v = 0;
	self->dc_predictor[0] = 128 << precision;
	self->dc_predictor[1] = 128 << precision;
	self->dc_predictor[2] = 128 << precision;

	self->quantizer_scale = plm_buffer_read(self->buffer, 5);

	// Skip extra
	while (plm_buffer_read(self->buffer, 1)) {
		plm_buffer_skip(self->buffer, 8);
	}

	do {
		movie_plm_video_decode_macroblock(self, precision);
	} while (
		self->macroblock_address < self->mb_size - 1 &&
		plm_buffer_peek_non_zero(self->buffer, 23)
	);
}

static void movie_plm_video_decode_macroblock(plm_video_t *self, int precision) {
	// Decode increment
	int increment = 0;
	int t = plm_buffer_read_vlc(self->buffer, PLM_VIDEO_MACROBLOCK_ADDRESS_INCREMENT);

	while (t == 34) {
		// macroblock_stuffing
		t = plm_buffer_read_vlc(self->buffer, PLM_VIDEO_MACROBLOCK_ADDRESS_INCREMENT);
	}
	while (t == 35) {
		// macroblock_escape
		increment += 33;
		t = plm_buffer_read_vlc(self->buffer, PLM_VIDEO_MACROBLOCK_ADDRESS_INCREMENT);
	}
	increment += t;

	// Process any skipped macroblocks
	if (self->slice_begin) {
		// The first increment of each slice is relative to beginning of the
		// previous row, not the previous macroblock
		self->slice_begin = FALSE;
		self->macroblock_address += increment;
	}
	else {
		if (self->macroblock_address + increment >= self->mb_size) {
			return; // invalid
		}
		if (increment > 1) {
			// Skipped macroblocks reset DC predictors
			self->dc_predictor[0] = 128 << precision;
			self->dc_predictor[1] = 128 << precision;
			self->dc_predictor[2] = 128 << precision;

			// Skipped macroblocks in P-pictures reset motion vectors
			if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE) {
				self->motion_forward.h = 0;
				self->motion_forward.v = 0;
			}
		}

		// Predict skipped macroblocks
		while (increment > 1) {
			self->macroblock_address++;
			self->mb_row = self->macroblock_address / self->mb_width;
			self->mb_col = self->macroblock_address % self->mb_width;

			plm_video_predict_macroblock(self);
			increment--;
		}
		self->macroblock_address++;
	}

	self->mb_row = self->macroblock_address / self->mb_width;
	self->mb_col = self->macroblock_address % self->mb_width;

	if (self->mb_col >= self->mb_width || self->mb_row >= self->mb_height) {
		return; // corrupt stream;
	}

	// Process the current macroblock
	const plm_vlc_t *table = PLM_VIDEO_MACROBLOCK_TYPE[self->picture_type];
	self->macroblock_type = plm_buffer_read_vlc(self->buffer, table);

	self->macroblock_intra = (self->macroblock_type & 0x01);
	self->motion_forward.is_set = (self->macroblock_type & 0x08);
	self->motion_backward.is_set = (self->macroblock_type & 0x04);

	// Quantizer scale
	if ((self->macroblock_type & 0x10) != 0) {
		self->quantizer_scale = plm_buffer_read(self->buffer, 5);
	}

	if (self->macroblock_intra) {
		// Intra-coded macroblocks reset motion vectors
		self->motion_backward.h = self->motion_forward.h = 0;
		self->motion_backward.v = self->motion_forward.v = 0;
	}
	else {
		// Non-intra macroblocks reset DC predictors
		self->dc_predictor[0] = 128 << precision;
		self->dc_predictor[1] = 128 << precision;
		self->dc_predictor[2] = 128 << precision;

		plm_video_decode_motion_vectors(self);
		plm_video_predict_macroblock(self);
	}

	// Decode blocks
	int cbp = ((self->macroblock_type & 0x02) != 0)
		? plm_buffer_read_vlc(self->buffer, PLM_VIDEO_CODE_BLOCK_PATTERN)
		: (self->macroblock_intra ? 0x3f : 0);

	for (int block = 0, mask = 0x20; block < 6; block++) {
		if ((cbp & mask) != 0) {
			movie_plm_video_decode_block(self, block, precision);
		}
		mask >>= 1;
	}
}

static void movie_plm_video_decode_block(plm_video_t *self, int block, int precision) {

	int n = 0;
	uint8_t *quant_matrix;

	// Decode DC coefficient of intra-coded blocks
	if (self->macroblock_intra) {
		int predictor;
		int dct_size;

		// DC prediction
		int plane_index = block > 3 ? block - 3 : 0;
		predictor = self->dc_predictor[plane_index];
		dct_size = movie_dc_size(self->buffer, plane_index != 0);

		// Read DC coeff
		if (dct_size > 0) {
			int differential = plm_buffer_read(self->buffer, dct_size);
			if ((differential & (1 << (dct_size - 1))) != 0) {
				self->block_data[0] = predictor + differential;
			}
			else {
				self->block_data[0] = predictor + (-(1 << dct_size) | (differential + 1));
			}
		}
		else {
			self->block_data[0] = predictor;
		}

		// Save predictor value
		self->dc_predictor[plane_index] = self->block_data[0];

		// Dequantize + premultiply
		self->block_data[0] <<= (3 + 5 - precision);

		quant_matrix = self->intra_quant_matrix;
		n = 1;
	}
	else {
		quant_matrix = self->non_intra_quant_matrix;
	}

	// Decode AC coefficients (+DC for non-intra)
	int level = 0;
	while (TRUE) {
		int run = 0;
		uint16_t coeff = plm_buffer_read_vlc_uint(self->buffer, PLM_VIDEO_DCT_COEFF);

		if ((coeff == 0x0001) && (n > 0) && (plm_buffer_read(self->buffer, 1) == 0)) {
			// end_of_block
			break;
		}
		if (coeff == 0xffff) {
			// escape
			run = plm_buffer_read(self->buffer, 6);
			level = plm_buffer_read(self->buffer, 8);
			if (level == 0) {
				level = plm_buffer_read(self->buffer, 8);
			}
			else if (level == 128) {
				level = plm_buffer_read(self->buffer, 8) - 256;
			}
			else if (level > 128) {
				level = level - 256;
			}
		}
		else {
			run = coeff >> 8;
			level = coeff & 0xff;
			if (plm_buffer_read(self->buffer, 1)) {
				level = -level;
			}
		}

		n += run;
		if (n < 0 || n >= 64) {
			return; // invalid
		}

		int de_zig_zagged = PLM_VIDEO_ZIG_ZAG[n];
		n++;

		// Dequantize, oddify, clip
		level = (unsigned)level << 1;
		if (!self->macroblock_intra) {
			level += (level < 0 ? -1 : 1);
		}
		level = (level * self->quantizer_scale * quant_matrix[de_zig_zagged]) >> 4;
		if ((level & 1) == 0) {
			level -= level > 0 ? 1 : -1;
		}
		if (level > 2047) {
			level = 2047;
		}
		else if (level < -2048) {
			level = -2048;
		}

		// Save premultiplied coefficient
		self->block_data[de_zig_zagged] = level * PLM_VIDEO_PREMULTIPLIER_MATRIX[de_zig_zagged];
	}

	// Move block to its place
	uint8_t *d;
	int dw;
	int di;

	if (block < 4) {
		d = self->frame_current.y.data;
		dw = self->luma_width;
		di = (self->mb_row * self->luma_width + self->mb_col) << 4;
		if ((block & 1) != 0) {
			di += 8;
		}
		if ((block & 2) != 0) {
			di += self->luma_width << 3;
		}
	}
	else {
		d = (block == 4) ? self->frame_current.cb.data : self->frame_current.cr.data;
		dw = self->chroma_width;
		di = ((self->mb_row * self->luma_width) << 2) + (self->mb_col << 3);
	}

	int *s = self->block_data;
	int si = 0;
	if (self->macroblock_intra) {
		// Overwrite (no prediction)
		if (n == 1) {
			int clamped = plm_clamp((s[0] + 128) >> 8);
			PLM_BLOCK_SET(d, di, dw, si, 8, 8, clamped);
			s[0] = 0;
		}
		else {
			plm_video_idct(s);
			PLM_BLOCK_SET(d, di, dw, si, 8, 8, plm_clamp(s[si]));
			memset(self->block_data, 0, sizeof(self->block_data));
		}
	}
	else {
		// Add data to the predicted macroblock
		if (n == 1) {
			int value = (s[0] + 128) >> 8;
			PLM_BLOCK_SET(d, di, dw, si, 8, 8, plm_clamp(d[di] + value));
			s[0] = 0;
		}
		else {
			plm_video_idct(s);
			PLM_BLOCK_SET(d, di, dw, si, 8, 8, plm_clamp(d[di] + s[si]));
			memset(self->block_data, 0, sizeof(self->block_data));
		}
	}
}

static DWORD WINAPI decode_worker(void *arg) {
    imbk_movie *m = (imbk_movie *)arg;
    EnterCriticalSection(&m->lock);
    for (;;) {
        plm_frame_t *frame;
        int slot, ok;
        unsigned char *dst;
        if (m->stop) break;
        if (m->restart) {
            LeaveCriticalSection(&m->lock);
            ok = reset_video(m);
            EnterCriticalSection(&m->lock);
            m->head = m->count = m->next_index = 0;
            m->ended = !ok;
            m->restart = 0;
            WakeAllConditionVariable(&m->changed);
            continue;
        }
        if (m->count == MOVIE_RING || m->ended) {
            SleepConditionVariableCS(&m->changed, &m->lock, INFINITE);
            continue;
        }
        slot = (m->head + m->count) % MOVIE_RING;
        LeaveCriticalSection(&m->lock);
        frame = movie_plm_video_decode(m);
        if (frame) {
            dst = m->frames + (size_t)slot * m->frame_bytes;
            copy_plane(dst, m->width, frame->y.data, frame->y.width, m->width, m->height);
            dst += (size_t)m->width * m->height;
            copy_plane(dst, m->cw, frame->cb.data, frame->cb.width, m->cw, m->ch);
            dst += (size_t)m->cw * m->ch;
            copy_plane(dst, m->cw, frame->cr.data, frame->cr.width, m->cw, m->ch);
        }
        EnterCriticalSection(&m->lock);
        if (m->restart || m->stop) continue;
        if (!frame) m->ended = 1;
        else {
            m->indices[slot] = m->next_index++;
            ++m->count;
        }
        WakeAllConditionVariable(&m->changed);
    }
    LeaveCriticalSection(&m->lock);
    return 0;
}

imbk_movie *imbk_movie_open(const char *path, unsigned long long offset,
                          unsigned long long size) {
    imbk_movie *m;
    __int64 length;
    if (!path || !size || offset > LLONG_MAX || size > LLONG_MAX - offset) return NULL;
    m = (imbk_movie *)calloc(1, sizeof(*m));
    if (!m) return NULL;
    InitializeCriticalSection(&m->lock);
    InitializeConditionVariable(&m->changed);
    m->offset = offset; m->size = size;
    m->file = fopen(path, "rb");
    if (!m->file || _fseeki64(m->file, 0, SEEK_END)) goto fail;
    length = _ftelli64(m->file);
    if (length < 0 || offset + size > (unsigned long long)length || !reset_video(m)) goto fail;
    m->width = plm_video_get_width(m->video);
    m->height = plm_video_get_height(m->video);
    m->fps = plm_video_get_framerate(m->video);
    if (m->width <= 0 || m->height <= 0 || m->fps <= 0) goto fail;
    m->cw = (m->width + 1) / 2; m->ch = (m->height + 1) / 2;
    m->frame_bytes = (size_t)m->width * m->height + 2 * (size_t)m->cw * m->ch;
    m->frames = (unsigned char *)malloc(MOVIE_RING * m->frame_bytes);
    if (!m->frames) goto fail;
    m->worker = CreateThread(NULL, 0, decode_worker, m, 0, NULL);
    if (!m->worker) goto fail;
    return m;
fail:
    imbk_movie_close(m);
    return NULL;
}

void imbk_movie_close(imbk_movie *m) {
    if (!m) return;
    if (m->worker) {
        EnterCriticalSection(&m->lock);
        m->stop = 1;
        WakeAllConditionVariable(&m->changed);
        LeaveCriticalSection(&m->lock);
        WaitForSingleObject(m->worker, INFINITE);
        CloseHandle(m->worker);
    }
    if (m->video) plm_video_destroy(m->video);
    if (m->file) fclose(m->file);
    free(m->frames);
    DeleteCriticalSection(&m->lock);
    free(m);
}

int imbk_movie_width(imbk_movie *m) { return m ? m->width : 0; }
int imbk_movie_height(imbk_movie *m) { return m ? m->height : 0; }
double imbk_movie_fps(imbk_movie *m) { return m ? m->fps : 0; }

int imbk_movie_frame(imbk_movie *m, int index,
                     unsigned char *y, int y_pitch,
                     unsigned char *u, int u_pitch,
                     unsigned char *v, int v_pitch) {
    const unsigned char *src;
    if (!m || index < 0 || !y || !u || !v ||
        y_pitch < m->width || u_pitch < m->cw || v_pitch < m->cw) return 0;
    EnterCriticalSection(&m->lock);
    if ((m->count && index < m->indices[m->head]) ||
        (!m->count && index < m->next_index)) {
        m->restart = 1;
        WakeAllConditionVariable(&m->changed);
        while (m->restart) SleepConditionVariableCS(&m->changed, &m->lock, INFINITE);
    }
    for (;;) {
        while (m->count && m->indices[m->head] < index) {
            m->head = (m->head + 1) % MOVIE_RING;
            --m->count;
            WakeAllConditionVariable(&m->changed);
        }
        if (m->count) break;
        if (m->ended) { LeaveCriticalSection(&m->lock); return 0; }
        SleepConditionVariableCS(&m->changed, &m->lock, INFINITE);
    }
    src = m->frames + (size_t)m->head * m->frame_bytes;
    copy_plane(y, (size_t)y_pitch, src, m->width, m->width, m->height);
    src += (size_t)m->width * m->height;
    copy_plane(u, (size_t)u_pitch, src, m->cw, m->cw, m->ch);
    src += (size_t)m->cw * m->ch;
    copy_plane(v, (size_t)v_pitch, src, m->cw, m->cw, m->ch);
    LeaveCriticalSection(&m->lock);
    return 1;
}
