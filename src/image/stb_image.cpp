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
typedef uint16_t stbi__uint16;
typedef int16_t stbi__int16;
typedef uint32_t stbi__uint32;
typedef int32_t stbi__int32;

// should produce compiler error if size is wrong
typedef unsigned char validate_uint32[sizeof(stbi__uint32) == 4 ? 1 : -1];


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
typedef struct
{
	stbi__uint32 img_x, img_y;
	int img_n, img_out_n;

	stbi_io_callbacks io;
	void *io_user_data;

	int read_from_callbacks;
	int buflen;
	stbi_uc buffer_start[128];
	int callback_already_read;

	stbi_uc *img_buffer, *img_buffer_end;
	stbi_uc *img_buffer_original, *img_buffer_original_end;
} stbi__context;


static void stbi__refill_buffer(stbi__context *s);

// initialize a memory-decode context
static void stbi__start_mem(stbi__context *s, stbi_uc const *buffer, int len)
{
	s->io.read = NULL;
	s->read_from_callbacks = 0;
	s->callback_already_read = 0;
	s->img_buffer = s->img_buffer_original = (stbi_uc *) buffer;
	s->img_buffer_end = s->img_buffer_original_end = (stbi_uc *) buffer + len;
}

// initialize a callback-based context
static void stbi__start_callbacks(stbi__context *s, stbi_io_callbacks *c, void *user)
{
	s->io = *c;
	s->io_user_data = user;
	s->buflen = sizeof(s->buffer_start);
	s->read_from_callbacks = 1;
	s->callback_already_read = 0;
	s->img_buffer = s->img_buffer_original = s->buffer_start;
	stbi__refill_buffer(s);
	s->img_buffer_original_end = s->img_buffer_end;
}

static void stbi__rewind(stbi__context *s)
{
	// conceptually rewind SHOULD rewind to the beginning of the stream,
	// but we just rewind to the beginning of the initial buffer, because
	// we only use it after doing 'test', which only ever looks at at most 92 bytes
	s->img_buffer = s->img_buffer_original;
	s->img_buffer_end = s->img_buffer_original_end;
}

enum
{
	STBI_ORDER_RGB,
	STBI_ORDER_BGR
};

typedef struct
{
	int bits_per_channel;
	int num_channels;
	int channel_order;
} stbi__result_info;



static int stbi__png_test(stbi__context *s);

static void *stbi__png_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);

static int stbi__png_info(stbi__context *s, int *x, int *y, int *comp);

static int stbi__png_is16(stbi__context *s);


static
const char *stbi__g_failure_reason;

STBIDEF const char *stbi_failure_reason(void)
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

#if !defined(STBI_NO_JPEG) || !defined(STBI_NO_PNG) || !defined(STBI_NO_TGA) || !defined(STBI_NO_HDR)
// returns 1 if "a*b + add" has no negative terms/factors and doesn't overflow
static int stbi__mad2sizes_valid(int a, int b, int add)
{
	return stbi__mul2sizes_valid(a, b) && stbi__addsizes_valid(a * b, add);
}
#endif

// returns 1 if "a*b*c + add" has no negative terms/factors and doesn't overflow
static int stbi__mad3sizes_valid(int a, int b, int c, int add)
{
	return stbi__mul2sizes_valid(a, b) && stbi__mul2sizes_valid(a * b, c) &&
	       stbi__addsizes_valid(a * b * c, add);
}

// returns 1 if "a*b*c*d + add" has no negative terms/factors and doesn't overflow
#if !defined(STBI_NO_LINEAR) || !defined(STBI_NO_HDR) || !defined(STBI_NO_PNM)
static int stbi__mad4sizes_valid(int a, int b, int c, int d, int add)
{
	return stbi__mul2sizes_valid(a, b) && stbi__mul2sizes_valid(a * b, c) &&
	       stbi__mul2sizes_valid(a * b * c, d) && stbi__addsizes_valid(a * b * c * d, add);
}
#endif

#if !defined(STBI_NO_JPEG) || !defined(STBI_NO_PNG) || !defined(STBI_NO_TGA) || !defined(STBI_NO_HDR)
// mallocs with size overflow checking
static void *stbi__malloc_mad2(int a, int b, int add)
{
	if (!stbi__mad2sizes_valid(a, b, add)) return NULL;
	return stbi__malloc(a * b + add);
}
#endif

static void *stbi__malloc_mad3(int a, int b, int c, int add)
{
	if (!stbi__mad3sizes_valid(a, b, c, add)) return NULL;
	return stbi__malloc(a * b * c + add);
}

#if !defined(STBI_NO_LINEAR) || !defined(STBI_NO_HDR) || !defined(STBI_NO_PNM)
static void *stbi__malloc_mad4(int a, int b, int c, int d, int add)
{
	if (!stbi__mad4sizes_valid(a, b, c, d, add)) return NULL;
	return stbi__malloc(a * b * c * d + add);
}
#endif

// returns 1 if the sum of two signed ints is valid (between -2^31 and 2^31-1 inclusive), 0 on overflow.
static int stbi__addints_valid(int a, int b)
{
	if ((a >= 0) != (b >= 0)) return 1; // a and b have different signs, so no overflow
	if (a < 0 && b < 0) return a >= INT_MIN - b; // same as a + b >= INT_MIN; INT_MIN - b cannot overflow since b < 0.
	return a <= INT_MAX - b;
}

// returns 1 if the product of two ints fits in a signed short, 0 on overflow.
static int stbi__mul2shorts_valid(int a, int b)
{
	if (b == 0 || b == -1) return 1; // multiplication by 0 is always 0; check for -1 so SHRT_MIN/b doesn't overflow
	if ((a >= 0) == (b >= 0)) return a <= SHRT_MAX / b; // product is positive, so similar to mul2sizes_valid
	if (b < 0) return a <= SHRT_MIN / b; // same as a * b >= SHRT_MIN
	return a >= SHRT_MIN / b;
}

// stbi__err - error
// stbi__errpf - error returning pointer to float
// stbi__errpuc - error returning pointer to unsigned char

#ifdef STBI_NO_FAILURE_STRINGS
   #define stbi__err(x,y)  0
#elif defined(STBI_FAILURE_USERMSG)
   #define stbi__err(x,y)  stbi__err(y)
#else
#define stbi__err(x,y)  stbi__err(x)
#endif

#define stbi__errpf(x,y)   ((float *)(size_t) (stbi__err(x,y)?NULL:NULL))
#define stbi__errpuc(x,y)  ((unsigned char *)(size_t) (stbi__err(x,y)?NULL:NULL))

STBIDEF void stbi_image_free(void *retval_from_stbi_load)
{
	STBI_FREE(retval_from_stbi_load);
}


static void *stbi__load_main(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri, int bpc)
{
	memset(ri, 0, sizeof(*ri)); // make sure it's initialized if we add new fields
	ri->bits_per_channel = 8; // default is 8 so most paths don't have to be changed
	ri->channel_order = STBI_ORDER_RGB; // all current input & output are this, but this is here so we can add BGR order
	ri->num_channels = 0;

	// test the formats with a very explicit header first (at least a FOURCC
	// or distinctive magic number first)
	if (stbi__png_test(s)) return stbi__png_load(s, x, y, comp, req_comp, ri);
	return stbi__errpuc("unknown image type", "Image not of any known type, or corrupt");
}

static stbi_uc *stbi__convert_16_to_8(stbi__uint16 *orig, int w, int h, int channels)
{
	int i;
	int img_len = w * h * channels;
	stbi_uc *reduced;

	reduced = (stbi_uc *) stbi__malloc(img_len);
	if (reduced == NULL) return stbi__errpuc("outofmem", "Out of memory");

	for (i = 0; i < img_len; ++i)
		reduced[i] = (stbi_uc) ((orig[i] >> 8) & 0xFF); // top half of each byte is sufficient approx of 16->8 bit scaling

	STBI_FREE(orig);
	return reduced;
}

static stbi__uint16 *stbi__convert_8_to_16(stbi_uc *orig, int w, int h, int channels)
{
	int i;
	int img_len = w * h * channels;
	stbi__uint16 *enlarged;

	enlarged = (stbi__uint16 *) stbi__malloc(img_len * 2);
	if (enlarged == NULL) return (stbi__uint16 *) stbi__errpuc("outofmem", "Out of memory");

	for (i = 0; i < img_len; ++i)
		enlarged[i] = (stbi__uint16) ((orig[i] << 8) + orig[i]); // replicate to high and low byte, maps 0->0, 255->0xffff

	STBI_FREE(orig);
	return enlarged;
}

static void stbi__vertical_flip(void *image, int w, int h, int bytes_per_pixel)
{
	int row;
	size_t bytes_per_row = (size_t) w * bytes_per_pixel;
	stbi_uc temp[2048];
	stbi_uc *bytes = (stbi_uc *) image;

	for (row = 0; row < (h >> 1); row++)
	{
		stbi_uc *row0 = bytes + row * bytes_per_row;
		stbi_uc *row1 = bytes + (h - row - 1) * bytes_per_row;
		// swap row0 with row1
		size_t bytes_left = bytes_per_row;
		while (bytes_left)
		{
			size_t bytes_copy = (bytes_left < sizeof(temp)) ? bytes_left : sizeof(temp);
			memcpy(temp, row0, bytes_copy);
			memcpy(row0, row1, bytes_copy);
			memcpy(row1, temp, bytes_copy);
			row0 += bytes_copy;
			row1 += bytes_copy;
			bytes_left -= bytes_copy;
		}
	}
}

static unsigned char *stbi__load_and_postprocess_8bit(stbi__context *s, int *x, int *y, int *comp, int req_comp)
{
	stbi__result_info ri;
	void *result = stbi__load_main(s, x, y, comp, req_comp, &ri, 8);

	if (result == NULL)
		return NULL;

	// it is the responsibility of the loaders to make sure we get either 8 or 16 bit.
	STBI_ASSERT(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);

	if (ri.bits_per_channel != 8)
	{
		result = stbi__convert_16_to_8((stbi__uint16 *) result, *x, *y, req_comp == 0 ? *comp : req_comp);
		ri.bits_per_channel = 8;
	}

	return (unsigned char *) result;
}

static stbi__uint16 *stbi__load_and_postprocess_16bit(stbi__context *s, int *x, int *y, int *comp, int req_comp)
{
	stbi__result_info ri;
	void *result = stbi__load_main(s, x, y, comp, req_comp, &ri, 16);

	if (result == NULL)
		return NULL;

	// it is the responsibility of the loaders to make sure we get either 8 or 16 bit.
	STBI_ASSERT(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);

	if (ri.bits_per_channel != 16)
	{
		result = stbi__convert_8_to_16((stbi_uc *) result, *x, *y, req_comp == 0 ? *comp : req_comp);
		ri.bits_per_channel = 16;
	}

	// @TODO: move stbi__convert_format16 to here

	return (stbi__uint16 *) result;
}



STBIDEF stbi_us *stbi_load_16_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *channels_in_file,
                                          int desired_channels)
{
	stbi__context s;
	stbi__start_mem(&s, buffer, len);
	return stbi__load_and_postprocess_16bit(&s, x, y, channels_in_file, desired_channels);
}

STBIDEF stbi_us *stbi_load_16_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y,
                                             int *channels_in_file, int desired_channels)
{
	stbi__context s;
	stbi__start_callbacks(&s, (stbi_io_callbacks *) clbk, user);
	return stbi__load_and_postprocess_16bit(&s, x, y, channels_in_file, desired_channels);
}

STBIDEF stbi_uc *stbi_load_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp, int req_comp)
{
	stbi__context s;
	stbi__start_mem(&s, buffer, len);
	return stbi__load_and_postprocess_8bit(&s, x, y, comp, req_comp);
}

STBIDEF stbi_uc *stbi_load_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *comp,
                                          int req_comp)
{
	stbi__context s;
	stbi__start_callbacks(&s, (stbi_io_callbacks *) clbk, user);
	return stbi__load_and_postprocess_8bit(&s, x, y, comp, req_comp);
}


static float stbi__h2l_gamma_i = 1.0f / 2.2f, stbi__h2l_scale_i = 1.0f;

STBIDEF void stbi_hdr_to_ldr_gamma(float gamma) { stbi__h2l_gamma_i = 1 / gamma; }
STBIDEF void stbi_hdr_to_ldr_scale(float scale) { stbi__h2l_scale_i = 1 / scale; }


//////////////////////////////////////////////////////////////////////////////
//
// Common code used by all image loaders
//

enum
{
	STBI__SCAN_load = 0,
	STBI__SCAN_type,
	STBI__SCAN_header
};

static void stbi__refill_buffer(stbi__context *s)
{
	int n = (s->io.read)(s->io_user_data, (char *) s->buffer_start, s->buflen);
	s->callback_already_read += (int) (s->img_buffer - s->img_buffer_original);
	if (n == 0)
	{
		// at end of file, treat same as if from memory, but need to handle case
		// where s->img_buffer isn't pointing to safe memory, e.g. 0-byte file
		s->read_from_callbacks = 0;
		s->img_buffer = s->buffer_start;
		s->img_buffer_end = s->buffer_start + 1;
		*s->img_buffer = 0;
	}
	else
	{
		s->img_buffer = s->buffer_start;
		s->img_buffer_end = s->buffer_start + n;
	}
}

static stbi_uc stbi__get8(stbi__context *s)
{
	if (s->img_buffer < s->img_buffer_end)
		return *s->img_buffer++;
	if (s->read_from_callbacks)
	{
		stbi__refill_buffer(s);
		return *s->img_buffer++;
	}
	return 0;
}



static void stbi__skip(stbi__context *s, int n)
{
	if (n == 0) return; // already there!
	if (n < 0)
	{
		s->img_buffer = s->img_buffer_end;
		return;
	}
	if (s->io.read)
	{
		int blen = (int) (s->img_buffer_end - s->img_buffer);
		if (blen < n)
		{
			s->img_buffer = s->img_buffer_end;
			(s->io.skip)(s->io_user_data, n - blen);
			return;
		}
	}
	s->img_buffer += n;
}


static int stbi__getn(stbi__context *s, stbi_uc *buffer, int n)
{
	if (s->io.read)
	{
		int blen = (int) (s->img_buffer_end - s->img_buffer);
		if (blen < n)
		{
			int res, count;

			memcpy(buffer, s->img_buffer, blen);

			count = (s->io.read)(s->io_user_data, (char *) buffer + blen, n - blen);
			res = (count == (n - blen));
			s->img_buffer = s->img_buffer_end;
			return res;
		}
	}

	if (s->img_buffer + n <= s->img_buffer_end)
	{
		memcpy(buffer, s->img_buffer, n);
		s->img_buffer += n;
		return 1;
	}
	else
		return 0;
}



static int stbi__get16be(stbi__context *s)
{
	int z = stbi__get8(s);
	return (z << 8) + stbi__get8(s);
}


static stbi__uint32 stbi__get32be(stbi__context *s)
{
	stbi__uint32 z = stbi__get16be(s);
	return (z << 16) + stbi__get16be(s);
}



#define STBI__BYTECAST(x)  ((stbi_uc) ((x) & 255))  // truncate int to byte without warnings


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

static stbi_uc stbi__compute_y(int r, int g, int b)
{
	return (stbi_uc) (((r * 77) + (g * 150) + (29 * b)) >> 8);
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


static stbi__uint16 stbi__compute_y_16(int r, int g, int b)
{
	return (stbi__uint16) (((r * 77) + (g * 150) + (29 * b)) >> 8);
}

static stbi__uint16 *stbi__convert_format16(stbi__uint16 *data, int img_n, int req_comp, unsigned int x, unsigned int y)
{
	int i, j;
	stbi__uint16 *good;

	if (req_comp == img_n) return data;
	STBI_ASSERT(req_comp >= 1 && req_comp <= 4);

	good = (stbi__uint16 *) stbi__malloc(req_comp * x * y * 2);
	if (good == NULL)
	{
		STBI_FREE(data);
		return (stbi__uint16 *) stbi__errpuc("outofmem", "Out of memory");
	}

	for (j = 0; j < (int) y; ++j)
	{
		stbi__uint16 *src = data + j * x * img_n;
		stbi__uint16 *dest = good + j * x * req_comp;

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
				return (stbi__uint16 *) stbi__errpuc("unsupported", "Unsupported format conversion");
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

typedef struct
{
	stbi__uint32 length;
	stbi__uint32 type;
} stbi__pngchunk;

static stbi__pngchunk stbi__get_chunk_header(stbi__context *s)
{
	stbi__pngchunk c;
	c.length = stbi__get32be(s);
	c.type = stbi__get32be(s);
	return c;
}

static int stbi__check_png_header(stbi__context *s)
{
	static const stbi_uc png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
	int i;
	for (i = 0; i < 8; ++i)
		if (stbi__get8(s) != png_sig[i]) return stbi__err("bad png sig", "Not a PNG");
	return 1;
}

typedef struct
{
	stbi__context *s;
	stbi_uc *idata, *expanded, *out;
	int depth;
} stbi__png;


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

static stbi_uc first_row_filter[5] =
{
	STBI__F_none,
	STBI__F_sub,
	STBI__F_none,
	STBI__F_avg_first,
	STBI__F_sub // Paeth with b=c=0 turns out to be equivalent to sub
};

static int stbi__paeth(int a, int b, int c)
{
	// This formulation looks very different from the reference in the PNG spec, but is
	// actually equivalent and has favorable data dependencies and admits straightforward
	// generation of branch-free code, which helps performance significantly.
	int thresh = c * 3 - (a + b);
	int lo = a < b ? a : b;
	int hi = a < b ? b : a;
	int t0 = (hi <= thresh) ? lo : c;
	int t1 = (thresh <= lo) ? hi : t0;
	return t1;
}

static const stbi_uc stbi__depth_scale_table[9] = {0, 0xff, 0x55, 0, 0x11, 0, 0, 0, 0x01};

// adds an extra all-255 alpha channel
// dest == src is legal
// img_n must be 1 or 3
static void stbi__create_png_alpha_expand8(stbi_uc *dest, stbi_uc *src, stbi__uint32 x, int img_n)
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
static int stbi__create_png_image_raw(stbi__png *a, stbi_uc *raw, stbi__uint32 raw_len, int out_n, stbi__uint32 x,
                                      stbi__uint32 y, int depth, int color)
{
	int bytes = (depth == 16 ? 2 : 1);
	stbi__context *s = a->s;
	stbi__uint32 i, j, stride = x * out_n * bytes;
	stbi__uint32 img_len, img_width_bytes;
	stbi_uc *filter_buf;
	int all_ok = 1;
	int k;
	int img_n = s->img_n; // copy it into a local for later

	int output_bytes = out_n * bytes;
	int filter_bytes = img_n * bytes;
	int width = x;

	STBI_ASSERT(out_n == s->img_n || out_n == s->img_n+1);
	a->out = (stbi_uc *) stbi__malloc_mad3(x, y, output_bytes, 0); // extra bytes to write off the end into
	if (!a->out) return stbi__err("outofmem", "Out of memory");

	// note: error exits here don't need to clean up a->out individually,
	// stbi__do_png always does on error.
	if (!stbi__mad3sizes_valid(img_n, x, depth, 7)) return stbi__err("too large", "Corrupt PNG");
	img_width_bytes = (((img_n * x * depth) + 7) >> 3);
	if (!stbi__mad2sizes_valid(img_width_bytes, y, img_width_bytes)) return stbi__err("too large", "Corrupt PNG");
	img_len = (img_width_bytes + 1) * y;

	// we used to check for exact match between raw_len and img_len on non-interlaced PNGs,
	// but issue #276 reported a PNG in the wild that had extra data at the end (all zeros),
	// so just check for raw_len < img_len always.
	if (raw_len < img_len) return stbi__err("not enough pixels", "Corrupt PNG");

	// Allocate two scan lines worth of filter workspace buffer.
	filter_buf = (stbi_uc *) stbi__malloc_mad2(img_width_bytes, 2, 0);
	if (!filter_buf) return stbi__err("outofmem", "Out of memory");

	// Filtering for low-bit-depth images
	if (depth < 8)
	{
		filter_bytes = 1;
		width = img_width_bytes;
	}

	for (j = 0; j < y; ++j)
	{
		// cur/prior filter buffers alternate
		stbi_uc *cur = filter_buf + (j & 1) * img_width_bytes;
		stbi_uc *prior = filter_buf + (~j & 1) * img_width_bytes;
		stbi_uc *dest = a->out + stride * j;
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
					cur[k] = STBI__BYTECAST(raw[k] + stbi__paeth(cur[k-filter_bytes], prior[k], prior[k-filter_bytes]));
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
			stbi_uc scale = (color == 0) ? stbi__depth_scale_table[depth] : 1; // scale grayscale values to 0..255 range
			stbi_uc *in = cur;
			stbi_uc *out = dest;
			stbi_uc inb = 0;
			stbi__uint32 nsmp = x * img_n;

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
				stbi__create_png_alpha_expand8(dest, dest, x, img_n);
		}
		else if (depth == 8)
		{
			if (img_n == out_n)
				memcpy(dest, cur, x * img_n);
			else
				stbi__create_png_alpha_expand8(dest, cur, x, img_n);
		}
		else if (depth == 16)
		{
			// convert the image data from big-endian to platform-native
			stbi__uint16 *dest16 = (stbi__uint16 *) dest;
			stbi__uint32 nsmp = x * img_n;

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

static int stbi__create_png_image(stbi__png *a, stbi_uc *image_data, stbi__uint32 image_data_len, int out_n, int depth,
                                  int color, int interlaced)
{
	int bytes = (depth == 16 ? 2 : 1);
	int out_bytes = out_n * bytes;
	stbi_uc *final;
	int p;
	if (!interlaced)
		return stbi__create_png_image_raw(a, image_data, image_data_len, out_n, a->s->img_x, a->s->img_y, depth, color);

	// de-interlacing
	final = (stbi_uc *) stbi__malloc_mad3(a->s->img_x, a->s->img_y, out_bytes, 0);
	if (!final) return stbi__err("outofmem", "Out of memory");
	for (p = 0; p < 7; ++p)
	{
		int xorig[] = {0, 4, 0, 2, 0, 1, 0};
		int yorig[] = {0, 0, 4, 0, 2, 0, 1};
		int xspc[] = {8, 8, 4, 4, 2, 2, 1};
		int yspc[] = {8, 8, 8, 4, 4, 2, 2};
		int i, j, x, y;
		// pass1_x[4] = 0, pass1_x[5] = 1, pass1_x[12] = 1
		x = (a->s->img_x - xorig[p] + xspc[p] - 1) / xspc[p];
		y = (a->s->img_y - yorig[p] + yspc[p] - 1) / yspc[p];
		if (x && y)
		{
			stbi__uint32 img_len = ((((a->s->img_n * x * depth) + 7) >> 3) + 1) * y;
			if (!stbi__create_png_image_raw(a, image_data, image_data_len, out_n, x, y, depth, color))
			{
				STBI_FREE(final);
				return 0;
			}
			for (j = 0; j < y; ++j)
			{
				for (i = 0; i < x; ++i)
				{
					int out_y = j * yspc[p] + yorig[p];
					int out_x = i * xspc[p] + xorig[p];
					memcpy(final + out_y * a->s->img_x * out_bytes + out_x * out_bytes,
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

static int stbi__compute_transparency(stbi__png *z, stbi_uc tc[3], int out_n)
{
	stbi__context *s = z->s;
	stbi__uint32 i, pixel_count = s->img_x * s->img_y;
	stbi_uc *p = z->out;

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

static int stbi__compute_transparency16(stbi__png *z, stbi__uint16 tc[3], int out_n)
{
	stbi__context *s = z->s;
	stbi__uint32 i, pixel_count = s->img_x * s->img_y;
	stbi__uint16 *p = (stbi__uint16 *) z->out;

	// compute color-based transparency, assuming we've
	// already got 65535 as the alpha value in the output
	STBI_ASSERT(out_n == 2 || out_n == 4);

	if (out_n == 2)
	{
		for (i = 0; i < pixel_count; ++i)
		{
			p[1] = (p[0] == tc[0] ? 0 : 65535);
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

static int stbi__expand_png_palette(stbi__png *a, stbi_uc *palette, int len, int pal_img_n)
{
	stbi__uint32 i, pixel_count = a->s->img_x * a->s->img_y;
	stbi_uc *p, *temp_out, *orig = a->out;

	p = (stbi_uc *) stbi__malloc_mad2(pixel_count, pal_img_n, 0);
	if (p == NULL) return stbi__err("outofmem", "Out of memory");

	// between here and free(out) below, exitting would leak
	temp_out = p;

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


#define STBI__PNG_TYPE(a,b,c,d)  (((unsigned) (a) << 24) + ((unsigned) (b) << 16) + ((unsigned) (c) << 8) + (unsigned) (d))

static int stbi__parse_png_file(stbi__png *z, int scan, int req_comp)
{
	stbi_uc palette[1024], pal_img_n = 0;
	stbi_uc has_trans = 0, tc[3] = {0};
	stbi__uint16 tc16[3];
	stbi__uint32 ioff = 0, idata_limit = 0, i, pal_len = 0;
	int first = 1, k, interlace = 0, color = 0, is_iphone = 0;
	stbi__context *s = z->s;

	z->expanded = NULL;
	z->idata = NULL;
	z->out = NULL;

	if (!stbi__check_png_header(s))
	{
		return 0;
	}

	if (scan == STBI__SCAN_type)
	{
		return 1;
	}

	for (;;)
	{
		stbi__pngchunk c = stbi__get_chunk_header(s);
		switch (c.type)
		{
			case STBI__PNG_TYPE('C', 'g', 'B', 'I'):
				is_iphone = 1;
				stbi__skip(s, c.length);
				break;
			case STBI__PNG_TYPE('I', 'H', 'D', 'R'): {
				int comp, filter;
				if (!first)
				{
					return stbi__err("multiple IHDR", "Corrupt PNG");
				}
				first = 0;
				if (c.length != 13) return stbi__err("bad IHDR len", "Corrupt PNG");
				s->img_x = stbi__get32be(s);
				s->img_y = stbi__get32be(s);
				if (s->img_y > STBI_MAX_DIMENSIONS)
				{
					return stbi__err("too large", "Very large image (corrupt?)");
				}
				if (s->img_x > STBI_MAX_DIMENSIONS)
				{
					return stbi__err("too large", "Very large image (corrupt?)");
				}
				z->depth = stbi__get8(s);
				if (z->depth != 1 && z->depth != 2 && z->depth != 4 && z->depth != 8 && z->depth != 16)
				{
					return stbi__err(
						"1/2/4/8/16-bit only", "PNG not supported: 1/2/4/8/16-bit only");
				}
				color = stbi__get8(s);
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
				comp = stbi__get8(s);
				if (comp)
				{
					return stbi__err("bad comp method", "Corrupt PNG");
				}
				filter = stbi__get8(s);
				if (filter)
				{
					return stbi__err("bad filter method", "Corrupt PNG");
				}
				interlace = stbi__get8(s);
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
					if ((1 << 30) / s->img_x / 4 < s->img_y) return stbi__err("too large", "Corrupt PNG");
				}
				else
				{
					s->img_n = (color & 2 ? 3 : 1) + (color & 4 ? 1 : 0);
					if ((1 << 30) / s->img_x / s->img_n < s->img_y) return stbi__err(
						"too large", "Image too large to decode");
				}
				// even with SCAN_header, have to scan to see if we have a tRNS
				break;
			}

			case STBI__PNG_TYPE('P', 'L', 'T', 'E'): {
				if (first) return stbi__err("first not IHDR", "Corrupt PNG");
				if (c.length > 256 * 3) return stbi__err("invalid PLTE", "Corrupt PNG");
				pal_len = c.length / 3;
				if (pal_len * 3 != c.length) return stbi__err("invalid PLTE", "Corrupt PNG");
				for (i = 0; i < pal_len; ++i)
				{
					palette[i * 4 + 0] = stbi__get8(s);
					palette[i * 4 + 1] = stbi__get8(s);
					palette[i * 4 + 2] = stbi__get8(s);
					palette[i * 4 + 3] = 255;
				}
				break;
			}

			case STBI__PNG_TYPE('t', 'R', 'N', 'S'): {
				if (first) return stbi__err("first not IHDR", "Corrupt PNG");
				if (z->idata) return stbi__err("tRNS after IDAT", "Corrupt PNG");
				if (pal_img_n)
				{
					if (scan == STBI__SCAN_header)
					{
						s->img_n = 4;
						return 1;
					}
					if (pal_len == 0) return stbi__err("tRNS before PLTE", "Corrupt PNG");
					if (c.length > pal_len) return stbi__err("bad tRNS len", "Corrupt PNG");
					pal_img_n = 4;
					for (i = 0; i < c.length; ++i)
						palette[i * 4 + 3] = stbi__get8(s);
				}
				else
				{
					if (!(s->img_n & 1)) return stbi__err("tRNS with alpha", "Corrupt PNG");
					if (c.length != (stbi__uint32) s->img_n * 2) return stbi__err("bad tRNS len", "Corrupt PNG");
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
							tc16[k] = (stbi__uint16) stbi__get16be(s); // copy the values as-is
					}
					else
					{
						for (k = 0; k < s->img_n && k < 3; ++k)
							tc[k] = (stbi_uc) (stbi__get16be(s) & 255) * stbi__depth_scale_table[z->depth];
						// non 8-bit images will be larger
					}
				}
				break;
			}

			case STBI__PNG_TYPE('I', 'D', 'A', 'T'): {
				if (first) return stbi__err("first not IHDR", "Corrupt PNG");
				if (pal_img_n && !pal_len) return stbi__err("no PLTE", "Corrupt PNG");
				if (scan == STBI__SCAN_header)
				{
					// header scan definitely stops at first IDAT
					if (pal_img_n)
						s->img_n = pal_img_n;
					return 1;
				}
				if (c.length > (1u << 30)) return stbi__err("IDAT size limit", "IDAT section larger than 2^30 bytes");
				if ((int) (ioff + c.length) < (int) ioff) return 0;
				if (ioff + c.length > idata_limit)
				{
					stbi__uint32 idata_limit_old = idata_limit;
					stbi_uc *p;
					if (idata_limit == 0) idata_limit = c.length > 4096 ? c.length : 4096;
					while (ioff + c.length > idata_limit)
						idata_limit *= 2;
					STBI_NOTUSED(idata_limit_old);
					p = (stbi_uc *) STBI_REALLOC_SIZED(z->idata, idata_limit_old, idata_limit);
					if (p == NULL) return stbi__err("outofmem", "Out of memory");
					z->idata = p;
				}
				if (!stbi__getn(s, z->idata + ioff, c.length)) return stbi__err("outofdata", "Corrupt PNG");
				ioff += c.length;
				break;
			}

			case STBI__PNG_TYPE('I', 'E', 'N', 'D'): {
				stbi__uint32 raw_len, bpl;
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

				zctx.malloc  = &stbi__malloc;
				zctx.free    = [](void* p) { STBI_FREE(p); };
				zctx.realloc = [](void* p, size_t olds, size_t news) { return STBI_REALLOC_SIZED(p, olds, news); };

				try
				{
					z->expanded = zctx.decode_malloc_guesssize_headerflag();
				}
				catch (Zlib::Er er)
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
					s->img_out_n = s->img_n + 1;
				else
					s->img_out_n = s->img_n;
				if (!stbi__create_png_image(z, z->expanded, raw_len, s->img_out_n, z->depth, color, interlace)) return 0;
				if (has_trans)
				{
					if (z->depth == 16)
					{
						if (!stbi__compute_transparency16(z, tc16, s->img_out_n)) return 0;
					}
					else
					{
						if (!stbi__compute_transparency(z, tc, s->img_out_n)) return 0;
					}
				}
				if (pal_img_n)
				{
					// pal_img_n == 3 or 4
					s->img_n = pal_img_n; // record the actual colors we had
					s->img_out_n = pal_img_n;
					if (req_comp >= 3) s->img_out_n = req_comp;
					if (!stbi__expand_png_palette(z, palette, pal_len, s->img_out_n))
						return 0;
				}
				else if (has_trans)
				{
					// non-paletted image with tRNS -> source image has (constant) alpha
					++s->img_n;
				}
				STBI_FREE(z->expanded);
				z->expanded = NULL;
				// end of PNG chunk, read and skip CRC
				stbi__get32be(s);
				return 1;
			}

			default:
				// if critical, fail
				if (first) return stbi__err("first not IHDR", "Corrupt PNG");
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
				stbi__skip(s, c.length);
				break;
		}
		// end of PNG chunk, read and skip CRC
		stbi__get32be(s);
	}
}

static void *stbi__do_png(stbi__png *p, int *x, int *y, int *n, int req_comp, stbi__result_info *ri)
{
	void *result = NULL;
	if (req_comp < 0 || req_comp > 4) return stbi__errpuc("bad req_comp", "Internal error");
	if (stbi__parse_png_file(p, STBI__SCAN_load, req_comp))
	{
		if (p->depth <= 8)
			ri->bits_per_channel = 8;
		else if (p->depth == 16)
			ri->bits_per_channel = 16;
		else
			return stbi__errpuc("bad bits_per_channel", "PNG not supported: unsupported color depth");
		result = p->out;
		p->out = NULL;
		if (req_comp && req_comp != p->s->img_out_n)
		{
			if (ri->bits_per_channel == 8)
				result = stbi__convert_format((unsigned char *) result, p->s->img_out_n, req_comp, p->s->img_x,
				                              p->s->img_y);
			else
				result = stbi__convert_format16((stbi__uint16 *) result, p->s->img_out_n, req_comp, p->s->img_x,
				                                p->s->img_y);
			p->s->img_out_n = req_comp;
			if (result == NULL) return result;
		}
		*x = p->s->img_x;
		*y = p->s->img_y;
		if (n) *n = p->s->img_n;
	}
	STBI_FREE(p->out);
	p->out = NULL;
	STBI_FREE(p->expanded);
	p->expanded = NULL;
	STBI_FREE(p->idata);
	p->idata = NULL;

	return result;
}

static void *stbi__png_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
	stbi__png p;
	p.s = s;
	return stbi__do_png(&p, x, y, comp, req_comp, ri);
}

static int stbi__png_test(stbi__context *s)
{
	int r;
	r = stbi__check_png_header(s);
	stbi__rewind(s);
	return r;
}

static int stbi__png_info_raw(stbi__png *p, int *x, int *y, int *comp)
{
	if (!stbi__parse_png_file(p, STBI__SCAN_header, 0))
	{
		stbi__rewind(p->s);
		return 0;
	}
	if (x) *x = p->s->img_x;
	if (y) *y = p->s->img_y;
	if (comp) *comp = p->s->img_n;
	return 1;
}

static int stbi__png_info(stbi__context *s, int *x, int *y, int *comp)
{
	stbi__png p;
	p.s = s;
	return stbi__png_info_raw(&p, x, y, comp);
}

static int stbi__png_is16(stbi__context *s)
{
	stbi__png p;
	p.s = s;
	if (!stbi__png_info_raw(&p, NULL, NULL, NULL))
		return 0;
	if (p.depth != 16)
	{
		stbi__rewind(p.s);
		return 0;
	}
	return 1;
}


static int stbi__info_main(stbi__context *s, int *x, int *y, int *comp)
{

	if (stbi__png_info(s, x, y, comp)) return 1;


	return stbi__err("unknown image type", "Image not of any known type, or corrupt");
}

static int stbi__is_16_main(stbi__context *s)
{
	if (stbi__png_is16(s)) return 1;
	return 0;
}


STBIDEF int stbi_info_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp)
{
	stbi__context s;
	stbi__start_mem(&s, buffer, len);
	return stbi__info_main(&s, x, y, comp);
}

STBIDEF int stbi_info_from_callbacks(stbi_io_callbacks const *c, void *user, int *x, int *y, int *comp)
{
	stbi__context s;
	stbi__start_callbacks(&s, (stbi_io_callbacks *) c, user);
	return stbi__info_main(&s, x, y, comp);
}

STBIDEF int stbi_is_16_bit_from_memory(stbi_uc const *buffer, int len)
{
	stbi__context s;
	stbi__start_mem(&s, buffer, len);
	return stbi__is_16_main(&s);
}

STBIDEF int stbi_is_16_bit_from_callbacks(stbi_io_callbacks const *c, void *user)
{
	stbi__context s;
	stbi__start_callbacks(&s, (stbi_io_callbacks *) c, user);
	return stbi__is_16_main(&s);
}
