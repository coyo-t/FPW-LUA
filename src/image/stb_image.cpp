
#include "stb_image.hpp"

// public domain "baseline" PNG decoder   v0.10  Sean Barrett 2006-11-18
//    simple implementation
//      - only 8-bit samples
//      - no CRC checking
//      - allocates lots of intermediate memory
//        - avoids problem of streaming data between subsystems
//        - avoids explicit window management
//    performance
//      - uses stb_zlib, a PD zlib implementation with fast huffman decoding

#include <cstdlib>
#include <cstring>

#include "coyote/zlib.hpp"
using Coyote::Huffman;
using Coyote::Huffer;

#ifndef STBI_ASSERT
#include <cassert>
#define STBI_ASSERT(x) assert(x)
#endif

using Coyote::U16;
using Coyote::U32;

// should produce compiler error if size is wrong
static_assert(sizeof(U32) == 4);

#ifndef STBI_MALLOC
#define STBI_MALLOC(sz)           malloc(sz)
#define STBI_REALLOC(p,newsz)     realloc(p,newsz)
#define STBI_FREE(p)              free(p)
#endif

#ifndef STBI_REALLOC_SIZED
#define STBI_REALLOC_SIZED(p,oldsz,newsz) STBI_REALLOC(p,newsz)
#endif

static
const char *stbi__g_failure_reason;

#ifndef STBI_NO_FAILURE_STRINGS
static int stbi__err(const char *str)
{
	stbi__g_failure_reason = str;
	return 0;
}
#endif



// stbi__err - error
// stbi__errpuc - error returning pointer to unsigned char


#define stbi__err(x,y)  stbi__err(x)
#define stbi__errpuc(x,y)  ((unsigned char *)(size_t) (stbi__err(x,y)?NULL:NULL))


static constexpr auto STBI_MAX_DIMENSIONS = 1 << 24;


struct ResultInfo
{
	int bits_per_channel;
	int num_channels;
	int channel_order;
};

struct PngChunk
{
	U32 length;
	U32 type;
};


///////////////////////////////////////////////
//
//  stbi__context struct and start_xxx functions

// stbi__context structure is our basic context used by all images, so it
// contains all the IO context, plus some basic image information
struct Context
{
	U32 img_x;
	U32 img_y;
	int img_n;
	int img_out_n;

	Byte* img_buffer;
	Byte* img_buffer_end;
	Byte* img_buffer_original;
	Byte* img_buffer_original_end;

	// initialize a memory-decode context
	auto start_mem(Byte const *buffer, int len) -> void
	{
		img_buffer = img_buffer_original = (Byte*) buffer;
		img_buffer_end = img_buffer_original_end = (Byte*) buffer + len;
	}
	auto rewind() -> void
	{
		// conceptually rewind SHOULD rewind to the beginning of the stream,
		// but we just rewind to the beginning of the initial buffer, because
		// we only use it after doing 'test', which only ever looks at at most 92 bytes
		img_buffer = img_buffer_original;
		img_buffer_end = img_buffer_original_end;
	}
	auto readu8() -> Byte
	{
		if (img_buffer < img_buffer_end)
		{
			return *img_buffer++;
		}
		return 0;
	}
	auto readu16be () -> U16
	{
		int z = static_cast<U16>(readu8());
		return (z << 8) + static_cast<U16>(readu8());
	}
	auto readu32be () -> U32
	{
		auto z = static_cast<U32>(readu16be());
		return (z << 16) + static_cast<U32>(readu16be());
	}
	auto check_png_header() -> bool
	{
		static const Byte png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
		for (const auto i : png_sig)
		{
			if (readu8() != i)
			{
				return stbi__err("bad png sig", "Not a PNG");
			}
		}
		return true;
	}
};

struct Png
{
	Context *s;
	Byte* idata;
	Byte* expanded;
	Byte* out;
	int depth;
};


static void *stbi_malloc(size_t size)
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
static bool stbi_addsizes_valid(int a, int b)
{
	// now 0 <= b <= INT_MAX, hence also
	// 0 <= INT_MAX - b <= INTMAX.
	// And "a + b <= INT_MAX" (which might overflow) is the
	// same as a <= INT_MAX - b (no overflow)
	return b >= 0 && a <= INT_MAX - b;
}

// returns 1 if the product is valid, 0 on overflow.
// negative factors are considered invalid.
static bool stbi_mul2sizes_valid(int a, int b)
{
	if (a < 0 || b < 0)
	{
		return false;
	}
	if (b == 0)
	{
		// mul-by-0 is always safe
		return true;
	}
	// portable way to check for no overflows in a*b
	return a <= INT_MAX / b;
}

// returns 1 if "a*b + add" has no negative terms/factors and doesn't overflow
static bool stbi_mad2sizes_valid(int a, int b, int add)
{
	return stbi_mul2sizes_valid(a, b) && stbi_addsizes_valid(a * b, add);
}

// returns 1 if "a*b*c + add" has no negative terms/factors and doesn't overflow
static bool stbi_mad3sizes_valid(int a, int b, int c, int add)
{
	return stbi_mul2sizes_valid(a, b) &&
		stbi_mul2sizes_valid(a * b, c) &&
		stbi_addsizes_valid(a * b * c, add);
}

// mallocs with size overflow checking
template<typename T>
static T*stbi_malloc_fma2(int a, int b, int add)
{
	if (!stbi_mad2sizes_valid(a, b, add))
		return nullptr;
	return static_cast<T*>(stbi_malloc(a * b + add));
}

template<typename T>
static T* stbi_malloc_fma3(int a, int b, int c, int add)
{
	if (!stbi_mad3sizes_valid(a, b, c, add))
		return nullptr;
	return static_cast<T*>(stbi_malloc(a * b * c + add));
}


enum class Scan
{
	Load,
	Type,
	Header,
};

static Byte* stbi_convert_format (
	Byte* data,
	int img_n,
	int req_comp,
	U32 x,
	U32 y)
{
	int i, j;

	if (req_comp == img_n)
	{
		return data;
	}
	STBI_ASSERT(req_comp >= 1 && req_comp <= 4);

	const auto good = stbi_malloc_fma3<Byte>(req_comp, x, y, 0);
	if (good == nullptr)
	{
		STBI_FREE(data);
		return stbi__errpuc("outofmem", "Out of memory");
	}

	static auto stbi__compute_y = [](int r, int g, int b) -> Byte
	{
		return static_cast<Byte>(((r * 77) + (g * 150) + (29 * b)) >> 8);
	};

	for (j = 0; j < (int) y; ++j)
	{
		auto src = data + j * x * img_n;
		auto dest = good + j * x * req_comp;

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


static U16 *stbi_convert_format16(U16*data, int img_n, int req_comp, unsigned int x, unsigned int y)
{
	if (req_comp == img_n)
	{
		return data;
	}
	STBI_ASSERT(req_comp >= 1 && req_comp <= 4);

	const auto good = static_cast<U16*>(stbi_malloc(req_comp * x * y * 2));
	if (good == nullptr)
	{
		STBI_FREE(data);
		return (U16*)stbi__errpuc("outofmem", "Out of memory");
	}

	static auto compute_y_16 = [](int r, int g, int b) -> U16
	{
		return static_cast<U16>(((r * 77) + (g * 150) + (29 * b)) >> 8);
	};

	int i;
	for (int j = 0; j < static_cast<int>(y); ++j)
	{
		auto* src = data + j * x * img_n;
		auto* dest = good + j * x * req_comp;

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
			STBI__CASE(3, 1) { dest[0] = compute_y_16(src[0], src[1], src[2]); }
				break;
			STBI__CASE(3, 2)
				{
					dest[0] = compute_y_16(src[0], src[1], src[2]);
					dest[1] = 0xffff;
				}
				break;
			STBI__CASE(4, 1) { dest[0] = compute_y_16(src[0], src[1], src[2]); }
				break;
			STBI__CASE(4, 2)
				{
					dest[0] = compute_y_16(src[0], src[1], src[2]);
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
				return (U16*) stbi__errpuc("unsupported", "Unsupported format conversion");
		}
#undef STBI__CASE
	}

	STBI_FREE(data);
	return good;
}

enum class Filter
{
	None = 0,
	Sub = 1,
	Up = 2,
	Avg = 3,
	Paeth = 4,
	AvgFirst,

};

static auto first_row_filter[5] = {
	Filter::None,
	Filter::Sub,
	Filter::None,
	Filter::AvgFirst,
	// Paeth with b=c=0 turns out to be equivalent to sub
	Filter::Sub
};

static int stbi_paeth(int a, int b, int c)
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

static const Byte depth_scale_table[9] = {
	0, 0xff, 0x55,
	0, 0x11, 0,
	0, 0,    0x01
};

// adds an extra all-255 alpha channel
// dest == src is legal
// img_n must be 1 or 3
static void stbi_create_png_alpha_expand8(Byte*dest, Byte*src, U32 x, int img_n)
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
static int stbi_create_png_image_raw(Png *a, Byte* raw, U32 raw_len, int out_n, U32 x, U32 y, int depth, int color)
{
	int bytes = (depth == 16 ? 2 : 1);
	Context *s = a->s;
	U32 i;
	const U32 stride = x * out_n * bytes;
	int all_ok = 1;
	int k;
	int img_n = s->img_n; // copy it into a local for later

	int output_bytes = out_n * bytes;
	int filter_bytes = img_n * bytes;
	int width = x;

	STBI_ASSERT(out_n == s->img_n || out_n == s->img_n+1);
	// extra bytes to write off the end into
	a->out = stbi_malloc_fma3<Byte>(x, y, output_bytes, 0);
	if (a->out == nullptr)
	{
		return stbi__err("outofmem", "Out of memory");
	}

	// note: error exits here don't need to clean up a->out individually,
	// stbi__do_png always does on error.
	if (!stbi_mad3sizes_valid(img_n, x, depth, 7))
	{
		return stbi__err("too large", "Corrupt PNG");
	}
	U32 img_width_bytes = (((img_n * x * depth) + 7) >> 3);
	if (!stbi_mad2sizes_valid(img_width_bytes, y, img_width_bytes))
	{
		return stbi__err("too large", "Corrupt PNG");
	}
	U32 img_len = (img_width_bytes + 1) * y;

	// we used to check for exact match between raw_len and img_len on non-interlaced PNGs,
	// but issue #276 reported a PNG in the wild that had extra data at the end (all zeros),
	// so just check for raw_len < img_len always.
	if (raw_len < img_len)
	{
		return stbi__err("not enough pixels", "Corrupt PNG");
	}

	// Allocate two scan lines worth of filter workspace buffer.
	auto *filter_buf = stbi_malloc_fma2<Byte>(img_width_bytes, 2, 0);
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

	static auto BYTECAST = [](Coyote::S64 x) {return static_cast<Byte>((x) & 255); };


	for (U32 j = 0; j < y; ++j)
	{
		// cur/prior filter buffers alternate
		auto *cur = filter_buf + (j & 1) * img_width_bytes;
		auto *prior = filter_buf + (~j & 1) * img_width_bytes;
		auto *dest = a->out + stride * j;
		int nk = width * filter_bytes;
		int filter = *raw++;

		// check filter type
		if (filter > 4)
		{
			all_ok = stbi__err("invalid filter", "Corrupt PNG");
			break;
		}

		// if first row, use special filter that doesn't sample previous row
		Filter targetFilter;
		if (j == 0)
		{
			targetFilter = first_row_filter[filter];
		}
		else
		{
			targetFilter = static_cast<Filter>(filter);
		}

		// perform actual filtering
		switch (targetFilter)
		{
			case Filter::None:
				memcpy(cur, raw, nk);
				break;
			case Filter::Sub:
				memcpy(cur, raw, filter_bytes);
				for (k = filter_bytes; k < nk; ++k)
				{
					cur[k] = BYTECAST(raw[k] + cur[k-filter_bytes]);
				}
				break;
			case Filter::Up:
				for (k = 0; k < nk; ++k)
				{
					cur[k] = BYTECAST(raw[k] + prior[k]);
				}
				break;
			case Filter::Avg:
				for (k = 0; k < filter_bytes; ++k)
				{
					cur[k] = BYTECAST(raw[k] + (prior[k]>>1));
				}
				for (k = filter_bytes; k < nk; ++k)
				{
					cur[k] = BYTECAST(raw[k] + ((prior[k] + cur[k-filter_bytes])>>1));
				}
				break;
			case Filter::Paeth:
				for (k = 0; k < filter_bytes; ++k)
				{
					// prior[k] == stbi__paeth(0,prior[k],0)
					cur[k] = BYTECAST(raw[k] + prior[k]);
				}
				for (k = filter_bytes; k < nk; ++k)
				{
					cur[k] = BYTECAST(raw[k] + stbi_paeth(cur[k-filter_bytes], prior[k], prior[k-filter_bytes]));
				}
				break;
			case Filter::AvgFirst:
				memcpy(cur, raw, filter_bytes);
				for (k = filter_bytes; k < nk; ++k)
				{
					cur[k] = BYTECAST(raw[k] + (cur[k-filter_bytes] >> 1));
				}
				break;
		}

		raw += nk;

		// expand decoded bits in cur to dest, also adding an extra alpha channel if desired
		if (depth < 8)
		{
			// scale grayscale values to 0..255 range
			Byte scale = (color == 0) ? depth_scale_table[depth] : 1;
			auto* in = cur;
			auto* out = dest;
			Byte inb = 0;
			U32 nsmp = x * img_n;

			// expand bits to bytes first
			if (depth == 4)
			{
				for (i = 0; i < nsmp; ++i)
				{
					if ((i & 1) == 0)
					{
						inb = *in++;
					}
					*out++ = scale * (inb >> 4);
					inb <<= 4;
				}
			}
			else if (depth == 2)
			{
				for (i = 0; i < nsmp; ++i)
				{
					if ((i & 3) == 0)
					{
						inb = *in++;
					}
					*out++ = scale * (inb >> 6);
					inb <<= 2;
				}
			}
			else
			{
				STBI_ASSERT(depth == 1);
				for (i = 0; i < nsmp; ++i)
				{
					if ((i & 7) == 0)
					{
						inb = *in++;
					}
					*out++ = scale * (inb >> 7);
					inb <<= 1;
				}
			}

			// insert alpha=255 values if desired
			if (img_n != out_n)
			{
				stbi_create_png_alpha_expand8(dest, dest, x, img_n);
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
				stbi_create_png_alpha_expand8(dest, cur, x, img_n);
			}
		}
		else if (depth == 16)
		{
			// convert the image data from big-endian to platform-native
			auto *dest16 = reinterpret_cast<U16*>(dest);
			U32 nsmp = x * img_n;

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
	if (!all_ok)
	{
		return 0;
	}

	return 1;
}

static int stbi_create_png_image(Png *a, Byte*image_data, U32 image_data_len, int out_n, int depth, int color, int interlaced)
{
	int bytes = (depth == 16 ? 2 : 1);
	int out_bytes = out_n * bytes;
	if (!interlaced)
	{
		return stbi_create_png_image_raw(
			a,
			image_data,
			image_data_len,
			out_n,
			a->s->img_x,
			a->s->img_y,
			depth,
			color
		);
	}

	// de-interlacing
	const auto final = stbi_malloc_fma3<Byte>(a->s->img_x, a->s->img_y, out_bytes, 0);
	if (!final)
	{
		return stbi__err("outofmem", "Out of memory");
	}
	for (int p = 0; p < 7; ++p)
	{
		const int xorig[] = {0, 4, 0, 2, 0, 1, 0};
		const int yorig[] = {0, 0, 4, 0, 2, 0, 1};
		const int xspc[] = {8, 8, 4, 4, 2, 2, 1};
		const int yspc[] = {8, 8, 8, 4, 4, 2, 2};
		int i, j, x, y;
		// pass1_x[4] = 0, pass1_x[5] = 1, pass1_x[12] = 1
		x = (a->s->img_x - xorig[p] + xspc[p] - 1) / xspc[p];
		y = (a->s->img_y - yorig[p] + yspc[p] - 1) / yspc[p];
		if (x && y)
		{
			U32 img_len = ((((a->s->img_n * x * depth) + 7) >> 3) + 1) * y;
			if (!stbi_create_png_image_raw(a, image_data, image_data_len, out_n, x, y, depth, color))
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

static int stbi_parse_png_file(Png *z, Scan scan, int req_comp)
{
	static constexpr auto FOURCC = [](char a, char b, char c, char d) -> U32
	{
		return (
			(static_cast<U32>(a) << 24) |
			(static_cast<U32>(b) << 16) |
			(static_cast<U32>(c) << 8) |
			static_cast<U32>(d)
		);
	};
	Byte palette[1024];
	Byte pal_img_n = 0;
	Byte has_trans = 0;
	Byte tc[3] = {0};
	U16 tc16[3];
	U32 ioff = 0;
	U32 idata_limit = 0;
	U32 i;
	U32 pal_len = 0;
	int first = 1;
	int k;
	int interlace = 0;
	int color = 0;
	Context *s = z->s;

	z->expanded = nullptr;
	z->idata = nullptr;
	z->out = nullptr;

	if (!s->check_png_header())
	{
		return 0;
	}

	while (true)
	{
		PngChunk c;
		c.length = s->readu32be();
		c.type = s->readu32be();
		switch (c.type)
		{
			case FOURCC('I', 'H', 'D', 'R'): {
				if (!first)
				{
					return stbi__err("multiple IHDR", "Corrupt PNG");
				}
				first = 0;
				if (c.length != 13)
				{
					return stbi__err("bad IHDR len", "Corrupt PNG");
				}
				s->img_x = s->readu32be();
				s->img_y = s->readu32be();
				if (s->img_y > STBI_MAX_DIMENSIONS)
				{
					return stbi__err("too large", "Very large image (corrupt?)");
				}
				if (s->img_x > STBI_MAX_DIMENSIONS)
				{
					return stbi__err("too large", "Very large image (corrupt?)");
				}
				z->depth = s->readu8();
				if (z->depth != 1 && z->depth != 2 && z->depth != 4 && z->depth != 8 && z->depth != 16)
				{
					return stbi__err("1/2/4/8/16-bit only", "PNG not supported: 1/2/4/8/16-bit only");
				}
				color = s->readu8();
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
				if (int comp = s->readu8())
				{
					return stbi__err("bad comp method", "Corrupt PNG");
				}
				if (int filter = s->readu8())
					return stbi__err("bad filter method", "Corrupt PNG");
				interlace = s->readu8();
				if (interlace > 1)
				{
					return stbi__err("bad interlace method", "Corrupt PNG");
				}
				if (!s->img_x || !s->img_y)
				{
					return stbi__err("0-pixel image", "Corrupt PNG");
				}
				if (!pal_img_n)
				{
					s->img_n = (color & 2 ? 3 : 1) + (color & 4 ? 1 : 0);
					if ((1 << 30) / s->img_x / s->img_n < s->img_y)
					{
						return stbi__err("too large", "Image too large to decode");
					}
				}
				else
				{
					// if paletted, then pal_n is our final components, and
					// img_n is # components to decompress/filter.
					s->img_n = 1;
					if ((1 << 30) / s->img_x / 4 < s->img_y)
					{
						return stbi__err("too large", "Corrupt PNG");
					}
				}
				// even with SCAN_header, have to scan to see if we have a tRNS
				break;
			}

			case FOURCC('P', 'L', 'T', 'E'): {
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
					palette[i * 4 + 0] = s->readu8();
					palette[i * 4 + 1] = s->readu8();
					palette[i * 4 + 2] = s->readu8();
					palette[i * 4 + 3] = 255;
				}
				break;
			}

			case FOURCC('t', 'R', 'N', 'S'): {
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
					if (scan == Scan::Header)
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
						palette[i * 4 + 3] = s->readu8();
					}
				}
				else
				{
					if (!(s->img_n & 1))
					{
						return stbi__err("tRNS with alpha", "Corrupt PNG");
					}
					if (c.length != static_cast<U32>(s->img_n) * 2)
					{
						return stbi__err("bad tRNS len", "Corrupt PNG");
					}
					has_trans = 1;
					// non-paletted with tRNS = constant alpha. if header-scanning, we can stop now.
					if (scan == Scan::Header)
					{
						++s->img_n;
						return 1;
					}
					if (z->depth == 16)
					{
						// extra loop test to suppress false GCC warning
						for (k = 0; k < s->img_n && k < 3; ++k)
						{
							// copy the values as-is
							tc16[k] = s->readu16be();
						}
					}
					else
					{
						for (k = 0; k < s->img_n && k < 3; ++k)
						{
							tc[k] = static_cast<Byte>(s->readu16be() & 255) * depth_scale_table[z->depth];
						}
						// non 8-bit images will be larger
					}
				}
				break;
			}

			case FOURCC('I', 'D', 'A', 'T'): {
				if (first)
				{
					return stbi__err("first not IHDR", "Corrupt PNG");
				}
				if (pal_img_n && !pal_len)
				{
					return stbi__err("no PLTE", "Corrupt PNG");
				}
				if (scan == Scan::Header)
				{
					// header scan definitely stops at first IDAT
					if (pal_img_n)
					{
						s->img_n = pal_img_n;
					}
					return true;
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
					if (idata_limit == 0)
					{
						idata_limit = c.length > 4096 ? c.length : 4096;
					}
					while (ioff + c.length > idata_limit)
					{
						idata_limit *= 2;
					}
					auto *p = static_cast<Byte*>(STBI_REALLOC_SIZED(z->idata, idata_limit_old, idata_limit));
					if (p == nullptr)
					{
						return stbi__err("outofmem", "Out of memory");
					}
					z->idata = p;
				}
				{
					bool res;
					const auto buffer = z->idata + ioff;
					if (const auto n = c.length; s->img_buffer + n > s->img_buffer_end)
					{
						res = true;
					}
					else
					{
						memcpy(buffer, s->img_buffer, n);
						s->img_buffer += n;
						res = false;
					}
					if (res)
					{
						return stbi__err("outofdata", "Corrupt PNG");
					}
				}
				ioff += c.length;
				break;
			}

			case FOURCC('I', 'E', 'N', 'D'): {
				U32 raw_len;
				if (first)
				{
					return stbi__err("first not IHDR", "Corrupt PNG");
				}
				if (scan != Scan::Load)
				{
					return 1;
				}
				if (z->idata == nullptr)
				{
					return stbi__err("no IDAT", "Corrupt PNG");
				}
				// initial guess for decoded data size to avoid unnecessary reallocs
				// bytes per line, per component
				U32 bpl = (s->img_x * z->depth + 7) / 8;
				raw_len = bpl * s->img_y * s->img_n /* pixels */ + s->img_y /* filter mode per row */;
				z->expanded = Huffer::decode_malloc_guesssize_headerflag(
					z->idata,
					ioff,
					raw_len,
					reinterpret_cast<int *>(&raw_len),
					true
				);
				if (z->expanded == nullptr)
				{
					// zlib should set error
					return false;
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
				if (!stbi_create_png_image(z, z->expanded, raw_len, s->img_out_n, z->depth, color, interlace))
				{
					return 0;
				}
				if (has_trans)
				{
					if (z->depth == 16)
					{
						Context *s2 = z->s;
						U32 i2, pc2 = s2->img_x * s2->img_y;
						auto *p = reinterpret_cast<U16*>(z->out);
						auto out_n = s2->img_out_n;

						// compute color-based transparency, assuming we've
						// already got 65535 as the alpha value in the output
						if (out_n == 2)
						{
							for (i2 = 0; i2 < pc2; ++i2)
							{
								p[1] = (p[0] == tc[0] ? 0 : 65535);
								p += 2;
							}
						}
						else if (out_n == 4)
						{
							for (i2 = 0; i2 < pc2; ++i2)
							{
								if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
								{
									p[3] = 0;
								}
								p += 4;
							}
						}
						else
						{
							return false;
						}
					}
					else
					{
						Context *s2 = z->s;
						U32 i2;
						U32 pc2 = s2->img_x * s2->img_y;
						auto* p = z->out;
						auto out_n = s2->img_out_n;

						// compute color-based transparency, assuming we've
						// already got 255 as the alpha value in the output

						if (out_n == 2)
						{
							for (i2 = 0; i2 < pc2; ++i2)
							{
								p[1] = (p[0] == tc[0] ? 0 : 255);
								p += 2;
							}
						}
						else if (out_n == 4)
						{
							for (i2 = 0; i2 < pc2; ++i2)
							{
								if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
									p[3] = 0;
								p += 4;
							}
						}
						else
						{
							return false;
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

					bool res;
					{
						auto* a = z;
						auto palimgn = s->img_out_n;
						U32 ii;
						U32 pc = a->s->img_x * a->s->img_y;
						const auto *orig1 = a->out;

						auto *temp_out = stbi_malloc_fma2<Byte>(pc, palimgn, 0);
						if (temp_out == nullptr)
						{
							res = stbi__err("outofmem", "Out of memory");
							goto ret;
						}

						// between here and free(out) below, exiting would leak
						auto *p = temp_out;

						if (palimgn == 3)
						{
							for (ii = 0; ii < pc; ++ii)
							{
								int n = orig1[ii] * 4;
								p[0] = palette[n];
								p[1] = palette[n + 1];
								p[2] = palette[n + 2];
								p += 3;
							}
						}
						else
						{
							for (ii = 0; ii < pc; ++ii)
							{
								int n = orig1[ii] * 4;
								p[0] = palette[n];
								p[1] = palette[n + 1];
								p[2] = palette[n + 2];
								p[3] = palette[n + 3];
								p += 4;
							}
						}
						STBI_FREE(a->out);
						a->out = temp_out;

						res = true;
						ret:
					}

					if (!res)
					{
						return false;
					}
				}
				else if (has_trans)
				{
					// non-paletted image with tRNS -> source image has (constant) alpha
					++s->img_n;
				}
				STBI_FREE(z->expanded);
				z->expanded = nullptr;
				// end of PNG chunk, read and skip CRC
				s->readu32be();
				return true;
			}

			default: {
				// if critical, fail
				if (first)
				{
					return stbi__err("first not IHDR", "Corrupt PNG");
				}
				if ((c.type & (1 << 29)) == 0)
				{
					// not threadsafe
					static char invalid_chunk[] = "XXXX PNG chunk not known";
					return stbi__err(invalid_chunk, "PNG not supported: unknown PNG chunk type");
				}

				if (const auto n = c.length; n != 0)
				{
					if (n < 0)
					{
						s->img_buffer = s->img_buffer_end;
					}
					else
					{
						s->img_buffer += n;
					}
				}

				break;
			}
		}
		// end of PNG chunk, read and skip CRC
		s->readu32be();
	}
}

const char *coyote_stbi_failure_reason()
{
	return stbi__g_failure_reason;
}

void coyote_stbi_image_free(void *retval_from_stbi_load)
{
	STBI_FREE(retval_from_stbi_load);
}

U64 coyote_stbi_info_from_memory(
	Byte const *source_buffer,
	U64 source_buffer_size,
	U64* out_x,
	U64* out_y,
	U64* channels_in_file)
{
	Context s;
	s.start_mem(source_buffer, source_buffer_size);
	Png p;
	p.s = &s;
	if (!stbi_parse_png_file(&p, Scan::Header, 0))
	{
		p.s->rewind();
		return stbi__err("unknown image type", "Image not of any known type, or corrupt");
	}
	if (out_x != nullptr)
	{
		*out_x = p.s->img_x;
	}
	if (out_y != nullptr)
	{
		*out_y = p.s->img_y;
	}
	if (channels_in_file != nullptr)
	{
		*channels_in_file = p.s->img_n;
	}
	return true;
}

Byte *coyote_stbi_load_from_memory(
	Byte const *source_buffer,
	U64 source_buffer_size,
	U64 *out_x,
	U64 *out_y,
	U64 *out_comp)
{
	Context s;
	s.start_mem(source_buffer, source_buffer_size);
	ResultInfo ri;

	void* result;
	const auto req_comp = 4;
	{
		memset(&ri, 0, sizeof(ri)); // make sure it's initialized if we add new fields
		ri.bits_per_channel = 8; // default is 8 so most paths don't have to be changed
		ri.num_channels = 0;

		// test the formats with a very explicit header first (at least a FOURCC
		// or distinctive magic number first)

		{
			const auto r = s.check_png_header();
			s.rewind();
			if (!r)
			{
				result = stbi__errpuc("unknown image type", "Image not of any known type, or corrupt");
				goto ret;
			}
		}

		Png p;
		p.s = &s;

		void *result2 = nullptr;
		if (stbi_parse_png_file(&p, Scan::Load, req_comp))
		{
			if (p.depth <= 8)
			{
				ri.bits_per_channel = 8;
			}
			else if (p.depth == 16)
			{
				ri.bits_per_channel = 16;
			}
			else
			{
				result = stbi__errpuc("bad bits_per_channel", "PNG not supported: unsupported color depth");
				goto ret;
			}
			result2 = p.out;
			p.out = nullptr;
			if (req_comp != p.s->img_out_n)
			{
				if (ri.bits_per_channel == 8)
				{
					result2 = stbi_convert_format(static_cast<Byte*>(result2), p.s->img_out_n, req_comp, p.s->img_x, p.s->img_y);
				}
				else
				{
					result2 = stbi_convert_format16(static_cast<U16*>(result2), p.s->img_out_n, req_comp, p.s->img_x, p.s->img_y);
				}
				p.s->img_out_n = req_comp;
				if (result2 == nullptr)
				{
					result = result2;
					goto ret;
				}
			}
			if (out_x != nullptr)
			{
				*out_x = p.s->img_x;
			}
			if (out_y != nullptr)
			{
				*out_y = p.s->img_y;
			}
			if (out_comp != nullptr)
			{
				*out_comp = p.s->img_n;
			}
		}
		STBI_FREE(p.out); p.out = nullptr;
		STBI_FREE(p.expanded); p.expanded = nullptr;
		STBI_FREE(p.idata); p.idata = nullptr;

		result = result2;
		ret:
	}


	if (result == nullptr)
	{
		return nullptr;
	}

	// it is the responsibility of the loaders to make sure we get either 8 or 16 bit.
	STBI_ASSERT(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);

	if (ri.bits_per_channel != 8)
	{
		const auto img_len = (*out_x) * (*out_y) * (req_comp == 0 ? (*out_comp) : req_comp);

		const auto reduced = static_cast<Byte*>(stbi_malloc(img_len));
		if (reduced == nullptr)
		{
			return stbi__errpuc("outofmem", "Out of memory");
		}

		const auto orig = static_cast<U16*>(result);
		for (int i = 0; i < img_len; ++i)
		{
			// top half of each byte is sufficient approx of 16->8 bit scaling
			reduced[i] = static_cast<Byte>((orig[i] >> 8) & 0xFF);
		}

		STBI_FREE(orig);
		result = reduced;

		ri.bits_per_channel = 8;
	}

	return static_cast<Byte*>(result);
}
