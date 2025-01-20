#include "./stb_image.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include <climits>
#include <cassert>
#include <cstdint>


#include "./zlib.hpp"


static_assert(sizeof(uint32_t) == 4);

#define STBI_ASSERT(x) assert(x)
#define STBI_NOTUSED(v)  (void)sizeof(v)

static constexpr auto STBI_MAX_DIMENSIONS = 1 << 24;


///////////////////////////////////////////////
//
//  stbi__context struct and start_xxx functions

// stbi__context structure is our basic context used by all images, so it
// contains all the IO context, plus some basic image information
struct DecodeContext
{
	size_t img_x;
	size_t img_y;
	size_t img_n;
	size_t img_out_n;

	uint8_t *img_buffer;
	uint8_t *img_buffer_end;
	uint8_t *img_buffer_original;
	uint8_t *img_buffer_original_end;

	auto get8() -> uint8_t
	{
		if (img_buffer < img_buffer_end)
			return *img_buffer++;
		return 0;
	}
	auto get16be() -> uint16_t
	{
		const auto z = get8();
		return (static_cast<uint16_t>(z) << 8) | get8();
	}
	auto get32be() -> uint32_t
	{
		const auto z = get16be();
		return (static_cast<uint32_t>(z) << 16) | get16be();
	}
	auto skip (int count) -> void
	{
		if (count == 0)
		{
			return;
		}
		if (count < 0)
		{
			img_buffer = img_buffer_end;
			return;
		}
		img_buffer += count;
	}
	auto start_mem(uint8_t const *buffer, size_t size) -> void
	{
		// initialize a memory-decode context
		const auto cbi = const_cast<uint8_t*>(buffer);
		img_buffer = img_buffer_original = cbi;
		img_buffer_end = img_buffer_original_end = cbi + size;
	}
	auto rewind () -> void
	{
		// conceptually rewind SHOULD rewind to the beginning of the stream,
		// but we just rewind to the beginning of the initial buffer, because
		// we only use it after doing 'test', which only ever looks at at most 92 bytes
		img_buffer = img_buffer_original;
		img_buffer_end = img_buffer_original_end;
}
};



struct ResultInfo
{
	int bits_per_channel;
	int num_channels;
	int channel_order;
};

static const char *stbi__g_failure_reason;


static auto stbi__err(const char *str) -> bool
{
	stbi__g_failure_reason = str;
	return false;
}

static auto stbi_malloc(size_t size) -> void*
{
	return std::malloc(size);
}

template<typename T>
static auto stbi_malloc_t (size_t size) -> T*
{
	return static_cast<T*>(stbi_malloc(size));
}

static auto stbi_free (void* p) -> void
{
	std::free(p);
}

static auto stb_realloc (void* p, size_t oldsz, size_t newsz) -> void*
{
	return std::realloc(p,newsz);
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
static int addsizes_valid(size_t a, size_t b)
{
	if (b < 0)
	{
		return 0;
	}
	// now 0 <= b <= INT_MAX, hence also
	// 0 <= INT_MAX - b <= INTMAX.
	// And "a + b <= INT_MAX" (which might overflow) is the
	// same as a <= INT_MAX - b (no overflow)
	return a <= SIZE_MAX - b;
}

// returns 1 if the product is valid, 0 on overflow.
// negative factors are considered invalid.
static int mul2sizes_valid(size_t a, size_t b)
{
	if (a < 0 || b < 0)
	{
		return 0;
	}
	// mul-by-0 is always safe
	// portable way to check for no overflows in a*b
	return b == 0 || (a <= SIZE_MAX / b);
}

// returns 1 if "a*b + add" has no negative terms/factors and doesn't overflow
static int fma2sizes_valid(size_t a, size_t b, size_t add)
{
	return mul2sizes_valid(a, b) && addsizes_valid(a * b, add);
}

// returns 1 if "a*b*c + add" has no negative terms/factors and doesn't overflow
static int fma3sizes_valid(size_t a, size_t b, size_t c, size_t add)
{
	return (
		mul2sizes_valid(a, b) &&
		mul2sizes_valid(a * b, c) &&
		addsizes_valid(a * b * c, add)
	);
}

// mallocs with size overflow checking
template<typename T>
static auto malloc_fma2(size_t a, size_t b, size_t add) -> T*
{
	if (!fma2sizes_valid(a, b, add))
	{
		return nullptr;
	}
	return stbi_malloc_t<T>(a * b + add);
}

template<typename T>
static auto malloc_fma3(size_t a, size_t b, size_t c, size_t add) -> T*
{
	if (!fma3sizes_valid(a, b, c, add))
	{
		return nullptr;
	}
	return stbi_malloc_t<T>(a * b * c + add);
}


// stbi__err - error
// stbi__errpuc - error returning pointer to unsigned char


#define stbi__err(x,y)  stbi__err(x)
#define stbi__errpuc(x,y)  ((unsigned char *)(size_t) (stbi__err(x,y)?NULL:NULL))


enum
{
	STBI__SCAN_load = 0,
	STBI__SCAN_type,
	STBI__SCAN_header
};


#define STBI__BYTECAST(x)  ((uint8_t) ((x) & 255))  // truncate int to byte without warnings


// public domain "baseline" PNG decoder   v0.10  Sean Barrett 2006-11-18
//    simple implementation
//      - only 8-bit samples
//      - no CRC checking
//      - allocates lots of intermediate memory
//        - avoids problem of streaming data between subsystems
//        - avoids explicit window management
//    performance
//      - uses stb_zlib, a PD zlib implementation with fast huffman decoding

struct PNGChunk
{
	uint32_t length;
	uint32_t type;
};


static int stbi__check_png_header(DecodeContext *s)
{
	static const uint8_t png_sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	for (const auto i: png_sig)
	{
		if (s->get8() != i)
		{
			return stbi__err("bad png sig", "Not a PNG");
		}
	}
	return 1;
}

struct PNG
{
	DecodeContext *s;
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

static const uint8_t DEPTH_SCALE_TABLE[9] = {0, 0xff, 0x55, 0, 0x11, 0, 0, 0, 0x01};

// adds an extra all-255 alpha channel
// dest == src is legal
// img_n must be 1 or 3
static void stbi__create_png_alpha_expand8(
	uint8_t *dest,
	uint8_t *src,
	size_t x,
	size_t img_n)
{
	// must process data backwards since we allow dest==src
	if (img_n == 1)
	{
		for (auto i = x - 1;; --i)
		{
			dest[i * 2 + 1] = 255;
			dest[i * 2 + 0] = src[i];
			if (i == 0)
			{
				return;
			}

		}
	}
	if (img_n == 3)
	{
		for (auto i = x - 1;; --i)
		{
			dest[i * 4 + 3] = 255;
			dest[i * 4 + 2] = src[i * 3 + 2];
			dest[i * 4 + 1] = src[i * 3 + 1];
			dest[i * 4 + 0] = src[i * 3 + 0];
			if (i == 0)
			{
				return;
			}
		}
	}
}

// create the png data from post-deflated data
static int stbi__create_png_image_raw(
	PNG *a,
	uint8_t *raw,
	size_t raw_len,
	size_t out_n,
	size_t x,
	size_t y,
	size_t depth,
	size_t color)
{
	auto bytes = (depth == 16 ? 2 : 1);
	auto s = a->s;
	uint32_t i;
	auto stride = x * out_n * bytes;
	int all_ok = 1;
	int k;
	auto img_n = s->img_n; // copy it into a local for later

	auto output_bytes = out_n * bytes;
	auto filter_bytes = img_n * bytes;
	auto width = x;

	STBI_ASSERT(out_n == s->img_n || out_n == s->img_n+1);
	// extra bytes to write off the end into
	a->out = malloc_fma3<uint8_t>(x, y, output_bytes, 0);
	if (!a->out)
	{
		return stbi__err("outofmem", "Out of memory");
	}

	// note: error exits here don't need to clean up a->out individually,
	// stbi__do_png always does on error.
	if (!fma3sizes_valid(img_n, x, depth, 7))
	{
		return stbi__err("too large", "Corrupt PNG");
	}
	auto img_width_bytes = (((img_n * x * depth) + 7) >> 3);
	if (!fma2sizes_valid(img_width_bytes, y, img_width_bytes))
	{
		return stbi__err("too large", "Corrupt PNG");
	}
	auto img_len = (img_width_bytes + 1) * y;

	// we used to check for exact match between raw_len and img_len on non-interlaced PNGs,
	// but issue #276 reported a PNG in the wild that had extra data at the end (all zeros),
	// so just check for raw_len < img_len always.
	if (raw_len < img_len)
	{
		return stbi__err("not enough pixels", "Corrupt PNG");
	}

	// Allocate two scan lines worth of filter workspace buffer.
	auto filter_buf = malloc_fma2<uint8_t>(img_width_bytes, 2, 0);
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

	for (auto j = 0; j < y; ++j)
	{
		// cur/prior filter buffers alternate
		auto cur = filter_buf + (j & 1) * img_width_bytes;
		auto prior = filter_buf + (~j & 1) * img_width_bytes;
		auto dest = a->out + stride * j;
		auto nk = width * filter_bytes;
		auto filter = *raw++;

		// check filter type
		if (filter > 4)
		{
			all_ok = stbi__err("invalid filter", "Corrupt PNG");
			break;
		}

		// if first row, use special filter that doesn't sample previous row
		if (j == 0)
		{
			filter = first_row_filter[filter];
		}

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
			auto scale = (color == 0) ? DEPTH_SCALE_TABLE[depth] : 1; // scale grayscale values to 0..255 range
			auto in = cur;
			auto out = dest;
			uint8_t inb = 0;
			auto nsmp = x * img_n;

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
			auto nsmp = x * img_n;

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

	stbi_free(filter_buf);
	if (!all_ok)
	{
		return 0;
	}

	return 1;
}

static int stbi__parse_png_file(PNG *z, int scan, int req_comp)
{
	DecodeContext *s = z->s;

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

	uint8_t palette[1024];
	uint8_t pal_img_n = 0;
	uint8_t has_trans = 0;
	uint16_t tc16[3];
	uint8_t tc[3] = {0};
	uint32_t ioff = 0;
	uint32_t idata_limit = 0;
	uint32_t i;
	uint32_t pal_len = 0;
	int first = 1;
	int k;
	int interlace = 0;
	int color = 0;
	int is_iphone = 0;
	while (true)
	{
		// stbi__get_chunk_header
		PNGChunk c;
		c.length = s->get32be();
		c.type = s->get32be();
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
							tc16[k] = s->get16be(); // copy the values as-is
						}
					}
					else
					{
						for (k = 0; k < s->img_n && k < 3; ++k)
						{
							tc[k] = static_cast<uint8_t>(s->get16be() & 255) * DEPTH_SCALE_TABLE[z->depth];
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

					auto p = static_cast<uint8_t*>(stb_realloc(z->idata, idata_limit_old, idata_limit));
					if (p == nullptr)
					{
						return stbi__err("outofmem", "Out of memory");
					}
					z->idata = p;
				}
				{
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

				zctx.malloc = &stbi_malloc;
				zctx.free = [](void *p) { stbi_free(p); };
				zctx.realloc = [](void *p, size_t olds, size_t news) { return stb_realloc(p, olds, news); };

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
				stbi_free(z->idata);
				z->idata = nullptr;
				if ((req_comp == s->img_n + 1 && req_comp != 3 && !pal_img_n) || has_trans)
				{
					s->img_out_n = s->img_n + 1;
				}
				else
				{
					s->img_out_n = s->img_n;
				}
				{
					auto _image_data = z->expanded;
					auto _image_data_len = raw_len;
					auto _out_n = s->img_out_n;
					auto _depth = z->depth;
					auto _color = color;
					auto _interlaced = interlace;
					if (!_interlaced)
					{
						return stbi__create_png_image_raw(
							z,
							_image_data,
							_image_data_len,
							_out_n,
							z->s->img_x,
							z->s->img_y,
							_depth,
							_color
						);
					}

					// de-interlacing
					auto out_bytes = _out_n * (_depth == 16 ? 2 : 1);
					auto final = malloc_fma3<uint8_t>(z->s->img_x, z->s->img_y, out_bytes, 0);
					if (!final)
					{
						return stbi__err("outofmem", "Out of memory");
					}
					for (int p = 0; p < 7; ++p)
					{
						int xorig[] = {0, 4, 0, 2, 0, 1, 0};
						int yorig[] = {0, 0, 4, 0, 2, 0, 1};
						int xspc[] = {8, 8, 4, 4, 2, 2, 1};
						int yspc[] = {8, 8, 8, 4, 4, 2, 2};
						// pass1_x[4] = 0, pass1_x[5] = 1, pass1_x[12] = 1
						auto x = (z->s->img_x - xorig[p] + xspc[p] - 1) / xspc[p];
						auto y = (z->s->img_y - yorig[p] + yspc[p] - 1) / yspc[p];
						if (x && y)
						{
							auto img_len = ((((z->s->img_n * x * _depth) + 7) >> 3) + 1) * y;
							if (!stbi__create_png_image_raw(z, _image_data, _image_data_len, _out_n, x, y, _depth, _color))
							{
								stbi_free(final);
								return false;
							}
							for (int jj = 0; jj < y; ++jj)
							{
								for (int ii = 0; ii < x; ++ii)
								{
									auto out_y = jj * yspc[p] + yorig[p];
									auto out_x = ii * xspc[p] + xorig[p];
									std::memcpy(
										final + out_y * z->s->img_x * out_bytes + out_x * out_bytes,
										z->out + (jj * x + ii) * out_bytes,
										out_bytes
									);
								}
							}
							stbi_free(z->out);
							_image_data += img_len;
							_image_data_len -= img_len;
						}
					}
					z->out = final;
				}
				if (has_trans)
				{
					if (z->depth == 16)
					{

						auto outn = s->img_out_n;
						auto pixel_count = z->s->img_x * z->s->img_y;
						auto p = reinterpret_cast<uint16_t *>(z->out);

						// compute color-based transparency, assuming we've
						// already got 65535 as the alpha value in the output
						if (outn == 2)
						{
							for (auto ii = 0; ii < pixel_count; ++ii)
							{
								p[1] = (p[0] == tc16[0] ? 0 : 0xFFFF);
								p += 2;
							}
						}
						else if (outn == 4)
						{
							for (auto ii = 0; ii < pixel_count; ++ii)
							{
								if (p[0] == tc16[0] && p[1] == tc16[1] && p[2] == tc16[2])
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
						auto outn = s->img_out_n;
						auto pixel_count = z->s->img_x * z->s->img_y;
						auto p = z->out;

						// compute color-based transparency, assuming we've
						// already got 255 as the alpha value in the output
						if (outn == 2)
						{
							for (auto ii = 0; ii < pixel_count; ++ii)
							{
								p[1] = (p[0] == tc[0] ? 0 : 255);
								p += 2;
							}
						}
						else if (outn == 4)
						{
							for (auto ii = 0; ii < pixel_count; ++ii)
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
					// stbi__expand_png_palette
					{
						auto pal_img_n2 = s->img_out_n;
						auto pixel_count = z->s->img_x * z->s->img_y;
						auto orig = z->out;

						auto p = malloc_fma2<uint8_t>(pixel_count, pal_img_n2, 0);
						if (p == nullptr)
						{
							return stbi__err("outofmem", "Out of memory");
						}

						// between here and free(out) below, exitting would leak
						auto temp_out = p;

						if (pal_img_n2 == 3)
						{
							for (auto ii = 0; ii < pixel_count; ++ii)
							{
								int nn = orig[ii] * 4;
								p[0] = palette[nn];
								p[1] = palette[nn + 1];
								p[2] = palette[nn + 2];
								p += 3;
							}
						}
						else
						{
							for (auto ii = 0; ii < pixel_count; ++ii)
							{
								auto nn = orig[ii] * 4;
								p[0] = palette[nn];
								p[1] = palette[nn + 1];
								p[2] = palette[nn + 2];
								p[3] = palette[nn + 3];
								p += 4;
							}
						}
						stbi_free(z->out);
						z->out = temp_out;
					}
				}
				else if (has_trans)
				{
					// non-paletted image with tRNS -> source image has (constant) alpha
					++s->img_n;
				}
				stbi_free(z->expanded);
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
					// invalid_chunk[0] = STBI__BYTECAST(c.type >> 24);
					// invalid_chunk[1] = STBI__BYTECAST(c.type >> 16);
					// invalid_chunk[2] = STBI__BYTECAST(c.type >> 8);
					// invalid_chunk[3] = STBI__BYTECAST(c.type >> 0);
					return stbi__err("XXXX PNG chunk not known", "PNG not supported: unknown PNG chunk type");
				}
				s->skip(c.length);
				break;
			}
		}
		// end of PNG chunk, read and skip CRC
		s->get32be();
	}
}

template<typename T>
static auto compute_luma (T r, T g, T b) -> T
{
	const auto ir =static_cast<uint64_t>(r);
	const auto ig =static_cast<uint64_t>(g);
	const auto ib =static_cast<uint64_t>(b);
	return static_cast<T>((ir * 77 + ig * 150 + ib * 29) >> 8);
}



auto coyote_stbi_load_from_memory(
	uint8_t const *buffer,
	uint64_t len,
	uint64_t* x,
	uint64_t* y,
	uint64_t* comp,
	uint64_t req_comp) -> uint8_t*
{
	DecodeContext s;
	s.start_mem(buffer, len);


	ResultInfo ri;
	void* true_result;
	{
		std::memset(&ri, 0, sizeof(ri)); // make sure it's initialized if we add new fields
		ri.bits_per_channel = 8; // default is 8 so most paths don't have to be changed
		ri.num_channels = 0;

		// test the formats with a very explicit header first (at least a FOURCC
		// or distinctive magic number first)
		const auto r = stbi__check_png_header(&s);
		s.rewind();
		if (r)
		{
			PNG p;
			p.s = &s;
			auto a_p = &p;
			auto a_x = x;
			auto a_y = y;
			auto a_n = comp;
			auto a_req_comp = req_comp;
			auto a_ri = &ri;
			void *result = nullptr;
			if (a_req_comp < 0 || a_req_comp > 4)
			{
				true_result = stbi__errpuc("bad req_comp", "Internal error");
				goto trueend;
			}
			if (stbi__parse_png_file(a_p, STBI__SCAN_load, a_req_comp))
			{
				if (a_p->depth <= 8)
				{
					a_ri->bits_per_channel = 8;
				}
				else if (a_p->depth == 16)
				{
					a_ri->bits_per_channel = 16;
				}
				else
				{
					true_result = stbi__errpuc("bad bits_per_channel", "PNG not supported: unsupported color depth");
					goto trueend;
				}
				result = a_p->out;
				a_p->out = nullptr;
				if (a_req_comp && a_req_comp != a_p->s->img_out_n)
				{
					static constexpr auto COMBO = [](size_t a, size_t b) { return (a<<3)|b; };
					if (a_ri->bits_per_channel == 8)
					{
						// result = stbi__convert_format(
						// 	static_cast<uint8_t*>(result),
						// 	p->s->img_out_n,
						// 	req_comp,
						// 	p->s->img_x,
						// 	p->s->img_y);
						auto data = static_cast<uint8_t*>(result);
						auto img_n = a_p->s->img_out_n;
						auto xx = a_p->s->img_x;
						auto yy = a_p->s->img_y;

						if (a_req_comp == img_n)
						{
							result = data;
							goto endp;
						}

						auto good = malloc_fma3<uint8_t>(a_req_comp, xx, yy, 0);
						if (good == nullptr)
						{
							stbi_free(data);
							result = stbi__errpuc("outofmem", "Out of memory");
							goto endp;
						}

						int i;
						for (auto j = 0; j < yy; ++j)
						{
							auto src = data + j * xx * img_n;
							auto dest = good + j * xx * a_req_comp;

							// convert source image with img_n components to one with req_comp components;
							// avoid switch per pixel, so use switch per scanline and massive macros
							switch ((img_n << 3) | a_req_comp)
							{
								case COMBO(1,2):
									for(i=xx-1; i >= 0; --i, src += 1, dest += 2) {
										dest[0] = src[0];
										dest[1] = 255;
									}
									break;
								case COMBO(1,3):
									for(i=xx-1; i >= 0; --i, src += 1, dest += 3) {
										dest[0] = dest[1] = dest[2] = src[0];
									}
									break;
								case COMBO(1,4):
									for(i=xx-1; i >= 0; --i, src += 1, dest += 4) {
										dest[0] = dest[1] = dest[2] = src[0];
										dest[3] = 255;
									}
									break;
								case COMBO(2,1):
									for(i=xx-1; i >= 0; --i, src += 2, dest += 1) {
										dest[0] = src[0];
									}
									break;
								case COMBO(2,3):
									for(i=xx-1; i >= 0; --i, src += 2, dest += 3) {
										dest[0] = dest[1] = dest[2] = src[0];
									}
									break;
								case COMBO(2,4):
									for(i=xx-1; i >= 0; --i, src += 2, dest += 4) {
										dest[0] = dest[1] = dest[2] = src[0];
										dest[3] = src[1];
									}
									break;
								case COMBO(3,4):
									for(i=xx-1; i >= 0; --i, src += 3, dest += 4) {
										dest[0] = src[0];
										dest[1] = src[1];
										dest[2] = src[2];
										dest[3] = 255;
									}
									break;
								case COMBO(3,1):
									for(i=xx-1; i >= 0; --i, src += 3, dest += 1) {
										dest[0] = compute_luma(src[0], src[1], src[2]);
									}
									break;
								case COMBO(3,2):
									for(i=xx-1; i >= 0; --i, src += 3, dest += 2) {
										dest[0] = compute_luma(src[0], src[1], src[2]);
										dest[1] = 255;
									}
									break;
								case COMBO(4,1):
									for(i=xx-1; i >= 0; --i, src += 4, dest += 1) {
										dest[0] = compute_luma(src[0], src[1], src[2]);
									}
									break;
								case COMBO(4,2):
									for(i=xx-1; i >= 0; --i, src += 4, dest += 2) {
										dest[0] = compute_luma(src[0], src[1], src[2]);
										dest[1] = src[3];
									}
									break;
								case COMBO(4,3):
									for(i=xx-1; i >= 0; --i, src += 4, dest += 3) {
										dest[0] = src[0];
										dest[1] = src[1];
										dest[2] = src[2];
									}
									break;
								default: {
									STBI_ASSERT(0);
									stbi_free(data);
									stbi_free(good);
									result = stbi__errpuc("unsupported", "Unsupported format conversion");
									goto endp;
								}
							}
						}

						stbi_free(data);
						result = good;
					endp:
					}
					else
					{
						auto _data = static_cast<uint16_t*>(result);
						auto _img_n = a_p->s->img_out_n;
						if (a_req_comp == _img_n)
						{
							result = _data;
							goto endp;
						}

						auto xx = a_p->s->img_x;
						auto yy = a_p->s->img_y;
						auto good = stbi_malloc_t<uint16_t>(a_req_comp * xx * yy * 2);
						if (good == nullptr)
						{
							stbi_free(_data);
							result = reinterpret_cast<uint16_t *>(stbi__errpuc("outofmem", "Out of memory"));
							goto endp;
						}

						int i;
						for (auto j = 0; j < yy; ++j)
						{
							auto src = _data + j * xx * _img_n;
							auto dest = good + j * xx * a_req_comp;

							// convert source image with img_n components to one with req_comp components;
							// avoid switch per pixel, so use switch per scanline and massive macros
							switch ((_img_n << 3) | a_req_comp)
							{
								case COMBO(1,2):
									for(i=xx-1; i >= 0; --i, src += 1, dest += 2)
									{
										dest[0] = src[0];
										dest[1] = 0xffff;
									}
									break;
								case COMBO(1,3):
									for(i=xx-1; i >= 0; --i, src += 1, dest += 3)
									{
										dest[0] = dest[1] = dest[2] = src[0];
									}
									break;
								case COMBO(1,4):
									for(i=xx-1; i >= 0; --i, src += 1, dest += 4)
									{
										dest[0] = dest[1] = dest[2] = src[0];
										dest[3] = 0xffff;
									}
									break;
								case COMBO(2,1):
									for(i=xx-1; i >= 0; --i, src += 2, dest += 1)
									{
										dest[0] = src[0];
									}
									break;
								case COMBO(2,3):
									for(i=xx-1; i >= 0; --i, src += 2, dest += 3) { dest[0] = dest[1] = dest[2] = src[0]; }
									break;
								case COMBO(2,4):
									for(i=xx-1; i >= 0; --i, src += 2, dest += 4)
									{
										dest[0] = dest[1] = dest[2] = src[0];
										dest[3] = src[1];
									}
									break;
								case COMBO(3,4):
									for(i=xx-1; i >= 0; --i, src += 3, dest += 4)
									{
										dest[0] = src[0];
										dest[1] = src[1];
										dest[2] = src[2];
										dest[3] = 0xffff;
									}
									break;
								case COMBO(3,1):
									for(i=xx-1; i >= 0; --i, src += 3, dest += 1)
									{
										dest[0] = compute_luma(src[0], src[1], src[2]);
									}
									break;
								case COMBO(3,2):
									for(i=xx-1; i >= 0; --i, src += 3, dest += 2)
									{
										dest[0] = compute_luma(src[0], src[1], src[2]);
										dest[1] = 0xffff;
									}
									break;
								case COMBO(4,1):
									for(i=xx-1; i >= 0; --i, src += 4, dest += 1)
									{
										dest[0] = compute_luma(src[0], src[1], src[2]);
									}
									break;
								case COMBO(4,2):
									for(i=xx-1; i >= 0; --i, src += 4, dest += 2)
									{
										dest[0] = compute_luma(src[0], src[1], src[2]);
										dest[1] = src[3];
									}
									break;
								case COMBO(4,3):
									for(i=xx-1; i >= 0; --i, src += 4, dest += 3)
									{
										dest[0] = src[0];
										dest[1] = src[1];
										dest[2] = src[2];
									}
									break;
								default: {
									STBI_ASSERT(0);
									stbi_free(_data);
									stbi_free(good);
									result = stbi__errpuc("unsupported", "Unsupported format conversion");
									goto endp;
								}

							}
						}

						stbi_free(_data);
						result = good;
					endp:
					}
					a_p->s->img_out_n = a_req_comp;
					if (result == nullptr)
					{
						true_result = result;
						goto trueend;
					}
				}
				*a_x = a_p->s->img_x;
				*a_y = a_p->s->img_y;
				if (a_n)
				{
					*a_n = a_p->s->img_n;
				}
			}
			stbi_free(a_p->out);
			a_p->out = nullptr;
			stbi_free(a_p->expanded);
			a_p->expanded = nullptr;
			stbi_free(a_p->idata);
			a_p->idata = nullptr;

			true_result = result;
		trueend:
		}
		else
		{
			true_result = stbi__errpuc("unknown image type", "Image not of any known type, or corrupt");
		}

		if (true_result == nullptr)
		{
			return nullptr;
		}
	}


	// it is the responsibility of the loaders to make sure we get either 8 or 16 bit.
	STBI_ASSERT(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);

	if (ri.bits_per_channel != 8)
	{
		auto orig = static_cast<uint16_t *>(true_result);
		auto img_len = (*x) * (*y) * (req_comp == 0 ? *comp : req_comp);

		auto reduced = stbi_malloc_t<uint8_t>(img_len);
		if (reduced == nullptr)
		{
			return stbi__errpuc("outofmem", "Out of memory");
		}

		for (int i = 0; i < img_len; ++i)
		{
			// top half of each byte is sufficient approx of 16->8 bit scaling
			reduced[i] = static_cast<uint8_t>((orig[i] >> 8) & 0xFF);
		}

		stbi_free(orig);
		true_result = reduced;
		ri.bits_per_channel = 8;
	}

	return static_cast<uint8_t*>(true_result);
}

auto coyote_stbi_info_from_memory(
	uint8_t const *buffer,
	uint64_t len,
	uint64_t *x,
	uint64_t *y,
	uint64_t *comp) -> uint32_t
{
	DecodeContext s;
	s.start_mem(buffer, len);

	PNG p;
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
		return true;
	}
	p.s->rewind();
	return stbi__err("unknown image type", "Image not of any known type, or corrupt");
}

void coyote_stbi_image_free(void *retval_from_stbi_load)
{
	stbi_free(retval_from_stbi_load);
}

auto coyote_stbi_failure_reason () -> const char *
{
	return stbi__g_failure_reason;
}
