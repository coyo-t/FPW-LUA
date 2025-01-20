#include "./stb_image.hpp"

#include <cmath>


#include <cstdlib>
#include <cstring>
#include <climits>


#ifndef STBI_ASSERT
#include <cassert>
#define STBI_ASSERT(x) assert(x)
#endif

#include "./zlib.hpp"


#include <cstdint>
static_assert(sizeof(uint32_t) == 4);


#define STBI_NOTUSED(v)  (void)sizeof(v)


#define stbi_lrot(x,y)  (((x) << (y)) | ((x) >> (-(y) & 31)))


#ifndef STBI_MALLOC
#define STBI_MALLOC(sz)           malloc(sz)
#define STBI_REALLOC(p,newsz)     realloc(p,newsz)
#define STBI_FREE(p)              free(p)
#endif

#ifndef STBI_REALLOC_SIZED
#define STBI_REALLOC_SIZED(p,oldsz,newsz) STBI_REALLOC(p,newsz)
#endif

static constexpr auto STBI_MAX_DIMENSIONS = 1 << 24;


///////////////////////////////////////////////
//
//  stbi__context struct and start_xxx functions

// stbi__context structure is our basic context used by all images, so it
// contains all the IO context, plus some basic image information
struct stbi__context
{
	uint32_t img_x, img_y;
	int img_n, img_out_n;

	uint8_t *img_buffer, *img_buffer_end;
	uint8_t *img_buffer_original, *img_buffer_original_end;

	auto get8() -> uint8_t
	{
		if (this->img_buffer < this->img_buffer_end)
			return *this->img_buffer++;
		return 0;
	}

	auto get16be() -> int
	{
		int z = this->get8();
		return (z << 8) + this->get8();
	}

	auto get32be() -> uint32_t
	{
		uint32_t z = this->get16be();
		return (z << 16) + this->get16be();
	}

	auto skip(int n) -> void
	{
		if (n == 0)
		{
			return; // already there!
		}
		if (n < 0)
		{
			this->img_buffer = this->img_buffer_end;
			return;
		}
		this->img_buffer += n;
	}

	// initialize a memory-decode context
	auto start_mem(uint8_t const *buffer, size_t len) -> void
	{
		this->img_buffer = this->img_buffer_original = const_cast<uint8_t *>(buffer);
		this->img_buffer_end = this->img_buffer_original_end = const_cast<uint8_t *>(buffer) + len;
	}
};


static void stbi__rewind(stbi__context *s)
{
	// conceptually rewind SHOULD rewind to the beginning of the stream,
	// but we just rewind to the beginning of the initial buffer, because
	// we only use it after doing 'test', which only ever looks at at most 92 bytes
	s->img_buffer = s->img_buffer_original;
	s->img_buffer_end = s->img_buffer_original_end;
}

struct stbi__result_info
{
	int bits_per_channel;
	int num_channels;
	int channel_order;
};

static void *stbi__png_load(
	stbi__context *s, size_t *x, size_t *y, size_t *comp, size_t req_comp, stbi__result_info *ri);

static const char *stbi__g_failure_reason;

const char *coyote_stbi_failure_reason()
{
	return stbi__g_failure_reason;
}

#ifndef STBI_NO_FAILURE_STRINGS
static int stbi__err(const char *str)
{
	stbi__g_failure_reason = str;
	return 0;
}
#endif

static void *stbi__malloc(size_t size)
{
	return STBI_MALLOC(size);
}

// stb_image uses ints pervasively, including for offset calculations.
// therefore the largest decoded image size we can support with the
// current code, even on 64-bit targets, is INT_MAX. this is not a
// significant limitation for the intended use case.
//
// we do, however, need to make sure our size calculations don't
// overflow. hence a few helper functions for size calculations that
// multiply integers together, making sure that they're non-negative
// and no overflow occurs.

// return 1 if the sum is valid, 0 on overflow.
// negative terms are considered invalid.
static int stbi__addsizes_valid(int a, int b)
{
	if (b < 0) return 0;
	// now 0 <= b <= INT_MAX, hence also
	// 0 <= INT_MAX - b <= INTMAX.
	// And "a + b <= INT_MAX" (which might overflow) is the
	// same as a <= INT_MAX - b (no overflow)
	return a <= INT_MAX - b;
}

// returns 1 if the product is valid, 0 on overflow.
// negative factors are considered invalid.
static int stbi__mul2sizes_valid(int a, int b)
{
	if (a < 0 || b < 0) return 0;
	if (b == 0) return 1; // mul-by-0 is always safe
	// portable way to check for no overflows in a*b
	return a <= INT_MAX / b;
}

// returns 1 if "a*b + add" has no negative terms/factors and doesn't overflow
static int stbi__mad2sizes_valid(int a, int b, int add)
{
	return stbi__mul2sizes_valid(a, b) && stbi__addsizes_valid(a * b, add);
}

// returns 1 if "a*b*c + add" has no negative terms/factors and doesn't overflow
static int stbi__mad3sizes_valid(int a, int b, int c, int add)
{
	return stbi__mul2sizes_valid(a, b) && stbi__mul2sizes_valid(a * b, c) &&
	       stbi__addsizes_valid(a * b * c, add);
}

// mallocs with size overflow checking
static void *stbi__malloc_mad2(int a, int b, int add)
{
	if (!stbi__mad2sizes_valid(a, b, add)) return NULL;
	return stbi__malloc(a * b + add);
}

static void *stbi__malloc_mad3(int a, int b, int c, int add)
{
	if (!stbi__mad3sizes_valid(a, b, c, add)) return NULL;
	return stbi__malloc(a * b * c + add);
}


// stbi__err - error
// stbi__errpuc - error returning pointer to unsigned char


#define stbi__err(x,y)  stbi__err(x)
#define stbi__errpuc(x,y)  ((unsigned char *)(size_t) (stbi__err(x,y)?NULL:NULL))

void coyote_stbi_image_free(void *retval_from_stbi_load)
{
	STBI_FREE(retval_from_stbi_load);
}


enum
{
	STBI__SCAN_load = 0,
	STBI__SCAN_type,
	STBI__SCAN_header
};


#define STBI__BYTECAST(x)  ((uint8_t) ((x) & 255))  // truncate int to byte without warnings


//////////////////////////////////////////////////////////////////////////////
//
//  generic converter from built-in img_n to req_comp
//    individual types do this automatically as much as possible (e.g. jpeg
//    does all cases internally since it needs to colorspace convert anyway,
//    and it never has alpha, so very few cases ). png can automatically
//    interleave an alpha=255 channel, but falls back to this for other cases
//
//  assume data buffer is malloced, so malloc a new one and free that one
//  only failure mode is malloc failing

static uint8_t stbi__compute_y(int r, int g, int b)
{
	return (uint8_t) (((r * 77) + (g * 150) + (29 * b)) >> 8);
}


static unsigned char *stbi__convert_format(unsigned char *data, int img_n, int req_comp, unsigned int x, unsigned int y)
{
	int i, j;
	unsigned char *good;

	if (req_comp == img_n) return data;
	STBI_ASSERT(req_comp >= 1 && req_comp <= 4);

	good = (unsigned char *) stbi__malloc_mad3(req_comp, x, y, 0);
	if (good == NULL)
	{
		STBI_FREE(data);
		return stbi__errpuc("outofmem", "Out of memory");
	}

	for (j = 0; j < (int) y; ++j)
	{
		unsigned char *src = data + j * x * img_n;
		unsigned char *dest = good + j * x * req_comp;

#define STBI__COMBO(a,b)  ((a)*8+(b))
#define STBI__CASE(a,b)   case STBI__COMBO(a,b): for(i=x-1; i >= 0; --i, src += a, dest += b)
		// convert source image with img_n components to one with req_comp components;
		// avoid switch per pixel, so use switch per scanline and massive macros
		switch (STBI__COMBO(img_n, req_comp))
		{
			STBI__CASE(1, 2)
				{
					dest[0] = src[0];
					dest[1] = 255;
				}
				break;
			STBI__CASE(1, 3) { dest[0] = dest[1] = dest[2] = src[0]; }
				break;
			STBI__CASE(1, 4)
				{
					dest[0] = dest[1] = dest[2] = src[0];
					dest[3] = 255;
				}
				break;
			STBI__CASE(2, 1) { dest[0] = src[0]; }
				break;
			STBI__CASE(2, 3) { dest[0] = dest[1] = dest[2] = src[0]; }
				break;
			STBI__CASE(2, 4)
				{
					dest[0] = dest[1] = dest[2] = src[0];
					dest[3] = src[1];
				}
				break;
			STBI__CASE(3, 4)
				{
					dest[0] = src[0];
					dest[1] = src[1];
					dest[2] = src[2];
					dest[3] = 255;
				}
				break;
			STBI__CASE(3, 1) { dest[0] = stbi__compute_y(src[0], src[1], src[2]); }
				break;
			STBI__CASE(3, 2)
				{
					dest[0] = stbi__compute_y(src[0], src[1], src[2]);
					dest[1] = 255;
				}
				break;
			STBI__CASE(4, 1) { dest[0] = stbi__compute_y(src[0], src[1], src[2]); }
				break;
			STBI__CASE(4, 2)
				{
					dest[0] = stbi__compute_y(src[0], src[1], src[2]);
					dest[1] = src[3];
				}
				break;
			STBI__CASE(4, 3)
				{
					dest[0] = src[0];
					dest[1] = src[1];
					dest[2] = src[2];
				}
				break;
			default: STBI_ASSERT(0);
				STBI_FREE(data);
				STBI_FREE(good);
				return stbi__errpuc("unsupported", "Unsupported format conversion");
		}
#undef STBI__CASE
	}

	STBI_FREE(data);
	return good;
}


static uint16_t *stbi__convert_format16(uint16_t *data, int img_n, int req_comp, unsigned int x, unsigned int y)
{
	int i;

	if (req_comp == img_n) return data;
	STBI_ASSERT(req_comp >= 1 && req_comp <= 4);

	auto good = static_cast<uint16_t *>(stbi__malloc(req_comp * x * y * 2));
	if (good == nullptr)
	{
		STBI_FREE(data);
		return (uint16_t *) stbi__errpuc("outofmem", "Out of memory");
	}

	static auto stbi__compute_y_16 = [](int r, int g, int b) {
		return static_cast<uint16_t>(((r * 77) + (g * 150) + (29 * b)) >> 8);
	};

	for (int j = 0; j < (int) y; ++j)
	{
		uint16_t *src = data + j * x * img_n;
		uint16_t *dest = good + j * x * req_comp;

#define STBI__COMBO(a,b)  ((a)*8+(b))
#define STBI__CASE(a,b)   case STBI__COMBO(a,b): for(i=x-1; i >= 0; --i, src += a, dest += b)
		// convert source image with img_n components to one with req_comp components;
		// avoid switch per pixel, so use switch per scanline and massive macros
		switch (STBI__COMBO(img_n, req_comp))
		{
			STBI__CASE(1, 2)
				{
					dest[0] = src[0];
					dest[1] = 0xffff;
				}
				break;
			STBI__CASE(1, 3) { dest[0] = dest[1] = dest[2] = src[0]; }
				break;
			STBI__CASE(1, 4)
				{
					dest[0] = dest[1] = dest[2] = src[0];
					dest[3] = 0xffff;
				}
				break;
			STBI__CASE(2, 1) { dest[0] = src[0]; }
				break;
			STBI__CASE(2, 3) { dest[0] = dest[1] = dest[2] = src[0]; }
				break;
			STBI__CASE(2, 4)
				{
					dest[0] = dest[1] = dest[2] = src[0];
					dest[3] = src[1];
				}
				break;
			STBI__CASE(3, 4)
				{
					dest[0] = src[0];
					dest[1] = src[1];
					dest[2] = src[2];
					dest[3] = 0xffff;
				}
				break;
			STBI__CASE(3, 1) { dest[0] = stbi__compute_y_16(src[0], src[1], src[2]); }
				break;
			STBI__CASE(3, 2)
				{
					dest[0] = stbi__compute_y_16(src[0], src[1], src[2]);
					dest[1] = 0xffff;
				}
				break;
			STBI__CASE(4, 1) { dest[0] = stbi__compute_y_16(src[0], src[1], src[2]); }
				break;
			STBI__CASE(4, 2)
				{
					dest[0] = stbi__compute_y_16(src[0], src[1], src[2]);
					dest[1] = src[3];
				}
				break;
			STBI__CASE(4, 3)
				{
					dest[0] = src[0];
					dest[1] = src[1];
					dest[2] = src[2];
				}
				break;
			default: STBI_ASSERT(0);
				STBI_FREE(data);
				STBI_FREE(good);
				return (uint16_t *) stbi__errpuc("unsupported", "Unsupported format conversion");
		}
#undef STBI__CASE
	}

	STBI_FREE(data);
	return good;
}


// public domain "baseline" PNG decoder   v0.10  Sean Barrett 2006-11-18
//    simple implementation
//      - only 8-bit samples
//      - no CRC checking
//      - allocates lots of intermediate memory
//        - avoids problem of streaming data between subsystems
//        - avoids explicit window management
//    performance
//      - uses stb_zlib, a PD zlib implementation with fast huffman decoding

struct stbi__pngchunk
{
	uint32_t length;
	uint32_t type;
};

static stbi__pngchunk stbi__get_chunk_header(stbi__context *s)
{
	stbi__pngchunk c;
	c.length = s->get32be();
	c.type = s->get32be();
	return c;
}

static const uint8_t png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};

static int stbi__check_png_header(stbi__context *s)
{
	for (const auto i: png_sig)
	{
		if (s->get8() != i)
		{
			return stbi__err("bad png sig", "Not a PNG");
		}
	}
	return 1;
}

struct stbi__png
{
	stbi__context *s;
	uint8_t *idata, *expanded, *out;
	int depth;
};


enum
{
	STBI__F_none = 0,
	STBI__F_sub = 1,
	STBI__F_up = 2,
	STBI__F_avg = 3,
	STBI__F_paeth = 4,
	// synthetic filter used for first scanline to avoid needing a dummy row of 0s
	STBI__F_avg_first
};

static uint8_t first_row_filter[5] =
{
	STBI__F_none,
	STBI__F_sub,
	STBI__F_none,
	STBI__F_avg_first,
	STBI__F_sub // Paeth with b=c=0 turns out to be equivalent to sub
};

static const uint8_t stbi__depth_scale_table[9] = {0, 0xff, 0x55, 0, 0x11, 0, 0, 0, 0x01};

// adds an extra all-255 alpha channel
// dest == src is legal
// img_n must be 1 or 3
static void stbi__create_png_alpha_expand8(uint8_t *dest, uint8_t *src, uint32_t x, int img_n)
{
	int i;
	// must process data backwards since we allow dest==src
	if (img_n == 1)
	{
		for (i = x - 1; i >= 0; --i)
		{
			dest[i * 2 + 1] = 255;
			dest[i * 2 + 0] = src[i];
		}
	}
	else
	{
		STBI_ASSERT(img_n == 3);
		for (i = x - 1; i >= 0; --i)
		{
			dest[i * 4 + 3] = 255;
			dest[i * 4 + 2] = src[i * 3 + 2];
			dest[i * 4 + 1] = src[i * 3 + 1];
			dest[i * 4 + 0] = src[i * 3 + 0];
		}
	}
}

// create the png data from post-deflated data
static int stbi__create_png_image_raw(stbi__png *a, uint8_t *raw, uint32_t raw_len, int out_n, uint32_t x,
                                      uint32_t y, int depth, int color)
{
	int bytes = (depth == 16 ? 2 : 1);
	stbi__context *s = a->s;
	uint32_t i, stride = x * out_n * bytes;
	int all_ok = 1;
	int k;
	int img_n = s->img_n; // copy it into a local for later

	int output_bytes = out_n * bytes;
	int filter_bytes = img_n * bytes;
	int width = x;

	STBI_ASSERT(out_n == s->img_n || out_n == s->img_n+1);
	// extra bytes to write off the end into
	a->out = static_cast<uint8_t *>(stbi__malloc_mad3(x, y, output_bytes, 0));
	if (!a->out)
	{
		return stbi__err("outofmem", "Out of memory");
	}

	// note: error exits here don't need to clean up a->out individually,
	// stbi__do_png always does on error.
	if (!stbi__mad3sizes_valid(img_n, x, depth, 7))
	{
		return stbi__err("too large", "Corrupt PNG");
	}
	uint32_t img_width_bytes = (((img_n * x * depth) + 7) >> 3);
	if (!stbi__mad2sizes_valid(img_width_bytes, y, img_width_bytes))
	{
		return stbi__err("too large", "Corrupt PNG");
	}
	uint32_t img_len = (img_width_bytes + 1) * y;

	// we used to check for exact match between raw_len and img_len on non-interlaced PNGs,
	// but issue #276 reported a PNG in the wild that had extra data at the end (all zeros),
	// so just check for raw_len < img_len always.
	if (raw_len < img_len)
	{
		return stbi__err("not enough pixels", "Corrupt PNG");
	}

	// Allocate two scan lines worth of filter workspace buffer.
	auto filter_buf = static_cast<uint8_t*>(stbi__malloc_mad2(img_width_bytes, 2, 0));
	if (!filter_buf)
	{
		return stbi__err("outofmem", "Out of memory");
	}

	// Filtering for low-bit-depth images
	if (depth < 8)
	{
		filter_bytes = 1;
		width = img_width_bytes;
	}

	static auto paeth = [](int a, int b, int c) {
		// This formulation looks very different from the reference in the PNG spec, but is
		// actually equivalent and has favorable data dependencies and admits straightforward
		// generation of branch-free code, which helps performance significantly.
		int thresh = c * 3 - (a + b);
		int lo = a < b ? a : b;
		int hi = a < b ? b : a;
		int t0 = (hi <= thresh) ? lo : c;
		return thresh <= lo ? hi : t0;
	};

	for (uint32_t j = 0; j < y; ++j)
	{
		// cur/prior filter buffers alternate
		uint8_t *cur = filter_buf + (j & 1) * img_width_bytes;
		uint8_t *prior = filter_buf + (~j & 1) * img_width_bytes;
		uint8_t *dest = a->out + stride * j;
		int nk = width * filter_bytes;
		int filter = *raw++;

		// check filter type
		if (filter > 4)
		{
			all_ok = stbi__err("invalid filter", "Corrupt PNG");
			break;
		}

		// if first row, use special filter that doesn't sample previous row
		if (j == 0) filter = first_row_filter[filter];

		// perform actual filtering
		switch (filter)
		{
			case STBI__F_none:
				memcpy(cur, raw, nk);
				break;
			case STBI__F_sub:
				memcpy(cur, raw, filter_bytes);
				for (k = filter_bytes; k < nk; ++k)
					cur[k] = STBI__BYTECAST(raw[k] + cur[k-filter_bytes]);
				break;
			case STBI__F_up:
				for (k = 0; k < nk; ++k)
					cur[k] = STBI__BYTECAST(raw[k] + prior[k]);
				break;
			case STBI__F_avg:
				for (k = 0; k < filter_bytes; ++k)
					cur[k] = STBI__BYTECAST(raw[k] + (prior[k]>>1));
				for (k = filter_bytes; k < nk; ++k)
					cur[k] = STBI__BYTECAST(raw[k] + ((prior[k] + cur[k-filter_bytes])>>1));
				break;
			case STBI__F_paeth:
				for (k = 0; k < filter_bytes; ++k)
					cur[k] = STBI__BYTECAST(raw[k] + prior[k]); // prior[k] == stbi__paeth(0,prior[k],0)
				for (k = filter_bytes; k < nk; ++k)
					cur[k] = STBI__BYTECAST(raw[k] + paeth(cur[k-filter_bytes], prior[k], prior[k-filter_bytes]));
				break;
			case STBI__F_avg_first:
				memcpy(cur, raw, filter_bytes);
				for (k = filter_bytes; k < nk; ++k)
					cur[k] = STBI__BYTECAST(raw[k] + (cur[k-filter_bytes] >> 1));
				break;
		}

		raw += nk;

		// expand decoded bits in cur to dest, also adding an extra alpha channel if desired
		if (depth < 8)
		{
			auto scale = (color == 0) ? stbi__depth_scale_table[depth] : 1; // scale grayscale values to 0..255 range
			auto* in = cur;
			auto* out = dest;
			uint8_t inb = 0;
			uint32_t nsmp = x * img_n;

			// expand bits to bytes first
			if (depth == 4)
			{
				for (i = 0; i < nsmp; ++i)
				{
					if ((i & 1) == 0) inb = *in++;
					*out++ = scale * (inb >> 4);
					inb <<= 4;
				}
			}
			else if (depth == 2)
			{
				for (i = 0; i < nsmp; ++i)
				{
					if ((i & 3) == 0) inb = *in++;
					*out++ = scale * (inb >> 6);
					inb <<= 2;
				}
			}
			else
			{
				STBI_ASSERT(depth == 1);
				for (i = 0; i < nsmp; ++i)
				{
					if ((i & 7) == 0) inb = *in++;
					*out++ = scale * (inb >> 7);
					inb <<= 1;
				}
			}

			// insert alpha=255 values if desired
			if (img_n != out_n)
			{
				stbi__create_png_alpha_expand8(dest, dest, x, img_n);
			}
		}
		else if (depth == 8)
		{
			if (img_n == out_n)
			{
				memcpy(dest, cur, x * img_n);
			}
			else
			{
				stbi__create_png_alpha_expand8(dest, cur, x, img_n);
			}
		}
		else if (depth == 16)
		{
			// convert the image data from big-endian to platform-native
			auto dest16 = reinterpret_cast<uint16_t *>(dest);
			uint32_t nsmp = x * img_n;

			if (img_n == out_n)
			{
				for (i = 0; i < nsmp; ++i, ++dest16, cur += 2)
					*dest16 = (cur[0] << 8) | cur[1];
			}
			else
			{
				STBI_ASSERT(img_n+1 == out_n);
				if (img_n == 1)
				{
					for (i = 0; i < x; ++i, dest16 += 2, cur += 2)
					{
						dest16[0] = (cur[0] << 8) | cur[1];
						dest16[1] = 0xffff;
					}
				}
				else
				{
					STBI_ASSERT(img_n == 3);
					for (i = 0; i < x; ++i, dest16 += 4, cur += 6)
					{
						dest16[0] = (cur[0] << 8) | cur[1];
						dest16[1] = (cur[2] << 8) | cur[3];
						dest16[2] = (cur[4] << 8) | cur[5];
						dest16[3] = 0xffff;
					}
				}
			}
		}
	}

	STBI_FREE(filter_buf);
	if (!all_ok) return 0;

	return 1;
}

static int stbi__create_png_image(stbi__png *a, uint8_t *image_data, uint32_t image_data_len, int out_n, int depth,
                                  int color, int interlaced)
{
	int bytes = (depth == 16 ? 2 : 1);
	int out_bytes = out_n * bytes;
	int p;
	if (!interlaced)
	{
		return stbi__create_png_image_raw(a, image_data, image_data_len, out_n, a->s->img_x, a->s->img_y, depth, color);
	}

	// de-interlacing
	auto *final = (uint8_t *) stbi__malloc_mad3(a->s->img_x, a->s->img_y, out_bytes, 0);
	if (!final)
	{
		return stbi__err("outofmem", "Out of memory");
	}
	for (p = 0; p < 7; ++p)
	{
		int xorig[] = {0, 4, 0, 2, 0, 1, 0};
		int yorig[] = {0, 0, 4, 0, 2, 0, 1};
		int xspc[] = {8, 8, 4, 4, 2, 2, 1};
		int yspc[] = {8, 8, 8, 4, 4, 2, 2};
		// pass1_x[4] = 0, pass1_x[5] = 1, pass1_x[12] = 1
		int x = (a->s->img_x - xorig[p] + xspc[p] - 1) / xspc[p];
		int y = (a->s->img_y - yorig[p] + yspc[p] - 1) / yspc[p];
		if (x && y)
		{
			uint32_t img_len = ((((a->s->img_n * x * depth) + 7) >> 3) + 1) * y;
			if (!stbi__create_png_image_raw(a, image_data, image_data_len, out_n, x, y, depth, color))
			{
				STBI_FREE(final);
				return 0;
			}
			for (int j = 0; j < y; ++j)
			{
				for (int i = 0; i < x; ++i)
				{
					int out_y = j * yspc[p] + yorig[p];
					int out_x = i * xspc[p] + xorig[p];
					std::memcpy(final + out_y * a->s->img_x * out_bytes + out_x * out_bytes,
					            a->out + (j * x + i) * out_bytes, out_bytes);
				}
			}
			STBI_FREE(a->out);
			image_data += img_len;
			image_data_len -= img_len;
		}
	}
	a->out = final;

	return 1;
}

static int stbi__compute_transparency(stbi__png *z, uint8_t tc[3], int out_n)
{
	stbi__context *s = z->s;
	uint32_t i, pixel_count = s->img_x * s->img_y;
	uint8_t *p = z->out;

	// compute color-based transparency, assuming we've
	// already got 255 as the alpha value in the output
	STBI_ASSERT(out_n == 2 || out_n == 4);

	if (out_n == 2)
	{
		for (i = 0; i < pixel_count; ++i)
		{
			p[1] = (p[0] == tc[0] ? 0 : 255);
			p += 2;
		}
	}
	else
	{
		for (i = 0; i < pixel_count; ++i)
		{
			if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
				p[3] = 0;
			p += 4;
		}
	}
	return 1;
}

static int stbi__compute_transparency16(stbi__png *z, uint16_t tc[3], int out_n)
{
	stbi__context *s = z->s;
	uint32_t pixel_count = s->img_x * s->img_y;
	auto p = reinterpret_cast<uint16_t *>(z->out);

	// compute color-based transparency, assuming we've
	// already got 65535 as the alpha value in the output
	STBI_ASSERT(out_n == 2 || out_n == 4);

	if (out_n == 2)
	{
		for (auto i = 0; i < pixel_count; ++i)
		{
			p[1] = (p[0] == tc[0] ? 0 : 0xFFFF);
			p += 2;
		}
	}
	else
	{
		for (auto i = 0; i < pixel_count; ++i)
		{
			if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
			{
				p[3] = 0;
			}
			p += 4;
		}
	}
	return 1;
}

static int stbi__expand_png_palette(stbi__png *a, uint8_t *palette, int len, int pal_img_n)
{
	uint32_t i, pixel_count = a->s->img_x * a->s->img_y;
	uint8_t *orig = a->out;

	auto *p = (uint8_t *) stbi__malloc_mad2(pixel_count, pal_img_n, 0);
	if (p == nullptr)
	{
		return stbi__err("outofmem", "Out of memory");
	}

	// between here and free(out) below, exitting would leak
	uint8_t *temp_out = p;

	if (pal_img_n == 3)
	{
		for (i = 0; i < pixel_count; ++i)
		{
			int n = orig[i] * 4;
			p[0] = palette[n];
			p[1] = palette[n + 1];
			p[2] = palette[n + 2];
			p += 3;
		}
	}
	else
	{
		for (i = 0; i < pixel_count; ++i)
		{
			int n = orig[i] * 4;
			p[0] = palette[n];
			p[1] = palette[n + 1];
			p[2] = palette[n + 2];
			p[3] = palette[n + 3];
			p += 4;
		}
	}
	STBI_FREE(a->out);
	a->out = temp_out;

	STBI_NOTUSED(len);

	return 1;
}

// #define STBI__PNG_TYPE(a,b,c,d)  (((unsigned) (a) << 24) + ((unsigned) (b) << 16) + ((unsigned) (c) << 8) + (unsigned) (d))

static int stbi__parse_png_file(stbi__png *z, int scan, int req_comp)
{
	uint8_t palette[1024];
	uint8_t pal_img_n = 0;
	uint8_t has_trans = 0;
	uint8_t tc[3] = {0};
	uint16_t tc16[3];
	uint32_t ioff = 0;
	uint32_t idata_limit = 0;
	uint32_t i;
	uint32_t pal_len = 0;
	int first = 1;
	int k;
	int interlace = 0;
	int color = 0;
	int is_iphone = 0;
	stbi__context *s = z->s;

	z->expanded = nullptr;
	z->idata = nullptr;
	z->out = nullptr;

	if (!stbi__check_png_header(s))
	{
		return 0;
	}

	if (scan == STBI__SCAN_type)
	{
		return 1;
	}

	static constexpr auto PNG_TYPE = [](char a, char b, char c, char d) -> uint32_t {
		return (
			(static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
			(static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d)
		);
	};

	while (true)
	{
		stbi__pngchunk c = stbi__get_chunk_header(s);
		switch (c.type)
		{
			case PNG_TYPE('C', 'g', 'B', 'I'): {
				is_iphone = 1;
				s->skip(c.length);
				break;
			}
			case PNG_TYPE('I', 'H', 'D', 'R'): {
				if (!first)
				{
					return stbi__err("multiple IHDR", "Corrupt PNG");
				}
				first = 0;
				if (c.length != 13) return stbi__err("bad IHDR len", "Corrupt PNG");
				s->img_x = s->get32be();
				s->img_y = s->get32be();
				if (s->img_y > STBI_MAX_DIMENSIONS)
				{
					return stbi__err("too large", "Very large image (corrupt?)");
				}
				if (s->img_x > STBI_MAX_DIMENSIONS)
				{
					return stbi__err("too large", "Very large image (corrupt?)");
				}
				z->depth = s->get8();
				if (z->depth != 1 && z->depth != 2 && z->depth != 4 && z->depth != 8 && z->depth != 16)
				{
					return stbi__err(
						"1/2/4/8/16-bit only", "PNG not supported: 1/2/4/8/16-bit only");
				}
				color = s->get8();
				if (color > 6)
				{
					return stbi__err("bad ctype", "Corrupt PNG");
				}
				if (color == 3 && z->depth == 16)
				{
					return stbi__err("bad ctype", "Corrupt PNG");
				}
				if (color == 3)
				{
					pal_img_n = 3;
				}
				else if (color & 1)
				{
					return stbi__err("bad ctype", "Corrupt PNG");
				}
				int comp = s->get8();
				if (comp)
				{
					return stbi__err("bad comp method", "Corrupt PNG");
				}
				int filter = s->get8();
				if (filter)
				{
					return stbi__err("bad filter method", "Corrupt PNG");
				}
				interlace = s->get8();
				if (interlace > 1)
				{
					return stbi__err("bad interlace method", "Corrupt PNG");
				}
				if (!s->img_x || !s->img_y)
				{
					return stbi__err("0-pixel image", "Corrupt PNG");
				}
				if (pal_img_n)
				{
					// if paletted, then pal_n is our final components, and
					// img_n is # components to decompress/filter.
					s->img_n = 1;
					if ((1 << 30) / s->img_x / 4 < s->img_y)
					{
						return stbi__err("too large", "Corrupt PNG");
					}
				}
				else
				{
					s->img_n = (color & 2 ? 3 : 1) + (color & 4 ? 1 : 0);
					if ((1 << 30) / s->img_x / s->img_n < s->img_y)
					{
						return stbi__err(
							"too large", "Image too large to decode");
					}
				}
				// even with SCAN_header, have to scan to see if we have a tRNS
				break;
			}

			case PNG_TYPE('P', 'L', 'T', 'E'): {
				if (first)
				{
					return stbi__err("first not IHDR", "Corrupt PNG");
				}
				if (c.length > 256 * 3)
				{
					return stbi__err("invalid PLTE", "Corrupt PNG");
				}
				pal_len = c.length / 3;
				if (pal_len * 3 != c.length)
				{
					return stbi__err("invalid PLTE", "Corrupt PNG");
				}
				for (i = 0; i < pal_len; ++i)
				{
					palette[i * 4 + 0] = s->get8();
					palette[i * 4 + 1] = s->get8();
					palette[i * 4 + 2] = s->get8();
					palette[i * 4 + 3] = 255;
				}
				break;
			}

			case PNG_TYPE('t', 'R', 'N', 'S'): {
				if (first)
				{
					return stbi__err("first not IHDR", "Corrupt PNG");
				}
				if (z->idata)
				{
					return stbi__err("tRNS after IDAT", "Corrupt PNG");
				}
				if (pal_img_n)
				{
					if (scan == STBI__SCAN_header)
					{
						s->img_n = 4;
						return 1;
					}
					if (pal_len == 0)
					{
						return stbi__err("tRNS before PLTE", "Corrupt PNG");
					}
					if (c.length > pal_len)
					{
						return stbi__err("bad tRNS len", "Corrupt PNG");
					}
					pal_img_n = 4;
					for (i = 0; i < c.length; ++i)
					{
						palette[i * 4 + 3] = s->get8();
					}
				}
				else
				{
					if (!(s->img_n & 1))
					{
						return stbi__err("tRNS with alpha", "Corrupt PNG");
					}
					if (c.length != (uint32_t) s->img_n * 2)
					{
						return stbi__err("bad tRNS len", "Corrupt PNG");
					}
					has_trans = 1;
					// non-paletted with tRNS = constant alpha. if header-scanning, we can stop now.
					if (scan == STBI__SCAN_header)
					{
						++s->img_n;
						return 1;
					}
					if (z->depth == 16)
					{
						for (k = 0; k < s->img_n && k < 3; ++k) // extra loop test to suppress false GCC warning
						{
							tc16[k] = (uint16_t) s->get16be(); // copy the values as-is
						}
					}
					else
					{
						for (k = 0; k < s->img_n && k < 3; ++k)
						{
							tc[k] = (uint8_t) (s->get16be() & 255) * stbi__depth_scale_table[z->depth];
						}
						// non 8-bit images will be larger
					}
				}
				break;
			}

			case PNG_TYPE('I', 'D', 'A', 'T'): {
				if (first)
				{
					return stbi__err("first not IHDR", "Corrupt PNG");
				}
				if (pal_img_n && !pal_len)
				{
					return stbi__err("no PLTE", "Corrupt PNG");
				}
				if (scan == STBI__SCAN_header)
				{
					// header scan definitely stops at first IDAT
					if (pal_img_n)
					{
						s->img_n = pal_img_n;
					}
					return 1;
				}
				if (c.length > (1u << 30))
				{
					return stbi__err("IDAT size limit", "IDAT section larger than 2^30 bytes");
				}
				if ((int) (ioff + c.length) < (int) ioff)
				{
					return 0;
				}
				if (ioff + c.length > idata_limit)
				{
					uint32_t idata_limit_old = idata_limit;
					if (idata_limit == 0)
					{
						idata_limit = c.length > 4096 ? c.length : 4096;
					}
					while (ioff + c.length > idata_limit)
					{
						idata_limit *= 2;
					}
					STBI_NOTUSED(idata_limit_old);
					uint8_t *p = (uint8_t *) STBI_REALLOC_SIZED(z->idata, idata_limit_old, idata_limit);
					if (p == NULL)
					{
						return stbi__err("outofmem", "Out of memory");
					}
					z->idata = p;
				} {
					const auto n = c.length;
					const auto buffer = z->idata + ioff;
					if (s->img_buffer + n > s->img_buffer_end)
					{
						return stbi__err("outofdata", "Corrupt PNG");
					}
					std::memcpy(buffer, s->img_buffer, n);
					s->img_buffer += n;
				}
				ioff += c.length;
				break;
			}

			case PNG_TYPE('I', 'E', 'N', 'D'): {
				uint32_t raw_len, bpl;
				if (first)
				{
					return stbi__err("first not IHDR", "Corrupt PNG");
				}
				if (scan != STBI__SCAN_load)
				{
					return 1;
				}
				if (z->idata == NULL)
				{
					return stbi__err("no IDAT", "Corrupt PNG");
				}
				// initial guess for decoded data size to avoid unnecessary reallocs
				bpl = (s->img_x * z->depth + 7) / 8; // bytes per line, per component
				raw_len = bpl * s->img_y * s->img_n /* pixels */ + s->img_y /* filter mode per row */;

				auto zctx = Zlib::Context();
				zctx.buffer = z->idata;
				zctx.len = ioff;
				zctx.initial_size = raw_len;
				zctx.parse_header = !is_iphone;

				zctx.malloc = &stbi__malloc;
				zctx.free = [](void *p) { STBI_FREE(p); };
				zctx.realloc = [](void *p, size_t olds, size_t news) { return STBI_REALLOC_SIZED(p, olds, news); };

				try
				{
					z->expanded = zctx.decode_malloc_guesssize_headerflag();
				} catch (Zlib::Er er)
				{
					return stbi__err(er.reason, "");
				}

				raw_len = zctx.out_len;

				if (z->expanded == nullptr)
				{
					return stbi__err("z->expanded == nullptr", "");
				}
				STBI_FREE(z->idata);
				z->idata = nullptr;
				if ((req_comp == s->img_n + 1 && req_comp != 3 && !pal_img_n) || has_trans)
				{
					s->img_out_n = s->img_n + 1;
				}
				else
				{
					s->img_out_n = s->img_n;
				}
				if (!stbi__create_png_image(z, z->expanded, raw_len, s->img_out_n, z->depth, color, interlace))
				{
					return 0;
				}
				if (has_trans)
				{
					if (z->depth == 16)
					{
						if (!stbi__compute_transparency16(z, tc16, s->img_out_n))
						{
							return 0;
						}
					}
					else
					{
						if (!stbi__compute_transparency(z, tc, s->img_out_n))
						{
							return 0;
						}
					}
				}
				if (pal_img_n)
				{
					// pal_img_n == 3 or 4
					s->img_n = pal_img_n; // record the actual colors we had
					s->img_out_n = pal_img_n;
					if (req_comp >= 3)
					{
						s->img_out_n = req_comp;
					}

					if (!stbi__expand_png_palette(z, palette, pal_len, s->img_out_n))
					{
						return 0;
					}
				}
				else if (has_trans)
				{
					// non-paletted image with tRNS -> source image has (constant) alpha
					++s->img_n;
				}
				STBI_FREE(z->expanded);
				z->expanded = NULL;
				// end of PNG chunk, read and skip CRC
				s->get32be();
				return 1;
			}

			default: {
				// if critical, fail
				if (first)
				{
					return stbi__err("first not IHDR", "Corrupt PNG");
				}
				if ((c.type & (1 << 29)) == 0)
				{
#ifndef STBI_NO_FAILURE_STRINGS
					// not threadsafe
					static char invalid_chunk[] = "XXXX PNG chunk not known";
					invalid_chunk[0] = STBI__BYTECAST(c.type >> 24);
					invalid_chunk[1] = STBI__BYTECAST(c.type >> 16);
					invalid_chunk[2] = STBI__BYTECAST(c.type >> 8);
					invalid_chunk[3] = STBI__BYTECAST(c.type >> 0);
#endif
					return stbi__err(invalid_chunk, "PNG not supported: unknown PNG chunk type");
				}
				s->skip(c.length);
				break;
			}
		}
		// end of PNG chunk, read and skip CRC
		s->get32be();
	}
}

static void *stbi__do_png(
	stbi__png *p,
	size_t *x,
	size_t *y,
	size_t *n,
	size_t req_comp,
	stbi__result_info *ri)
{
	void *result = NULL;
	if (req_comp < 0 || req_comp > 4)
	{
		return stbi__errpuc("bad req_comp", "Internal error");
	}
	if (stbi__parse_png_file(p, STBI__SCAN_load, req_comp))
	{
		if (p->depth <= 8)
		{
			ri->bits_per_channel = 8;
		}
		else if (p->depth == 16)
		{
			ri->bits_per_channel = 16;
		}
		else
		{
			return stbi__errpuc("bad bits_per_channel", "PNG not supported: unsupported color depth");
		}
		result = p->out;
		p->out = NULL;
		if (req_comp && req_comp != p->s->img_out_n)
		{
			if (ri->bits_per_channel == 8)
			{
				result = stbi__convert_format((unsigned char *) result, p->s->img_out_n, req_comp, p->s->img_x,
				                              p->s->img_y);
			}
			else
			{
				result = stbi__convert_format16((uint16_t *) result, p->s->img_out_n, req_comp, p->s->img_x,
				                                p->s->img_y);
			}
			p->s->img_out_n = req_comp;
			if (result == NULL)
			{
				return result;
			}
		}
		*x = p->s->img_x;
		*y = p->s->img_y;
		if (n)
		{
			*n = p->s->img_n;
		}
	}
	STBI_FREE(p->out);
	p->out = NULL;
	STBI_FREE(p->expanded);
	p->expanded = NULL;
	STBI_FREE(p->idata);
	p->idata = NULL;

	return result;
}


int coyote_stbi_info_from_memory(uint8_t const *buffer, int len, int *x, int *y, int *comp)
{
	stbi__context s;
	s.start_mem(buffer, len);

	stbi__png p;
	p.s = &s;

	if (stbi__parse_png_file(&p, STBI__SCAN_header, 0))
	{
		if (x)
		{
			*x = p.s->img_x;
		}
		if (y)
		{
			*y = p.s->img_y;
		}
		if (comp)
		{
			*comp = p.s->img_n;
		}
		return 1;
	}
	stbi__rewind(p.s);
	return stbi__err("unknown image type", "Image not of any known type, or corrupt");
}


uint8_t *coyote_stbi_load_from_memory(
	uint8_t const *buffer,
	size_t len,
	size_t* x,
	size_t* y,
	size_t* comp,
	size_t req_comp)
{
	stbi__context s;
	s.start_mem(buffer, len);


	stbi__result_info ri;
	void* result;
	{
		std::memset(&ri, 0, sizeof(ri)); // make sure it's initialized if we add new fields
		ri.bits_per_channel = 8; // default is 8 so most paths don't have to be changed
		ri.num_channels = 0;

		// test the formats with a very explicit header first (at least a FOURCC
		// or distinctive magic number first)
		const auto r = stbi__check_png_header(&s);
		stbi__rewind(&s);
		if (r)
		{
			stbi__png p;
			p.s = &s;
			result = stbi__do_png(&p, x, y, comp, req_comp, &ri);
		}
		else
		{
			result = stbi__errpuc("unknown image type", "Image not of any known type, or corrupt");
		}

		if (result == nullptr)
		{
			return nullptr;
		}
	}


	// it is the responsibility of the loaders to make sure we get either 8 or 16 bit.
	STBI_ASSERT(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);

	if (ri.bits_per_channel != 8)
	{
		auto orig = static_cast<uint16_t *>(result);
		auto img_len = (*x) * (*y) * (req_comp == 0 ? *comp : req_comp);

		auto reduced = static_cast<uint8_t *>(stbi__malloc(img_len));
		if (reduced == nullptr)
		{
			return stbi__errpuc("outofmem", "Out of memory");
		}

		for (int i = 0; i < img_len; ++i)
		{
			// top half of each byte is sufficient approx of 16->8 bit scaling
			reduced[i] = static_cast<uint8_t>((orig[i] >> 8) & 0xFF);
		}

		STBI_FREE(orig);
		result = reduced;
		ri.bits_per_channel = 8;
	}

	return (unsigned char *) result;
}
