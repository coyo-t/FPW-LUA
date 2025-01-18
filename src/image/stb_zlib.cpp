
#include "stb_zlib.hpp"

#include <climits>

#include "stb_image.hpp"

#include <cstdint>
#include <cstring>

// public domain zlib decode    v0.2  Sean Barrett 2006-11-18
//    simple implementation
//      - all input must be provided in an upfront buffer
//      - all output is written to a single output buffer (can malloc/realloc)
//    performance
//      - fast huffman


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

static int bitreverse16(int n)
{
	n = ((n & 0xAAAA) >> 1) | ((n & 0x5555) << 1);
	n = ((n & 0xCCCC) >> 2) | ((n & 0x3333) << 2);
	n = ((n & 0xF0F0) >> 4) | ((n & 0x0F0F) << 4);
	n = ((n & 0xFF00) >> 8) | ((n & 0x00FF) << 8);
	return n;
}

static int bit_reverse(int v, int bits)
{
	// to bit reverse n bits, reverse 16 and shift
	// e.g. 11 bits, bit reverse and shift away 5
	return bitreverse16(v) >> (16 - bits);
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
		int code, next_code[16], sizes[17];

		// DEFLATE spec for generating codes
		memset(sizes, 0, sizeof(sizes));
		memset(this->fast, 0, sizeof(this->fast));
		for (i = 0; i < num; ++i)
			++sizes[sizelist[i]];
		sizes[0] = 0;
		for (i = 1; i < 16; ++i)
			if (sizes[i] > (1 << i))
				return stbi__err("bad sizes", "Corrupt PNG");
		code = 0;
		for (i = 1; i < 16; ++i)
		{
			next_code[i] = code;
			this->firstcode[i] = (std::uint16_t) code;
			this->firstsymbol[i] = (std::uint16_t) k;
			code = (code + sizes[i]);
			if (sizes[i])
				if (code - 1 >= (1 << i)) return stbi__err("bad codelengths", "Corrupt PNG");
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
		unsigned int cur, limit, old_limit;
		this->zout = zout;
		if (!this->z_expandable) return stbi__err("output buffer limit", "Corrupt PNG");
		cur = (unsigned int) (this->zout - this->zout_start);
		limit = old_limit = (unsigned) (this->zout_end - this->zout_start);
		if (UINT_MAX - cur < (unsigned) n) return stbi__err("outofmem", "Out of memory");
		while (cur + n > limit)
		{
			if (limit > UINT_MAX / 2) return stbi__err("outofmem", "Out of memory");
			limit *= 2;
		}
		q = (std::uint8_t*) STBI_REALLOC_SIZED(this->zout_start, old_limit, limit);
		STBI_NOTUSED(old_limit);
		if (q == NULL) return stbi__err("outofmem", "Out of memory");
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
					if (!this->zexpand(zout, len)) return 0;
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
	auto do_zlib(std::uint8_t* obuf, int olen, int exp, int parse_header) -> int
	{
		this->zout_start = obuf;
		this->zout = obuf;
		this->zout_end = obuf + olen;
		this->z_expandable = exp;

		return this->parse_zlib(parse_header);
	}
};


STBIDEF std::uint8_t* stbi_zlib_decode_malloc_guesssize_headerflag(
	const std::uint8_t *buffer,
	int len,
	int initial_size,
	int *outlen,
	int parse_header)
{
	Buffer a;
	auto p = new std::uint8_t[initial_size];
	if (p == nullptr) return nullptr;
	a.zbuffer = (std::uint8_t *) buffer;
	a.zbuffer_end = (std::uint8_t *) buffer + len;
	if (a.do_zlib(p, initial_size, 1, parse_header))
	{
		if (outlen) *outlen = (int) (a.zout - a.zout_start);
		return a.zout_start;
	}

	STBI_FREE(a.zout_start);
	return nullptr;
}
