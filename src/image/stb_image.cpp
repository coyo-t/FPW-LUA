#include "./stb_image.hpp"

#include <cstring>

#include <cstdint>

#include "./zlib.hpp"


static constexpr auto FOURCC (const char (&items)[5]) -> uint32_t
{
	static constexpr auto
	PNG_TYPE = [](uint8_t a, uint8_t b, uint8_t c, uint8_t d) -> uint32_t {
		return (
			(static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
			(static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d)
		);
	};
	return PNG_TYPE(
		static_cast<uint8_t>(items[0]),
		static_cast<uint8_t>(items[1]),
		static_cast<uint8_t>(items[2]),
		static_cast<uint8_t>(items[3])
	);
}


static_assert(sizeof(uint32_t) == 4);

template<typename T>
static auto compute_luma (T r, T g, T b) -> T
{
	const auto ir = static_cast<uint32_t>(r);
	const auto ig = static_cast<uint32_t>(g);
	const auto ib = static_cast<uint32_t>(b);
	return static_cast<T>((ir * 77 + ig * 150 + ib * 29) >> 8);
}

static auto stbi_free (void* p) -> void
{
	std::free(p);
}

static auto stb_realloc (void* p, size_t oldsz, size_t newsz) -> void*
{
	return std::realloc(p,newsz);
}

static constexpr auto STBI_MAX_DIMENSIONS = 1 << 24;

constexpr size_t MAX_ALLOCATIONS = 128;

struct Allocation
{
	uint32_t magic = 292202;
	bool freed = true;
	size_t size = 0;
	Allocation* next;

	Allocation()
	{
		next = this;
	}

	auto data () -> void*
	{
		return reinterpret_cast<uint8_t*>(this) + sizeof(Allocation);
	}

	static auto get_address (void* from) -> Allocation*
	{
		return reinterpret_cast<Allocation*>(static_cast<uint8_t*>(from) - sizeof(Allocation));
	}
};

struct DecodeContext
{
	size_t image_wide = 0;
	size_t image_tall = 0;
	size_t image_component_count = 0;
	size_t img_out_n = 0;

	uint8_t* img_buffer;
	uint8_t* img_buffer_end;
	uint8_t* img_buffer_original;
	uint8_t* img_buffer_original_end;

	size_t allocate_index = 0;
	Allocation head;
	AllocatorCallback* allocator;


	DecodeContext (uint8_t const *buffer, size_t size, AllocatorCallback* allocator)
	{
		// initialize a memory-decode context
		const auto cbi = const_cast<uint8_t *>(buffer);
		const auto cbe = cbi + size;
		img_buffer = cbi;
		img_buffer_original = cbi;
		img_buffer_end = cbe;
		img_buffer_original_end = cbe;
		head.next = &head;
		this->allocator = allocator;
	}

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
	auto rewind () -> void
	{
		// conceptually rewind SHOULD rewind to the beginning of the stream,
		// but we just rewind to the beginning of the initial buffer, because
		// we only use it after doing 'test', which only ever looks at at most 92 bytes
		img_buffer = img_buffer_original;
		img_buffer_end = img_buffer_original_end;
	}

	auto allocate (size_t size) -> void*
	{
		if (allocate_index >= MAX_ALLOCATIONS)
		{
			return nullptr;
		}

		auto node = &head;
		Allocation* maybe_freed = nullptr;
		while (true)
		{
			node = node->next;
			if (node == &head)
			{
				break;
			}
			if (node->freed && node->size <= size)
			{
				maybe_freed = node;
				break;
			}
		}

		if (maybe_freed != nullptr)
		{
			maybe_freed->freed = false;
			return maybe_freed->data();
		}

		const auto outs = allocator(size + sizeof(Allocation));

		if (outs == nullptr)
		{
			return nullptr;
		}

		const auto entry = static_cast<Allocation*>(outs);
		entry->size = size;
		entry->freed = false;
		entry->next = head.next;
		head.next = entry;
		allocate_index++;
		return static_cast<uint8_t*>(outs) + sizeof(Allocation);
	}

	auto free (void* thing) -> void
	{
		auto entry = Allocation::get_address(thing);
		if (entry->magic != 292202)
		{
			throw STBIErr("tried freeing a bogus block!");
		}
		if (entry->freed)
		{
			throw STBIErr("tried freeing an already freed block! this is a mistake!");
		}
		entry->freed = true;
	}

	auto reallocate (void* thing, size_t news) -> void*
	{
		if (news == 0)
		{
			free(thing);
			return nullptr;
		}
		auto a = Allocation::get_address(thing);
		if (a->size >= news)
		{
			return thing;
		}
		return allocate(news);
	}

	template<typename T>
	auto allocate_t (size_t count) -> T*
	{
		return static_cast<T*>(allocate(count * sizeof(T)));
	}

	auto free_all_blocks () -> void
	{
		auto node = &head;
		auto next = node->next;
		// no stale reference
		head.next = &head;
		while (true)
		{
			if (next == &head)
			{
				break;
			}
			// weirdo way of walking the list because if we free
			// node, then node.next may or may not poof out of existance
			node = next;
			next = node->next;
		}
	}
};


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


// stbi__err - error
// stbi__errpuc - error returning pointer to unsigned char


#define stbi__err(x,y)  STBIErr(x)

enum
{
	STBI__SCAN_load = 0,
	STBI__SCAN_header
};


template<typename T>
auto BYTECAST (T x) -> uint8_t
{
	return static_cast<uint8_t>(x & 0xFF);
}

struct PNGChunk
{
	uint32_t length;
	uint32_t type;
};


static auto check_png_header (DecodeContext &s) -> void
{
	static const uint8_t png_sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	for (const auto i: png_sig)
	{
		if (s.get8() != i)
		{
			// "Not a PNG"
			throw STBIErr("incorrect PNG magic");
		}
	}
}

struct PNG
{
	DecodeContext& context;
	uint8_t *idata = nullptr;
	uint8_t *expanded = nullptr;
	uint8_t *out = nullptr;
	int pixel_bit_depth = 0;

	explicit PNG (DecodeContext& ctx): context(ctx)
	{
	}
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
static void create_png_alpha_expand8(
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
static int create_png_image_raw(
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
	auto s = a->context;
	uint32_t i;
	auto stride = x * out_n * bytes;
	int k;
	auto img_n = s.image_component_count; // copy it into a local for later

	auto output_bytes = out_n * bytes;
	auto filter_bytes = img_n * bytes;
	auto width = x;

	if (out_n != s.image_component_count && out_n != s.image_component_count+1)
	{
		throw STBIErr("assertion error: out_n != component count");
	}
	a->out = s.allocate_t<uint8_t>(x * y * output_bytes);
	if (!a->out)
	{
		throw STBIErr("out of memory");
	}

	// note: error exits here don't need to clean up a->out individually,
	// stbi__do_png always does on error.
	if (!fma3sizes_valid(img_n, x, depth, 7))
	{
		throw STBIErr("image too large");
	}
	auto img_width_bytes = (((img_n * x * depth) + 7) >> 3);
	if (!fma2sizes_valid(img_width_bytes, y, img_width_bytes))
	{
		throw STBIErr("image too large");
	}
	auto img_len = (img_width_bytes + 1) * y;

	// we used to check for exact match between raw_len and img_len on non-interlaced PNGs,
	// but issue #276 reported a PNG in the wild that had extra data at the end (all zeros),
	// so just check for raw_len < img_len always.
	if (raw_len < img_len)
	{
		throw STBIErr("not enough pixels");
	}

	// Allocate two scan lines worth of filter workspace buffer.
	auto filter_buf = s.allocate_t<uint8_t>(img_width_bytes * 2);
	if (!filter_buf)
	{
		throw STBIErr("out of memory");
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
			throw STBIErr("invalid filter");
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
				std::memcpy(cur, raw, nk);
				break;
			case STBI__F_sub:
				std::memcpy(cur, raw, filter_bytes);
				for (k = filter_bytes; k < nk; ++k)
				{
					cur[k] = BYTECAST(raw[k] + cur[k-filter_bytes]);
				}
				break;
			case STBI__F_up:
				for (k = 0; k < nk; ++k)
				{
					cur[k] = BYTECAST(raw[k] + prior[k]);
				}
				break;
			case STBI__F_avg:
				for (k = 0; k < filter_bytes; ++k)
				{
					cur[k] = BYTECAST(raw[k] + (prior[k]>>1));
				}
				for (k = filter_bytes; k < nk; ++k)
				{
					cur[k] = BYTECAST(raw[k] + ((prior[k] + cur[k-filter_bytes])>>1));
				}
				break;
			case STBI__F_paeth:
				for (k = 0; k < filter_bytes; ++k)
				{
					cur[k] = BYTECAST(raw[k] + prior[k]);
				}
				for (k = filter_bytes; k < nk; ++k)
				{
					cur[k] = BYTECAST(raw[k] + paeth(cur[k-filter_bytes], prior[k], prior[k-filter_bytes]));
				}
				break;
			case STBI__F_avg_first:
				std::memcpy(cur, raw, filter_bytes);
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
			auto scale = (color == 0) ? DEPTH_SCALE_TABLE[depth] : 1;
			auto in = cur;
			auto out = dest;
			uint8_t inb = 0;
			auto nsmp = x * img_n;

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
			else if (depth == 1)
			{
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
				create_png_alpha_expand8(dest, dest, x, img_n);
			}
		}
		else if (depth == 8)
		{
			if (img_n == out_n)
			{
				std::memcpy(dest, cur, x * img_n);
			}
			else
			{
				create_png_alpha_expand8(dest, cur, x, img_n);
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
				if (img_n + 1 != out_n)
				{
					throw STBIErr("assertion failure: img_n + 1 == out_n");
				}
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
					if (img_n != 3)
					{
						throw STBIErr("assertion failure: img_n == 3");
					}
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
	return 1;
}

static int parse_png_file(PNG& z, size_t scan, size_t req_comp)
{
	auto s = z.context;

	z.expanded = nullptr;
	z.idata = nullptr;
	z.out = nullptr;

	check_png_header(s);

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
		PNGChunk chunkc;
		chunkc.length = s.get32be();
		chunkc.type = s.get32be();
		switch (chunkc.type)
		{
			case FOURCC("CgBI"): {
				is_iphone = 1;
				s.skip(chunkc.length);
				break;
			}
			case FOURCC("IHDR"): {
				if (!first)
				{
					throw stbi__err("multiple IHDR", "Corrupt PNG");
				}
				first = 0;
				if (chunkc.length != 13)
				{
					throw stbi__err("bad IHDR len", "Corrupt PNG");
				}
				s.image_wide = s.get32be();
				s.image_tall = s.get32be();
				if (s.image_tall > STBI_MAX_DIMENSIONS)
				{
					throw stbi__err("too large", "Very large image (corrupt?)");
				}
				if (s.image_wide > STBI_MAX_DIMENSIONS)
				{
					throw stbi__err("too large", "Very large image (corrupt?)");
				}
				z.pixel_bit_depth = s.get8();
				if (z.pixel_bit_depth != 1 && z.pixel_bit_depth != 2 && z.pixel_bit_depth != 4 && z.pixel_bit_depth != 8 && z.pixel_bit_depth != 16)
				{
					throw STBIErr("PNG not supported: 1/2/4/8/16-bit only");
				}
				color = s.get8();
				if (color > 6)
				{
					throw stbi__err("bad ctype", "Corrupt PNG");
				}
				if (color == 3 && z.pixel_bit_depth == 16)
				{
					throw stbi__err("bad ctype", "Corrupt PNG");
				}
				if (color == 3)
				{
					pal_img_n = 3;
				}
				else if (color & 1)
				{
					throw stbi__err("bad ctype", "Corrupt PNG");
				}
				int comp = s.get8();
				if (comp)
				{
					throw stbi__err("bad comp method", "Corrupt PNG");
				}
				int filter = s.get8();
				if (filter)
				{
					throw stbi__err("bad filter method", "Corrupt PNG");
				}
				interlace = s.get8();
				if (interlace > 1)
				{
					throw stbi__err("bad interlace method", "Corrupt PNG");
				}
				if (s.image_wide <= 0 || s.image_tall <= 0)
				{
					throw STBIErr("image has 0 dimensions in an axis");
				}
				if (pal_img_n)
				{
					// if paletted, then pal_n is our final components, and
					// img_n is # components to decompress/filter.
					s.image_component_count = 1;
					if ((1 << 30) / s.image_wide / 4 < s.image_tall)
					{
						throw stbi__err("too large", "Corrupt PNG");
					}
				}
				else
				{
					s.image_component_count = (color & 2 ? 3 : 1) + (color & 4 ? 1 : 0);
					if ((1 << 30) / s.image_wide / s.image_component_count < s.image_tall)
					{
						throw STBIErr("image too large to decode");
					}
				}
				// even with SCAN_header, have to scan to see if we have a tRNS
				break;
			}
			case FOURCC("PLTE"): {
				if (first)
				{
					throw stbi__err("first not IHDR", "Corrupt PNG");
				}
				if (chunkc.length > 256 * 3)
				{
					throw stbi__err("invalid PLTE", "Corrupt PNG");
				}
				pal_len = chunkc.length / 3;
				if (pal_len * 3 != chunkc.length)
				{
					throw stbi__err("invalid PLTE", "Corrupt PNG");
				}
				for (i = 0; i < pal_len; ++i)
				{
					palette[i * 4 + 0] = s.get8();
					palette[i * 4 + 1] = s.get8();
					palette[i * 4 + 2] = s.get8();
					palette[i * 4 + 3] = 255;
				}
				break;
			}
			case FOURCC("tRNS"): {
				if (first)
				{
					throw stbi__err("first not IHDR", "Corrupt PNG");
				}
				if (z.idata)
				{
					throw stbi__err("tRNS after IDAT", "Corrupt PNG");
				}
				if (pal_img_n)
				{
					if (scan == STBI__SCAN_header)
					{
						s.image_component_count = 4;
						return 1;
					}
					if (pal_len == 0)
					{
						throw stbi__err("tRNS before PLTE", "Corrupt PNG");
					}
					if (chunkc.length > pal_len)
					{
						throw stbi__err("bad tRNS len", "Corrupt PNG");
					}
					pal_img_n = 4;
					for (i = 0; i < chunkc.length; ++i)
					{
						palette[i * 4 + 3] = s.get8();
					}
				}
				else
				{
					if (!(s.image_component_count & 1))
					{
						throw stbi__err("tRNS with alpha", "Corrupt PNG");
					}
					if (chunkc.length != s.image_component_count * 2)
					{
						throw stbi__err("bad tRNS len", "Corrupt PNG");
					}
					has_trans = 1;
					// non-paletted with tRNS = constant alpha. if header-scanning, we can stop now.
					if (scan == STBI__SCAN_header)
					{
						++s.image_component_count;
						return true;
					}
					if (z.pixel_bit_depth == 16)
					{
						for (k = 0; k < s.image_component_count && k < 3; ++k) // extra loop test to suppress false GCC warning
						{
							tc16[k] = s.get16be(); // copy the values as-is
						}
					}
					else
					{
						for (k = 0; k < s.image_component_count && k < 3; ++k)
						{
							tc[k] = static_cast<uint8_t>(s.get16be() & 255) * DEPTH_SCALE_TABLE[z.pixel_bit_depth];
						}
						// non 8-bit images will be larger
					}
				}
				break;
			}
			case FOURCC("IDAT"): {
				if (first)
				{
					throw stbi__err("first not IHDR", "Corrupt PNG");
				}
				if (pal_img_n && !pal_len)
				{
					throw stbi__err("no PLTE", "Corrupt PNG");
				}
				if (scan == STBI__SCAN_header)
				{
					// header scan definitely stops at first IDAT
					if (pal_img_n)
					{
						s.image_component_count = pal_img_n;
					}
					return true;
				}
				if (chunkc.length > (1u << 30))
				{
					throw stbi__err("IDAT size limit", "IDAT section larger than 2^30 bytes");
				}
				if ((int) (ioff + chunkc.length) < (int) ioff)
				{
					return false;
				}
				if (ioff + chunkc.length > idata_limit)
				{
					uint32_t idata_limit_old = idata_limit;
					if (idata_limit == 0)
					{
						idata_limit = chunkc.length > 4096 ? chunkc.length : 4096;
					}
					while (ioff + chunkc.length > idata_limit)
					{
						idata_limit *= 2;
					}

					auto p = static_cast<uint8_t*>(stb_realloc(z.idata, idata_limit_old, idata_limit));
					if (p == nullptr)
					{
						throw stbi__err("outofmem", "Out of memory");
					}
					z.idata = p;
				}
				const auto n = chunkc.length;
				const auto buffer = z.idata + ioff;
				if (s.img_buffer + n > s.img_buffer_end)
				{
					throw stbi__err("outofdata", "Corrupt PNG");
				}
				std::memcpy(buffer, s.img_buffer, n);
				s.img_buffer += n;
				ioff += chunkc.length;
				break;
			}
			case FOURCC("IEND"): {
				uint32_t raw_len, bpl;
				if (first)
				{
					throw stbi__err("first not IHDR", "Corrupt PNG");
				}
				if (scan != STBI__SCAN_load)
				{
					return true;
				}
				if (z.idata == nullptr)
				{
					throw stbi__err("no IDAT", "Corrupt PNG");
				}
				// initial guess for decoded data size to avoid unnecessary reallocs
				bpl = (s.image_wide * z.pixel_bit_depth + 7) / 8; // bytes per line, per component
				raw_len = bpl * s.image_tall * s.image_component_count /* pixels */ + s.image_tall /* filter mode per row */;

				auto zctx = Zlib::Context();
				zctx.buffer = z.idata;
				zctx.len = ioff;
				zctx.initial_size = raw_len;
				zctx.parse_header = !is_iphone;

				zctx.allocation_self = &s;
				zctx.malloc = [](auto self, auto size) {
					return static_cast<DecodeContext*>(self)->allocate(size);
				};
				zctx.free = [](auto self, auto p) {
					static_cast<DecodeContext*>(self)->free(p);
				};
				zctx.realloc = [](auto self, auto p, auto olds, auto news) {
					return static_cast<DecodeContext*>(self)->reallocate(p, news);
				};

				try
				{
					z.expanded = zctx.decode_malloc_guesssize_headerflag();
				}
				catch (Zlib::Err& er)
				{
					throw STBIErr(er.reason);
				}

				raw_len = zctx.out_len;

				if (z.expanded == nullptr)
				{
					throw stbi__err("z->expanded == nullptr", "");
				}
				stbi_free(z.idata);
				z.idata = nullptr;
				if ((req_comp == s.image_component_count + 1 && req_comp != 3 && !pal_img_n) || has_trans)
				{
					s.img_out_n = s.image_component_count + 1;
				}
				else
				{
					s.img_out_n = s.image_component_count;
				}
				const auto pic_wide = z.context.image_wide;
				const auto pic_tall = z.context.image_tall;
				{
					auto _image_data = z.expanded;
					auto _image_data_len = raw_len;
					auto _out_n = s.img_out_n;
					auto _depth = z.pixel_bit_depth;
					auto _color = color;
					auto _interlaced = interlace;
					if (!_interlaced)
					{
						return create_png_image_raw(
							&z,
							_image_data,
							_image_data_len,
							_out_n,
							pic_wide,
							pic_tall,
							_depth,
							_color
						);
					}

					// de-interlacing
					auto out_bytes = _out_n * (_depth == 16 ? 2 : 1);
					auto final = s.allocate_t<uint8_t>(pic_wide * pic_tall * out_bytes);
					if (!final)
					{
						throw STBIErr("out of memory");
					}
					for (int p = 0; p < 7; ++p)
					{
						int xorig[] = {0, 4, 0, 2, 0, 1, 0};
						int yorig[] = {0, 0, 4, 0, 2, 0, 1};
						int xspc[] = {8, 8, 4, 4, 2, 2, 1};
						int yspc[] = {8, 8, 8, 4, 4, 2, 2};
						// pass1_x[4] = 0, pass1_x[5] = 1, pass1_x[12] = 1
						auto x = (pic_wide - xorig[p] + xspc[p] - 1) / xspc[p];
						auto y = (pic_tall - yorig[p] + yspc[p] - 1) / yspc[p];
						if (x && y)
						{
							auto img_len = ((((z.context.image_component_count * x * _depth) + 7) >> 3) + 1) * y;
							if (!create_png_image_raw(&z, _image_data, _image_data_len, _out_n, x, y, _depth, _color))
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
										final + out_y * pic_wide * out_bytes + out_x * out_bytes,
										z.out + (jj * x + ii) * out_bytes,
										out_bytes
									);
								}
							}
							stbi_free(z.out);
							_image_data += img_len;
							_image_data_len -= img_len;
						}
					}
					z.out = final;
				}
				if (has_trans)
				{
					if (z.pixel_bit_depth == 16)
					{

						auto outn = s.img_out_n;
						auto pixel_count = pic_wide * pic_tall;
						auto p = reinterpret_cast<uint16_t *>(z.out);

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
						auto outn = s.img_out_n;
						auto pixel_count = pic_wide * pic_tall;
						auto p = z.out;

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
					s.image_component_count = pal_img_n; // record the actual colors we had
					s.img_out_n = pal_img_n;
					if (req_comp >= 3)
					{
						s.img_out_n = req_comp;
					}

					auto pal_img_n2 = s.img_out_n;
					auto pixel_count = pic_wide * pic_tall;
					auto orig = z.out;

					auto p = s.allocate_t<uint8_t>(pixel_count * pal_img_n2);
					if (p == nullptr)
					{
						throw STBIErr("out of memory");
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
					s.free(z.out);
					z.out = temp_out;
				}
				else if (has_trans)
				{
					// non-paletted image with tRNS -> source image has (constant) alpha
					++s.image_component_count;
				}
				s.free(z.expanded);
				z.expanded = nullptr;
				// end of PNG chunk, read and skip CRC
				s.get32be();
				return 1;
			}
			default: {
				// if critical, fail
				if (first)
				{
					throw stbi__err("first not IHDR", "Corrupt PNG");
				}
				if ((chunkc.type & (1 << 29)) == 0)
				{
					// invalid_chunk[0] = STBI__BYTECAST(c.type >> 24);
					// invalid_chunk[1] = STBI__BYTECAST(c.type >> 16);
					// invalid_chunk[2] = STBI__BYTECAST(c.type >> 8);
					// invalid_chunk[3] = STBI__BYTECAST(c.type >> 0);
					throw stbi__err("XXXX PNG chunk not known", "PNG not supported: unknown PNG chunk type");
				}
				s.skip(chunkc.length);
				break;
			}
		}
		// end of PNG chunk, read and skip CRC
		s.get32be();
	}
}

auto get_valuessss (
	uint64_t *out_wide,
	uint64_t *out_tall,
	DecodeContext s,
	size_t bits_per_channel
) -> uint8_t*
{
	auto req_comp = 4;
	void *true_result;
	auto p = PNG(s);
	void *result = nullptr;
	if (parse_png_file(p, STBI__SCAN_load, req_comp))
	{
		if (p.pixel_bit_depth <= 8)
		{
			bits_per_channel = 8;
		}
		else if (p.pixel_bit_depth == 16)
		{
			bits_per_channel = 16;
		}
		else
		{
			throw STBIErr("PNG not supported: unsupported color depth");
		}
		result = p.out;
		p.out = nullptr;
		if (req_comp != p.context.img_out_n)
		{
			static constexpr auto COMBO = [](size_t a, size_t b) { return (a<<3)|b; };
			const auto img_components = p.context.img_out_n;
			const auto src_ofs = img_components;
			const auto dst_ofs = req_comp;
			const auto xx = p.context.image_wide;
			const auto yy = p.context.image_tall;
			if (bits_per_channel == 8)
			{
				auto data = static_cast<uint8_t*>(result);

				if (req_comp == img_components)
				{
					result = data;
					goto endp;
				}

				// auto good = stbi_malloc_t<uint8_t>(req_comp * xx * yy);
				auto good = s.allocate_t<uint8_t>(req_comp * xx * yy);
				if (good == nullptr)
				{
					stbi_free(data);
					throw STBIErr("out of memory");
				}

				auto (*CONV_FUNC)(uint8_t* src, uint8_t* dst) -> void = nullptr;

				switch (COMBO(img_components, req_comp))
				{
					case COMBO(1,2): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = src[0];
							dest[1] = 255;
						};
						break;
					}
					case COMBO(1,3): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = dest[1] = dest[2] = src[0];
						};
						break;
					}
					case COMBO(1,4): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = dest[1] = dest[2] = src[0];
							dest[3] = 255;
						};
						break;
					}
					case COMBO(2,1): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = src[0];
						};
						break;
					}
					case COMBO(2,3): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = dest[1] = dest[2] = src[0];
						};
						break;
					}
					case COMBO(2,4): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = dest[1] = dest[2] = src[0];
							dest[3] = src[1];
						};
						break;
					}
					case COMBO(3,4): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = src[0];
							dest[1] = src[1];
							dest[2] = src[2];
							dest[3] = 255;
						};
						break;
					}
					case COMBO(3,1): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = compute_luma(src[0], src[1], src[2]);
						};
						break;
					}
					case COMBO(3,2): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = compute_luma(src[0], src[1], src[2]);
							dest[1] = 255;
						};
						break;
					}
					case COMBO(4,1): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = compute_luma(src[0], src[1], src[2]);
						};
						break;
					}
					case COMBO(4,2): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = compute_luma(src[0], src[1], src[2]);
							dest[1] = src[3];
						};
						break;
					}
					case COMBO(4,3): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = src[0];
							dest[1] = src[1];
							dest[2] = src[2];
						};
						break;
					}
					default: {
						CONV_FUNC = nullptr;
						break;
					}
				}

				if (CONV_FUNC == nullptr)
				{
					stbi_free(data);
					stbi_free(good);
					throw STBIErr("unsupported format conversion");
				}

				for (auto j = 0; j < yy; ++j)
				{
					auto src = data + j * xx * img_components;
					auto dst = good + j * xx * req_comp;

					// FIXME: ditch the scanline approach, do whole image at once
					for(auto i=xx-1;; --i)
					{
						CONV_FUNC(src, dst);
						src += src_ofs;
						dst += dst_ofs;
						if (i == 0)
						{
							break;
						}
					}
				}

				stbi_free(data);
				result = good;
			}
			else
			{
				auto data = static_cast<uint16_t*>(result);
				if (req_comp == img_components)
				{
					result = data;
					goto endp;
				}

				auto good = s.allocate_t<uint16_t>(req_comp * xx * yy);
				if (good == nullptr)
				{
					stbi_free(data);
					throw STBIErr("out of memory");
				}

				auto (*CONV_FUNC)(uint16_t* src, uint16_t* dst) -> void = nullptr;

				switch (COMBO(img_components, req_comp))
				{
					case COMBO(1,2): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = src[0];
							dest[1] = 0xffff;
						};
						break;
					}
					case COMBO(1,3): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = dest[1] = dest[2] = src[0];
						};
						break;
					}
					case COMBO(1,4): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = dest[1] = dest[2] = src[0];
							dest[3] = 0xffff;
						};
						break;
					}
					case COMBO(2,1): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = src[0];
						};
						break;
					}
					case COMBO(2,3): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = dest[1] = dest[2] = src[0];
						};
						break;
					}
					case COMBO(2,4): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = dest[1] = dest[2] = src[0];
							dest[3] = src[1];
						};
						break;
					}
					case COMBO(3,4): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = src[0];
							dest[1] = src[1];
							dest[2] = src[2];
							dest[3] = 0xffff;
						};
						break;
					}
					case COMBO(3,1): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = compute_luma(src[0], src[1], src[2]);
						};
						break;
					}
					case COMBO(3,2): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = compute_luma(src[0], src[1], src[2]);
							dest[1] = 0xffff;
						};
						break;
					}
					case COMBO(4,1): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = compute_luma(src[0], src[1], src[2]);
						};
						break;
					}
					case COMBO(4,2): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = compute_luma(src[0], src[1], src[2]);
							dest[1] = src[3];
						};
						break;
					}
					case COMBO(4,3): {
						CONV_FUNC = [](auto src, auto dest) {
							dest[0] = src[0];
							dest[1] = src[1];
							dest[2] = src[2];
						};
						break;
					}
					default: {
						CONV_FUNC = nullptr;
					}
				}

				if (CONV_FUNC == nullptr)
				{
					stbi_free(data);
					stbi_free(good);
					throw STBIErr("unsupported format conversion");
				}

				for (auto j = 0; j < yy; ++j)
				{
					auto src = data + j * xx * img_components;
					auto dest = good + j * xx * req_comp;

					// FIXME: ditch the scanline approach, do whole image at once
					for(auto i=xx-1;; --i)
					{
						CONV_FUNC(src, dest);
						src += src_ofs;
						dest += dst_ofs;
						if (i == 0)
						{
							break;
						}
					}
				}

				stbi_free(data);
				result = good;
			}
			endp:
			p.context.img_out_n = req_comp;
			if (result == nullptr)
			{
				true_result = result;
				goto trueend;
			}
		}
	}
	stbi_free(p.out);
	p.out = nullptr;
	stbi_free(p.expanded);
	p.expanded = nullptr;
	stbi_free(p.idata);
	p.idata = nullptr;

	true_result = result;
trueend:

	if (true_result == nullptr)
	{
		return nullptr;
	}

	// it is the responsibility of the loaders to make sure we get either 8 or 16 bit.
	if (bits_per_channel != 8 && bits_per_channel != 16)
	{
		throw STBIErr("assertion error: bits per channel isn't 8 or 16");
	}



	if (bits_per_channel != 8)
	{
		auto orig = static_cast<uint16_t *>(true_result);
		auto img_len = p.context.image_wide * p.context.image_tall * req_comp;

		auto reduced = s.allocate_t<uint8_t>(img_len);
		if (reduced == nullptr)
		{
			throw STBIErr("out of memory");
		}

		for (int i = 0; i < img_len; ++i)
		{
			// top half of each byte is sufficient approx of 16->8 bit scaling
			reduced[i] = static_cast<uint8_t>((orig[i] >> 8) & 0xFF);
		}
		s.free(orig);
		return reduced;
	}

	if (out_wide != nullptr)
	{
		*out_wide = p.context.image_wide;
	}
	if (out_tall != nullptr)
	{
		*out_tall = p.context.image_tall;
	}
	return static_cast<uint8_t*>(true_result);
}

auto coyote_stbi_load_from_memory(
	DllInterface *interface,
	uint64_t *out_wide,
	uint64_t *out_tall) -> uint8_t*
{
	try
	{
		if (interface->allocator == nullptr)
		{
			throw STBIErr("no memory allocator callback defined");
		}

		auto s = DecodeContext(
			interface->source_png_buffer,
			interface->source_png_size,
			interface->allocator
		);

		// default is 8 so most paths don't have to be changed
		size_t bits_per_channel = 8;

		// test the formats with a very explicit header first
		// (at least a FOURCC or distinctive magic number first)
		check_png_header(s);
		s.rewind();
		auto value = get_valuessss(
			out_wide,
			out_tall,
			s,
			bits_per_channel
		);
		if (value == nullptr)
			return nullptr;
		return value;
	}
	catch (STBIErr& e)
	{
		interface->set_failure(e.reason);
		return nullptr;
	}
}

auto coyote_stbi_info_from_memory(
	DllInterface *interface,
	uint64_t *out_pic_wide,
	uint64_t *out_pic_tall,
	uint64_t *out_required_output_size) -> uint32_t
{
	try
	{
		if (interface->allocator == nullptr)
		{
			throw STBIErr("no memory allocator callback defined");
		}
		auto s = DecodeContext(
			interface->source_png_buffer,
			interface->source_png_size,
			interface->allocator
		);
		auto p = PNG(s);
		parse_png_file(p, STBI__SCAN_header, 0);

		if (out_pic_wide != nullptr)
		{
			*out_pic_wide = p.context.image_wide;
		}
		if (out_pic_tall != nullptr)
		{
			*out_pic_tall = p.context.image_tall;
		}
		if (out_required_output_size != nullptr)
		{
			*out_required_output_size = (
				p.context.image_wide * p.context.image_tall * 4
			);
		}
	}
	catch (STBIErr& e)
	{
		return interface->set_failure(e.reason);
	}

	return true;
}

void coyote_stbi_image_free(void *retval_from_stbi_load)
{
	stbi_free(retval_from_stbi_load);
}

auto coyote_stbi_interface_sizeof() -> std::uint64_t
{
	return sizeof(DllInterface);
}

auto coyote_stbi_get_failure(DllInterface *res) -> const char *
{
	if (res->is_success)
	{
		return "not actually a failure dingus!!!";
	}
	return res->result.failure.reason;
}

auto coyote_stbi_get_success(DllInterface *res, uint64_t *out_size) -> uint8_t *
{
	if (!res->is_success)
	{
		return nullptr;
	}

	auto [s, p] = res->result.success;
	if (out_size != nullptr)
	{
		*out_size = s;
	}
	return p;
}

auto coyote_stbi_interface_setup(
	DllInterface *interface,
	uint8_t const *source_png_buffer,
	uint64_t source_png_size,
	uint64_t desired_channel_count,
	AllocatorCallback* allocator
) -> void
{
	interface->allocator = allocator;
	interface->source_png_buffer = source_png_buffer;
	interface->source_png_size = source_png_size;
	interface->is_success = false;
	interface->result.failure.reason = nullptr;
	interface->desired_channel_count = desired_channel_count == 0
		? 4
		: desired_channel_count;
}
