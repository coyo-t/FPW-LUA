
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
#include <climits>

#ifndef STBI_ASSERT
#include <cassert>
#define STBI_ASSERT(x) assert(x)
#endif



#ifndef STBI_NO_THREAD_LOCALS
#if defined(__cplusplus) &&  __cplusplus >= 201103L
#define STBI_THREAD_LOCAL       thread_local
#elif defined(__GNUC__) && __GNUC__ < 5
      #define STBI_THREAD_LOCAL       __thread
#elif defined(_MSC_VER)
      #define STBI_THREAD_LOCAL       __declspec(thread)
#elif defined (__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
      #define STBI_THREAD_LOCAL       _Thread_local
#endif

#ifndef STBI_THREAD_LOCAL
#if defined(__GNUC__)
        #define STBI_THREAD_LOCAL       __thread
#endif
#endif
#endif


#include <cstdint>


// should produce compiler error if size is wrong
static_assert(sizeof(std::uint32_t) == 4);

#if defined(STBI_MALLOC) && defined(STBI_FREE) && (defined(STBI_REALLOC) || defined(STBI_REALLOC_SIZED))
// ok
#elif !defined(STBI_MALLOC) && !defined(STBI_FREE) && !defined(STBI_REALLOC) && !defined(STBI_REALLOC_SIZED)
// ok
#else
#error "Must define all or none of STBI_MALLOC, STBI_FREE, and STBI_REALLOC (or STBI_REALLOC_SIZED)."
#endif

#ifndef STBI_MALLOC
#define STBI_MALLOC(sz)           malloc(sz)
#define STBI_REALLOC(p,newsz)     realloc(p,newsz)
#define STBI_FREE(p)              free(p)
#endif

#ifndef STBI_REALLOC_SIZED
#define STBI_REALLOC_SIZED(p,oldsz,newsz) STBI_REALLOC(p,newsz)
#endif

static
#ifdef STBI_THREAD_LOCAL
STBI_THREAD_LOCAL
#endif
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


#ifndef STBI_MAX_DIMENSIONS
#define STBI_MAX_DIMENSIONS (1 << 24)
#endif


struct stbi__result_info
{
	int bits_per_channel;
	int num_channels;
	int channel_order;
};

struct stbi__pngchunk
{
	std::uint32_t length;
	std::uint32_t type;
};


///////////////////////////////////////////////
//
//  stbi__context struct and start_xxx functions

// stbi__context structure is our basic context used by all images, so it
// contains all the IO context, plus some basic image information
struct stbi__context
{
	std::uint32_t img_x, img_y;
	int img_n, img_out_n;

	int buflen;
	std::uint8_t buffer_start[128];
	int callback_already_read;

	std::uint8_t *img_buffer, *img_buffer_end;
	std::uint8_t *img_buffer_original, *img_buffer_original_end;

	// initialize a memory-decode context
	auto stbi__start_mem(std::uint8_t const *buffer, int len) -> void
	{
		callback_already_read = 0;
		img_buffer = img_buffer_original = (std::uint8_t *) buffer;
		img_buffer_end = img_buffer_original_end = (std::uint8_t *) buffer + len;
	}
	auto stbi__rewind() -> void
	{
		// conceptually rewind SHOULD rewind to the beginning of the stream,
		// but we just rewind to the beginning of the initial buffer, because
		// we only use it after doing 'test', which only ever looks at at most 92 bytes
		this->img_buffer = this->img_buffer_original;
		this->img_buffer_end = this->img_buffer_original_end;
	}
	auto stbi__get8() -> std::uint8_t
	{
		if (this->img_buffer < this->img_buffer_end)
			return *this->img_buffer++;
		return 0;
	}
	auto stbi__get16be () -> int
	{
		int z = stbi__get8();
		return (z << 8) + stbi__get8();
	}
	auto stbi__get32be () -> std::uint32_t
	{
		auto z = stbi__get16be();
		return (z << 16) + stbi__get16be();
	}
	auto stbi__check_png_header() -> int
	{
		static const std::uint8_t png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
		for (const auto i : png_sig)
		{
			if (stbi__get8() != i)
			{
				return stbi__err("bad png sig", "Not a PNG");
			}
		}
		return 1;
	}
};

struct stbi__png
{
	stbi__context *s;
	std::uint8_t *idata, *expanded, *out;
	int depth;
};

static int stbi__png_test(stbi__context *s);

static void *stbi__png_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);


static void *stbi__malloc(size_t size)
{
	return STBI_MALLOC(size);
}


STBIDEF void stbi_image_free(void *retval_from_stbi_load)
{
	STBI_FREE(retval_from_stbi_load);
}

static void *stbi__load_main(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri, int bpc)
{
	memset(ri, 0, sizeof(*ri)); // make sure it's initialized if we add new fields
	ri->bits_per_channel = 8; // default is 8 so most paths don't have to be changed
	ri->num_channels = 0;

	// test the formats with a very explicit header first (at least a FOURCC
	// or distinctive magic number first)
	if (stbi__png_test(s))
	{
		return stbi__png_load(s, x, y, comp, req_comp, ri);
	}

	return stbi__errpuc("unknown image type", "Image not of any known type, or corrupt");
}

static std::uint8_t *stbi__convert_16_to_8(std::uint16_t *orig, int w, int h, int channels)
{
	int i;
	int img_len = w * h * channels;
	std::uint8_t *reduced;

	reduced = (std::uint8_t *) stbi__malloc(img_len);
	if (reduced == NULL) return stbi__errpuc("outofmem", "Out of memory");

	for (i = 0; i < img_len; ++i)
		reduced[i] = (std::uint8_t) ((orig[i] >> 8) & 0xFF); // top half of each byte is sufficient approx of 16->8 bit scaling

	STBI_FREE(orig);
	return reduced;
}


static unsigned char *stbi__load_and_postprocess_8bit(stbi__context *s, int *x, int *y, int *comp, int req_comp)
{
	stbi__result_info ri;
	void *result = stbi__load_main(s, x, y, comp, req_comp, &ri, 8);

	if (result == nullptr)
		return nullptr;

	// it is the responsibility of the loaders to make sure we get either 8 or 16 bit.
	STBI_ASSERT(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);

	if (ri.bits_per_channel != 8)
	{
		result = stbi__convert_16_to_8((std::uint16_t *) result, *x, *y, req_comp == 0 ? *comp : req_comp);
		ri.bits_per_channel = 8;
	}

	return static_cast<unsigned char *>(result);
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
static bool stbi__addsizes_valid(int a, int b)
{
	if (b < 0) return false;
	// now 0 <= b <= INT_MAX, hence also
	// 0 <= INT_MAX - b <= INTMAX.
	// And "a + b <= INT_MAX" (which might overflow) is the
	// same as a <= INT_MAX - b (no overflow)
	return a <= INT_MAX - b;
}

// returns 1 if the product is valid, 0 on overflow.
// negative factors are considered invalid.
static bool stbi__mul2sizes_valid(int a, int b)
{
	if (a < 0 || b < 0) return false;
	if (b == 0) return true; // mul-by-0 is always safe
	// portable way to check for no overflows in a*b
	return a <= INT_MAX / b;
}

// returns 1 if "a*b + add" has no negative terms/factors and doesn't overflow
static bool stbi__mad2sizes_valid(int a, int b, int add)
{
	return stbi__mul2sizes_valid(a, b) && stbi__addsizes_valid(a * b, add);
}

// returns 1 if "a*b*c + add" has no negative terms/factors and doesn't overflow
static bool stbi__mad3sizes_valid(int a, int b, int c, int add)
{
	return stbi__mul2sizes_valid(a, b) &&
		stbi__mul2sizes_valid(a * b, c) &&
		stbi__addsizes_valid(a * b * c, add);
}

// mallocs with size overflow checking
template<typename T>
static T*stbi__malloc_fma2(int a, int b, int add)
{
	if (!stbi__mad2sizes_valid(a, b, add))
		return nullptr;
	return static_cast<T*>(stbi__malloc(a * b + add));
}

template<typename T>
static T* stbi__malloc_fma3(int a, int b, int c, int add)
{
	if (!stbi__mad3sizes_valid(a, b, c, add))
		return nullptr;
	return static_cast<T*>(stbi__malloc(a * b * c + add));
}



// fast-way is faster to check than jpeg huffman, but slow way is slower
// accelerate all cases in default tables
static constexpr auto FAST_BITS =  9;
static constexpr auto FAST_MASK =  ((1 << FAST_BITS) - 1);

// number of symbols in literal/length alphabet
static constexpr auto NSYMS = 288;


static constexpr auto LENGTH_BASE[31] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13,
	15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
	67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0
};

static constexpr auto LENGTH_EXTRA[31] =
		{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 0, 0};

static constexpr auto DIST_BASE[32] = {
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
	257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 0, 0
};

static constexpr auto DIST_EXTRA[32] =
		{0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};


/*
Init algorithm:
{
	int i;   // use <= to match clearly with spec
	for (i=0; i <= 143; ++i)     stbi__zdefault_length[i]   = 8;
	for (   ; i <= 255; ++i)     stbi__zdefault_length[i]   = 9;
	for (   ; i <= 279; ++i)     stbi__zdefault_length[i]   = 7;
	for (   ; i <= 287; ++i)     stbi__zdefault_length[i]   = 8;

	for (i=0; i <=  31; ++i)     stbi__zdefault_distance[i] = 5;
}
*/

static constexpr std::uint8_t DEFAULT_LENGTH[NSYMS] =
{
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8
};
static constexpr std::uint8_t DEFAULT_DISTANCE[32] =
{
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
};


static int bit_reverse(const int v, const int bits)
{
	// to bit reverse n bits, reverse 16 and shift
	// e.g. 11 bits, bit reverse and shift away 5
	int n = v;
	n = ((n & 0xAAAA) >> 1) | ((n & 0x5555) << 1);
	n = ((n & 0xCCCC) >> 2) | ((n & 0x3333) << 2);
	n = ((n & 0xF0F0) >> 4) | ((n & 0x0F0F) << 4);
	n = ((n & 0xFF00) >> 8) | ((n & 0x00FF) << 8);
	return n >> (16 - bits);
}




// zlib-style huffman encoding
// (jpegs packs from left, zlib from right, so can't share code)
struct Huffman {
	std::uint16_t fast[1 << FAST_BITS];
	std::uint16_t firstcode[16];
	int maxcode[17];
	std::uint16_t firstsymbol[16];
	std::uint8_t size[NSYMS];
	std::uint16_t value[NSYMS];

	auto stbi__zbuild_huffman(const std::uint8_t *sizelist, int num) -> int
	{
		int i, k = 0;
		int next_code[16];
		int sizes[17] = {};

		memset(this->fast, 0, sizeof(this->fast));
		for (i = 0; i < num; ++i)
			++sizes[sizelist[i]];
		sizes[0] = 0;
		for (i = 1; i < 16; ++i)
			if (sizes[i] > (1 << i))
				return stbi__err("bad sizes", "Corrupt PNG");
		int code = 0;
		for (i = 1; i < 16; ++i)
		{
			next_code[i] = code;
			this->firstcode[i] = static_cast<std::uint16_t>(code);
			this->firstsymbol[i] = static_cast<std::uint16_t>(k);
			code = (code + sizes[i]);
			if (sizes[i])
				if (code - 1 >= (1 << i))
					return stbi__err("bad codelengths", "Corrupt PNG");
			this->maxcode[i] = code << (16 - i); // preshift for inner loop
			code <<= 1;
			k += sizes[i];
		}
		this->maxcode[16] = 0x10000; // sentinel
		for (i = 0; i < num; ++i)
		{
			int s = sizelist[i];
			if (s)
			{
				int c = next_code[s] - this->firstcode[s] + this->firstsymbol[s];
				auto fastv = (std::uint16_t) ((s << 9) | i);
				this->size[c] = (std::uint8_t) s;
				this->value[c] = (std::uint16_t) i;
				if (s <= FAST_BITS)
				{
					int j = bit_reverse(next_code[s], s);
					while (j < (1 << FAST_BITS))
					{
						this->fast[j] = fastv;
						j += (1 << s);
					}
				}
				++next_code[s];
			}
		}
		return 1;
	}
};



// zlib-from-memory implementation for PNG reading
//    because PNG allows splitting the zlib stream arbitrarily,
//    and it's annoying structurally to have PNG call ZLIB call PNG,
//    we require PNG read all the IDATs and combine them into a single
//    memory buffer

struct Buffer {
	std::uint8_t *zbuffer, *zbuffer_end;
	int num_bits;
	int hit_zeof_once;
	std::uint32_t code_buffer;

	std::uint8_t* zout;
	std::uint8_t* zout_start;
	std::uint8_t* zout_end;
	int z_expandable;

	Huffman z_length, z_distance;

	auto eof () const -> bool
	{
		return this->zbuffer >= this->zbuffer_end;
	}
	auto get8 () -> std::uint8_t
	{
		return eof() ? 0 : *this->zbuffer++;
	}
	auto fill_bits () -> void
	{
		do
		{
			if (code_buffer >= (1U << num_bits))
			{
				zbuffer = zbuffer_end; /* treat this as EOF so we fail. */
				return;
			}
			code_buffer |= static_cast<unsigned int>(get8()) << num_bits;
			num_bits += 8;
		} while (num_bits <= 24);

	}
	auto recieve (int n) -> unsigned int
	{
		if (num_bits < n) fill_bits();
		unsigned int k = code_buffer & ((1 << n) - 1);
		code_buffer >>= n;
		num_bits -= n;
		return k;
	}
	auto zhuffman_decode_slowpath (Huffman* z) -> int
	{
		int b, s, k;
		// not resolved by fast table, so compute it the slow way
		// use jpeg approach, which requires MSbits at top
		k = bit_reverse(code_buffer, 16);
		for (s = FAST_BITS + 1; ; ++s)
			if (k < z->maxcode[s])
				break;
		if (s >= 16) return -1; // invalid code!
		// code size is s, so:
		b = (k >> (16 - s)) - z->firstcode[s] + z->firstsymbol[s];
		if (b >= NSYMS) return -1; // some data was corrupt somewhere!
		if (z->size[b] != s) return -1; // was originally an assert, but report failure instead.
		code_buffer >>= s;
		num_bits -= s;
		return z->value[b];
	}
	auto zhuffman_decode(Huffman *z) -> int
	{
		int b, s;
		if (num_bits < 16)
		{
			if (eof())
			{
				if (!hit_zeof_once)
				{
					// This is the first time we hit eof, insert 16 extra padding btis
					// to allow us to keep going; if we actually consume any of them
					// though, that is invalid data. This is caught later.
					hit_zeof_once = 1;
					num_bits += 16; // add 16 implicit zero bits
				}
				else
				{
					// We already inserted our extra 16 padding bits and are again
					// out, this stream is actually prematurely terminated.
					return -1;
				}
			}
			else
			{
				fill_bits();
			}
		}
		b = z->fast[code_buffer & FAST_MASK];
		if (b)
		{
			s = b >> 9;
			code_buffer >>= s;
			num_bits -= s;
			return b & 511;
		}
		return zhuffman_decode_slowpath(z);
	}
	// need to make room for n bytes
	auto zexpand(std::uint8_t* zout, int n) -> int
	{
		std::uint8_t *q;
		this->zout = zout;
		if (!this->z_expandable) return stbi__err("output buffer limit", "Corrupt PNG");
		auto cur = static_cast<unsigned int>(this->zout - this->zout_start);
		auto limit = static_cast<unsigned>(this->zout_end - this->zout_start);
		if (UINT_MAX - cur < static_cast<unsigned>(n))
			return stbi__err("outofmem", "Out of memory");
		while (cur + n > limit)
		{
			if (limit > UINT_MAX / 2)
				return stbi__err("outofmem", "Out of memory");
			limit *= 2;
		}
		q = static_cast<std::uint8_t *>(STBI_REALLOC_SIZED(this->zout_start, old_limit, limit));
		if (q == nullptr)
			return stbi__err("outofmem", "Out of memory");
		this->zout_start = q;
		this->zout = q + cur;
		this->zout_end = q + limit;
		return 1;
	}
	auto parse_huffman_block() -> int
	{
		auto* zout = this->zout;
		for (;;)
		{
			int z = this->zhuffman_decode(&this->z_length);
			if (z < 256)
			{
				if (z < 0) return stbi__err("bad huffman code", "Corrupt PNG"); // error in huffman codes
				if (zout >= this->zout_end)
				{
					if (!this->zexpand(zout, 1)) return 0;
					zout = this->zout;
				}
				*zout++ = static_cast<std::uint8_t>(z);
			}
			else
			{
				if (z == 256)
				{
					this->zout = zout;
					if (this->hit_zeof_once && this->num_bits < 16)
					{
						// The first time we hit zeof, we inserted 16 extra zero bits into our bit
						// buffer so the decoder can just do its speculative decoding. But if we
						// actually consumed any of those bits (which is the case when num_bits < 16),
						// the stream actually read past the end so it is malformed.
						return stbi__err("unexpected end", "Corrupt PNG");
					}
					return 1;
				}
				if (z >= 286) return stbi__err("bad huffman code", "Corrupt PNG");
				// per DEFLATE, length codes 286 and 287 must not appear in compressed data
				z -= 257;
				int len = LENGTH_BASE[z];
				if (LENGTH_EXTRA[z]) len += this->recieve(LENGTH_EXTRA[z]);
				z = this->zhuffman_decode(&this->z_distance);
				if (z < 0 || z >= 30) return stbi__err("bad huffman code", "Corrupt PNG");
				// per DEFLATE, distance codes 30 and 31 must not appear in compressed data
				int dist = DIST_BASE[z];
				if (DIST_EXTRA[z]) dist += this->recieve(DIST_EXTRA[z]);
				if (zout - this->zout_start < dist) return stbi__err("bad dist", "Corrupt PNG");
				if (len > this->zout_end - zout)
				{
					if (!this->zexpand(zout, len))
						return 0;
					zout = this->zout;
				}
				std::uint8_t *p = zout - dist;
				if (dist == 1)
				{
					// run of one byte; common in images.
					std::uint8_t v = *p;
					if (len) { do *zout++ = v; while (--len); }
				}
				else
				{
					if (len) { do *zout++ = *p++; while (--len); }
				}
			}
		}
	}
	auto compute_huffman_codes() -> int
	{
		static const std::uint8_t length_dezigzag[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
		Huffman z_codelength;
		std::uint8_t lencodes[286 + 32 + 137]; //padding for maximum single op
		std::uint8_t codelength_sizes[19];
		int i, n;

		int hlit = this->recieve(5) + 257;
		int hdist = this->recieve(5) + 1;
		int hclen = this->recieve(4) + 4;
		int ntot = hlit + hdist;

		memset(codelength_sizes, 0, sizeof(codelength_sizes));
		for (i = 0; i < hclen; ++i)
		{
			int s = this->recieve(3);
			codelength_sizes[length_dezigzag[i]] = (std::uint8_t) s;
		}
		if (!z_codelength.stbi__zbuild_huffman(codelength_sizes, 19)) return 0;

		n = 0;
		while (n < ntot)
		{
			int c = this->zhuffman_decode(&z_codelength);
			if (c < 0 || c >= 19) return stbi__err("bad codelengths", "Corrupt PNG");
			if (c < 16)
				lencodes[n++] = (std::uint8_t) c;
			else
			{
				std::uint8_t fill = 0;
				if (c == 16)
				{
					c = this->recieve(2) + 3;
					if (n == 0) return stbi__err("bad codelengths", "Corrupt PNG");
					fill = lencodes[n - 1];
				}
				else if (c == 17)
				{
					c = this->recieve(3) + 3;
				}
				else if (c == 18)
				{
					c = this->recieve(7) + 11;
				}
				else
				{
					return stbi__err("bad codelengths", "Corrupt PNG");
				}
				if (ntot - n < c) return stbi__err("bad codelengths", "Corrupt PNG");
				memset(lencodes + n, fill, c);
				n += c;
			}
		}
		if (n != ntot) return stbi__err("bad codelengths", "Corrupt PNG");
		if (!this->z_length.stbi__zbuild_huffman(lencodes, hlit)) return 0;
		if (!this->z_distance.stbi__zbuild_huffman(lencodes + hlit, hdist)) return 0;
		return 1;
	}
	auto parse_uncompressed_block() -> int
	{
		std::uint8_t header[4];
		int len, nlen, k;
		if (this->num_bits & 7)
			this->recieve(this->num_bits & 7); // discard
		// drain the bit-packed data into header
		k = 0;
		while (this->num_bits > 0)
		{
			header[k++] = (std::uint8_t) (this->code_buffer & 255); // suppress MSVC run-time check
			this->code_buffer >>= 8;
			this->num_bits -= 8;
		}
		if (this->num_bits < 0) return stbi__err("zlib corrupt", "Corrupt PNG");
		// now fill header the normal way
		while (k < 4)
			header[k++] = this->get8();
		len = header[1] * 256 + header[0];
		nlen = header[3] * 256 + header[2];
		if (nlen != (len ^ 0xffff)) return stbi__err("zlib corrupt", "Corrupt PNG");
		if (this->zbuffer + len > this->zbuffer_end) return stbi__err("read past buffer", "Corrupt PNG");
		if (this->zout + len > this->zout_end)
			if (!this->zexpand(this->zout, len)) return 0;
		memcpy(this->zout, this->zbuffer, len);
		this->zbuffer += len;
		this->zout += len;
		return 1;
	}
	auto parse_zlib_header() -> int
	{
		int cmf = this->get8();
		int cm = cmf & 15;
		/* int cinfo = cmf >> 4; */
		int flg = this->get8();
		// zlib spec
		if (this->eof())
			return stbi__err("bad zlib header", "Corrupt PNG");
		// zlib spec
		if ((cmf * 256 + flg) % 31 != 0)
			return stbi__err("bad zlib header", "Corrupt PNG");
		// preset dictionary not allowed in png
		if (flg & 32)
			return stbi__err("no preset dict", "Corrupt PNG");
		// DEFLATE required for png
		if (cm != 8)
			return stbi__err("bad compression", "Corrupt PNG");
		// window = 1 << (8 + cinfo)... but who cares, we fully buffer output
		return 1;
	}
	auto parse_zlib(int parse_header) -> int
	{
		int final;
		if (parse_header)
			if (!this->parse_zlib_header()) return 0;
		this->num_bits = 0;
		this->code_buffer = 0;
		this->hit_zeof_once = 0;
		do
		{
			final = this->recieve(1);
			int type = this->recieve(2);
			if (type == 0)
			{
				if (!this->parse_uncompressed_block()) return 0;
			}
			else if (type == 3)
			{
				return 0;
			}
			else
			{
				if (type == 1)
				{
					// use fixed code lengths
					if (!this->z_length.stbi__zbuild_huffman(DEFAULT_LENGTH, NSYMS)) return 0;
					if (!this->z_distance.stbi__zbuild_huffman(DEFAULT_DISTANCE, 32)) return 0;
				}
				else
				{
					if (!this->compute_huffman_codes()) return 0;
				}
				if (!this->parse_huffman_block()) return 0;
			}
		} while (!final);
		return 1;
	}
	static auto zlib_decode_malloc_guesssize_headerflag(
		const std::uint8_t *buffer,
		int len,
		int initial_size,
		int *outlen,
		int parse_header) -> std::uint8_t*
	{
		Buffer a;
		auto p = (std::uint8_t*)stbi__malloc(initial_size);
		if (p == nullptr) return nullptr;
		a.zbuffer = (std::uint8_t *) buffer;
		a.zbuffer_end = (std::uint8_t *) buffer + len;

		a.zout_start = p;
		a.zout = p;
		a.zout_end = p + initial_size;
		a.z_expandable = true;

		if (a.parse_zlib(parse_header))
		{
			if (outlen) *outlen = (int) (a.zout - a.zout_start);
			return a.zout_start;
		}

		STBI_FREE(a.zout_start);
		// delete[] p;
		return nullptr;
	}
};

enum
{
	STBI__SCAN_load = 0,
	STBI__SCAN_type,
	STBI__SCAN_header
};


static void stbi__skip(stbi__context *s, int n)
{
	if (n == 0) return; // already there!
	if (n < 0)
	{
		s->img_buffer = s->img_buffer_end;
		return;
	}
	s->img_buffer += n;
}


static int stbi__getn(stbi__context *s, std::uint8_t *buffer, int n)
{
	if (s->img_buffer + n <= s->img_buffer_end)
	{
		memcpy(buffer, s->img_buffer, n);
		s->img_buffer += n;
		return 1;
	}
	return 0;
}






#define STBI__BYTECAST(x)  ((std::uint8_t) ((x) & 255))  // truncate int to byte without warnings


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

static std::uint8_t stbi__compute_y(int r, int g, int b)
{
	return (std::uint8_t) (((r * 77) + (g * 150) + (29 * b)) >> 8);
}

static unsigned char *stbi__convert_format(unsigned char *data, int img_n, int req_comp, unsigned int x, unsigned int y)
{
	int i, j;
	unsigned char *good;

	if (req_comp == img_n) return data;
	STBI_ASSERT(req_comp >= 1 && req_comp <= 4);

	good = stbi__malloc_fma3<unsigned char>(req_comp, x, y, 0);
	if (good == nullptr)
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


static std::uint16_t stbi__compute_y_16(int r, int g, int b)
{
	return (std::uint16_t) (((r * 77) + (g * 150) + (29 * b)) >> 8);
}



static std::uint16_t *stbi__convert_format16(std::uint16_t *data, int img_n, int req_comp, unsigned int x, unsigned int y)
{
	int i, j;
	std::uint16_t *good;

	if (req_comp == img_n) return data;
	STBI_ASSERT(req_comp >= 1 && req_comp <= 4);

	good = (std::uint16_t *) stbi__malloc(req_comp * x * y * 2);
	if (good == NULL)
	{
		STBI_FREE(data);
		return (std::uint16_t *) stbi__errpuc("outofmem", "Out of memory");
	}

	for (j = 0; j < static_cast<int>(y); ++j)
	{
		std::uint16_t *src = data + j * x * img_n;
		std::uint16_t *dest = good + j * x * req_comp;

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
				return (std::uint16_t *) stbi__errpuc("unsupported", "Unsupported format conversion");
		}
#undef STBI__CASE
	}

	STBI_FREE(data);
	return good;
}


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

static std::uint8_t first_row_filter[5] =
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

static const std::uint8_t stbi__depth_scale_table[9] = {
	0, 0xff, 0x55,
	0, 0x11, 0,
	0, 0,    0x01
};

// adds an extra all-255 alpha channel
// dest == src is legal
// img_n must be 1 or 3
static void stbi__create_png_alpha_expand8(std::uint8_t *dest, std::uint8_t *src, std::uint32_t x, int img_n)
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
static int stbi__create_png_image_raw(stbi__png *a, std::uint8_t *raw, std::uint32_t raw_len, int out_n, std::uint32_t x,
                                      std::uint32_t y, int depth, int color)
{
	int bytes = (depth == 16 ? 2 : 1);
	stbi__context *s = a->s;
	std::uint32_t i, j, stride = x * out_n * bytes;
	int all_ok = 1;
	int k;
	int img_n = s->img_n; // copy it into a local for later

	int output_bytes = out_n * bytes;
	int filter_bytes = img_n * bytes;
	int width = x;

	STBI_ASSERT(out_n == s->img_n || out_n == s->img_n+1);
	// extra bytes to write off the end into
	a->out = stbi__malloc_fma3<std::uint8_t>(x, y, output_bytes, 0);
	if (a->out == nullptr)
		return stbi__err("outofmem", "Out of memory");

	// note: error exits here don't need to clean up a->out individually,
	// stbi__do_png always does on error.
	if (!stbi__mad3sizes_valid(img_n, x, depth, 7))
		return stbi__err("too large", "Corrupt PNG");
	std::uint32_t img_width_bytes = (((img_n * x * depth) + 7) >> 3);
	if (!stbi__mad2sizes_valid(img_width_bytes, y, img_width_bytes))
		return stbi__err("too large", "Corrupt PNG");
	std::uint32_t img_len = (img_width_bytes + 1) * y;

	// we used to check for exact match between raw_len and img_len on non-interlaced PNGs,
	// but issue #276 reported a PNG in the wild that had extra data at the end (all zeros),
	// so just check for raw_len < img_len always.
	if (raw_len < img_len)
		return stbi__err("not enough pixels", "Corrupt PNG");

	// Allocate two scan lines worth of filter workspace buffer.
	auto *filter_buf = stbi__malloc_fma2<std::uint8_t>(img_width_bytes, 2, 0);
	if (!filter_buf)
		return stbi__err("outofmem", "Out of memory");

	// Filtering for low-bit-depth images
	if (depth < 8)
	{
		filter_bytes = 1;
		width = img_width_bytes;
	}

	for (j = 0; j < y; ++j)
	{
		// cur/prior filter buffers alternate
		std::uint8_t *cur = filter_buf + (j & 1) * img_width_bytes;
		std::uint8_t *prior = filter_buf + (~j & 1) * img_width_bytes;
		std::uint8_t *dest = a->out + stride * j;
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
			std::uint8_t scale = (color == 0) ? stbi__depth_scale_table[depth] : 1; // scale grayscale values to 0..255 range
			std::uint8_t *in = cur;
			std::uint8_t *out = dest;
			std::uint8_t inb = 0;
			std::uint32_t nsmp = x * img_n;

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
			auto *dest16 = reinterpret_cast<std::uint16_t *>(dest);
			std::uint32_t nsmp = x * img_n;

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

static int stbi__create_png_image(stbi__png *a, std::uint8_t *image_data, std::uint32_t image_data_len, int out_n, int depth,
                                  int color, int interlaced)
{
	int bytes = (depth == 16 ? 2 : 1);
	int out_bytes = out_n * bytes;
	int p;
	if (!interlaced)
		return stbi__create_png_image_raw(a, image_data, image_data_len, out_n, a->s->img_x, a->s->img_y, depth, color);

	// de-interlacing
	std::uint8_t *final = stbi__malloc_fma3<std::uint8_t>(a->s->img_x, a->s->img_y, out_bytes, 0);
	if (!final) return stbi__err("outofmem", "Out of memory");
	for (p = 0; p < 7; ++p)
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
			std::uint32_t img_len = ((((a->s->img_n * x * depth) + 7) >> 3) + 1) * y;
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

static int stbi__compute_transparency(stbi__png *z, std::uint8_t tc[3], int out_n)
{
	stbi__context *s = z->s;
	std::uint32_t i, pixel_count = s->img_x * s->img_y;
	std::uint8_t *p = z->out;

	// compute color-based transparency, assuming we've
	// already got 255 as the alpha value in the output

	if (out_n == 2)
	{
		for (i = 0; i < pixel_count; ++i)
		{
			p[1] = (p[0] == tc[0] ? 0 : 255);
			p += 2;
		}
	}
	else if (out_n == 4)
	{
		for (i = 0; i < pixel_count; ++i)
		{
			if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
				p[3] = 0;
			p += 4;
		}
	}
	else
	{
		return 0;
	}
	return 1;
}

static int stbi__compute_transparency16(stbi__png *z, std::uint16_t tc[3], int out_n)
{
	stbi__context *s = z->s;
	std::uint32_t i, pixel_count = s->img_x * s->img_y;
	auto *p = reinterpret_cast<std::uint16_t *>(z->out);

	// compute color-based transparency, assuming we've
	// already got 65535 as the alpha value in the output
	if (out_n == 2)
	{
		for (i = 0; i < pixel_count; ++i)
		{
			p[1] = (p[0] == tc[0] ? 0 : 65535);
			p += 2;
		}
	}
	else if (out_n == 4)
	{
		for (i = 0; i < pixel_count; ++i)
		{
			if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
				p[3] = 0;
			p += 4;
		}
	}
	else
	{
		return 0;
	}
	return 1;
}

static int stbi__expand_png_palette(stbi__png *a, std::uint8_t *palette, int len, int pal_img_n)
{
	std::uint32_t i, pixel_count = a->s->img_x * a->s->img_y;
	std::uint8_t *orig = a->out;

	auto *p = stbi__malloc_fma2<std::uint8_t>(pixel_count, pal_img_n, 0);
	if (p == nullptr)
		return stbi__err("outofmem", "Out of memory");

	// between here and free(out) below, exitting would leak
	std::uint8_t *temp_out = p;

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

	return 1;
}



#define STBI__PNG_TYPE(a,b,c,d)  (((unsigned) (a) << 24) + ((unsigned) (b) << 16) + ((unsigned) (c) << 8) + (unsigned) (d))

static int stbi__parse_png_file(stbi__png *z, int scan, int req_comp)
{
	std::uint8_t palette[1024], pal_img_n = 0;
	std::uint8_t has_trans = 0, tc[3] = {0};
	std::uint16_t tc16[3];
	std::uint32_t ioff = 0, idata_limit = 0, i, pal_len = 0;
	int first = 1, k, interlace = 0, color = 0, is_iphone = 0;
	stbi__context *s = z->s;

	z->expanded = nullptr;
	z->idata = nullptr;
	z->out = nullptr;

	if (!s->stbi__check_png_header())
		return 0;

	for (;;)
	{
		stbi__pngchunk c;
		c.length = s->stbi__get32be();
		c.type = s->stbi__get32be();
		switch (c.type)
		{
			case STBI__PNG_TYPE('I', 'H', 'D', 'R'): {
				if (!first)
					return stbi__err("multiple IHDR", "Corrupt PNG");
				first = 0;
				if (c.length != 13)
					return stbi__err("bad IHDR len", "Corrupt PNG");
				s->img_x = s->stbi__get32be();
				s->img_y = s->stbi__get32be();
				if (s->img_y > STBI_MAX_DIMENSIONS)
					return stbi__err("too large", "Very large image (corrupt?)");
				if (s->img_x > STBI_MAX_DIMENSIONS)
					return stbi__err("too large", "Very large image (corrupt?)");
				z->depth = s->stbi__get8();
				if (z->depth != 1 && z->depth != 2 && z->depth != 4 && z->depth != 8 && z->depth != 16)
					return stbi__err("1/2/4/8/16-bit only", "PNG not supported: 1/2/4/8/16-bit only");
				color = s->stbi__get8();
				if (color > 6)
					return stbi__err("bad ctype", "Corrupt PNG");
				if (color == 3 && z->depth == 16)
					return stbi__err("bad ctype", "Corrupt PNG");
				if (color == 3)
					pal_img_n = 3;
				else if (color & 1)
					return stbi__err("bad ctype", "Corrupt PNG");
				int comp = s->stbi__get8();
				if (comp)
					return stbi__err("bad comp method", "Corrupt PNG");
				int filter = s->stbi__get8();
				if (filter)
					return stbi__err("bad filter method", "Corrupt PNG");
				interlace = s->stbi__get8();
				if (interlace > 1)
					return stbi__err("bad interlace method", "Corrupt PNG");
				if (!s->img_x || !s->img_y)
					return stbi__err("0-pixel image", "Corrupt PNG");
				if (!pal_img_n)
				{
					s->img_n = (color & 2 ? 3 : 1) + (color & 4 ? 1 : 0);
					if ((1 << 30) / s->img_x / s->img_n < s->img_y)
						return stbi__err("too large", "Image too large to decode");
				}
				else
				{
					// if paletted, then pal_n is our final components, and
					// img_n is # components to decompress/filter.
					s->img_n = 1;
					if ((1 << 30) / s->img_x / 4 < s->img_y)
						return stbi__err("too large", "Corrupt PNG");
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
					palette[i * 4 + 0] = s->stbi__get8();
					palette[i * 4 + 1] = s->stbi__get8();
					palette[i * 4 + 2] = s->stbi__get8();
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
						palette[i * 4 + 3] = s->stbi__get8();
				}
				else
				{
					if (!(s->img_n & 1)) return stbi__err("tRNS with alpha", "Corrupt PNG");
					if (c.length != (std::uint32_t) s->img_n * 2) return stbi__err("bad tRNS len", "Corrupt PNG");
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
							tc16[k] = (std::uint16_t) s->stbi__get16be(); // copy the values as-is
					}
					else
					{
						for (k = 0; k < s->img_n && k < 3; ++k)
							tc[k] = (std::uint8_t) (s->stbi__get16be() & 255) * stbi__depth_scale_table[z->depth];
						// non 8-bit images will be larger
					}
				}
				break;
			}

			case STBI__PNG_TYPE('I', 'D', 'A', 'T'): {
				if (first)
					return stbi__err("first not IHDR", "Corrupt PNG");
				if (pal_img_n && !pal_len)
					return stbi__err("no PLTE", "Corrupt PNG");
				if (scan == STBI__SCAN_header)
				{
					// header scan definitely stops at first IDAT
					if (pal_img_n)
						s->img_n = pal_img_n;
					return 1;
				}
				if (c.length > (1u << 30))
					return stbi__err("IDAT size limit", "IDAT section larger than 2^30 bytes");
				if ((int) (ioff + c.length) < (int) ioff) return 0;
				if (ioff + c.length > idata_limit)
				{
					std::uint8_t *p;
					if (idata_limit == 0) idata_limit = c.length > 4096 ? c.length : 4096;
					while (ioff + c.length > idata_limit)
						idata_limit *= 2;
					p = static_cast<std::uint8_t *>(STBI_REALLOC_SIZED(z->idata, idata_limit_old, idata_limit));
					if (p == nullptr)
						return stbi__err("outofmem", "Out of memory");
					z->idata = p;
				}
				if (!stbi__getn(s, z->idata + ioff, c.length))
					return stbi__err("outofdata", "Corrupt PNG");
				ioff += c.length;
				break;
			}

			case STBI__PNG_TYPE('I', 'E', 'N', 'D'): {
				std::uint32_t raw_len, bpl;
				if (first)
					return stbi__err("first not IHDR", "Corrupt PNG");
				if (scan != STBI__SCAN_load)
					return 1;
				if (z->idata == NULL)
					return stbi__err("no IDAT", "Corrupt PNG");
				// initial guess for decoded data size to avoid unnecessary reallocs
				bpl = (s->img_x * z->depth + 7) / 8; // bytes per line, per component
				raw_len = bpl * s->img_y * s->img_n /* pixels */ + s->img_y /* filter mode per row */;
				z->expanded = Buffer::zlib_decode_malloc_guesssize_headerflag(
					z->idata,
					ioff,
					raw_len,
					(int *)&raw_len,
					!is_iphone
				);
				if (z->expanded == NULL) return 0; // zlib should set error
				STBI_FREE(z->idata);
				z->idata = NULL;
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
				z->expanded = nullptr;
				// end of PNG chunk, read and skip CRC
				s->stbi__get32be();
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
		s->stbi__get32be();
	}
}

static void *stbi__png_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
	stbi__png p;
	p.s = s;

	void *result = nullptr;
	if (req_comp < 0 || req_comp > 4)
		return stbi__errpuc("bad req_comp", "Internal error");
	if (stbi__parse_png_file(&p, STBI__SCAN_load, req_comp))
	{
		if (p.depth <= 8)
			ri->bits_per_channel = 8;
		else if (p.depth == 16)
			ri->bits_per_channel = 16;
		else
			return stbi__errpuc("bad bits_per_channel", "PNG not supported: unsupported color depth");
		result = p.out;
		p.out = nullptr;
		if (req_comp && req_comp != p.s->img_out_n)
		{
			if (ri->bits_per_channel == 8)
				result = stbi__convert_format((std::uint8_t*) result, p.s->img_out_n, req_comp, p.s->img_x, p.s->img_y);
			else
				result = stbi__convert_format16((std::uint16_t*) result, p.s->img_out_n, req_comp, p.s->img_x, p.s->img_y);
			p.s->img_out_n = req_comp;
			if (result == nullptr) return result;
		}
		*x = p.s->img_x;
		*y = p.s->img_y;
		if (comp != nullptr)
			*comp = p.s->img_n;
	}
	STBI_FREE(p.out); p.out = nullptr;
	STBI_FREE(p.expanded); p.expanded = nullptr;
	STBI_FREE(p.idata); p.idata = nullptr;

	return result;
}

static int stbi__png_test(stbi__context *s)
{
	int r = s->stbi__check_png_header();
	s->stbi__rewind();
	return r;
}


STBIDEF int stbi_info_from_memory(std::uint8_t const *buffer, int len, int *x, int *y, int *comp)
{
	stbi__context s;
	s.stbi__start_mem(buffer, len);
	stbi__png p;
	p.s = &s;
	if (!stbi__parse_png_file(&p, STBI__SCAN_header, 0))
	{
		p.s->stbi__rewind();
		return stbi__err("unknown image type", "Image not of any known type, or corrupt");
	}
	if (x != nullptr)
		*x = p.s->img_x;
	if (y != nullptr)
		*y = p.s->img_y;
	if (comp != nullptr)
		*comp = p.s->img_n;
	return true;
}


STBIDEF std::uint8_t *stbi_load_from_memory(std::uint8_t const *buffer, int len, int *x, int *y, int *comp, int req_comp)
{
	stbi__context s;
	s.stbi__start_mem(buffer, len);
	return stbi__load_and_postprocess_8bit(&s, x, y, comp, req_comp);
}

STBIDEF const char *stbi_failure_reason(void)
{
	return stbi__g_failure_reason;
}