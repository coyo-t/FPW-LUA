#include "./zlib.hpp"

#include <cstring>

namespace Coyote {

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

static constexpr auto LENGTH_EXTRA[31] = {
	0, 0, 0, 0, 0, 0, 0, 0, 1,
	1, 1, 1, 2, 2, 2, 2, 3, 3,
	3, 3, 4, 4, 4, 4, 5, 5, 5,
	5, 0, 0, 0,
};

static constexpr auto DIST_BASE[32] = {
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
	257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 0, 0
};

static constexpr auto DIST_EXTRA[32] = {
	0,  0,  0,  0,
	1,  1,  2,  2,
	3,  3,  4,  4,
	5,  5,  6,  6,
	7,  7,  8,  8,
	9,  9,  10, 10,
	11, 11, 12, 12,
	13, 13,
};


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

static constexpr Byte DEFAULT_LENGTH[NSYMS] = {
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9,
	7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7,
	8, 8, 8, 8, 8, 8, 8, 8,
};
static constexpr Byte DEFAULT_DISTANCE[32] = {
	5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5,
};

static constexpr Byte length_dezigzag[19] = {
	16, 17, 18, 0,
	8,  7,  9,  6,
	10, 5,  11, 4,
	12, 3,  13, 2,
	14, 1,  15,
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

struct Huffman {
	U16 fast[1 << FAST_BITS];
	U16 firstcode[16];
	int maxcode[17];
	U16 firstsymbol[16];
	Byte size[NSYMS];
	U16 value[NSYMS];

	auto zbuild_huffman(const Byte *sizelist, int num) -> bool;

	auto get_failure_reason() -> const char *;

private:
	const char* failure_reason = nullptr;

};

struct Huffer {
	Byte* zbuffer;
	Byte* zbuffer_end;
	int num_bits;
	int hit_zeof_once;
	U32 code_buffer;

	Byte* zout;
	Byte* zout_start;
	Byte* zout_end;

	Huffman z_length;
	Huffman z_distance;

	auto eof () const -> bool
	{
		return zbuffer >= zbuffer_end;
	}
	auto get8 () -> Byte
	{
		return eof() ? 0 : *zbuffer++;
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
			code_buffer |= static_cast<U32>(get8()) << num_bits;
			num_bits += 8;
		} while (num_bits <= 24);

	}
	auto recieve (int bitcount) -> U32
	{
		if (num_bits < bitcount)
		{
			fill_bits();
		}
		const auto k = code_buffer & ((1 << bitcount) - 1);
		code_buffer >>= bitcount;
		num_bits -= bitcount;
		return k;
	}

	auto zhuffman_decode(Huffman *z) -> int;

	// need to make room for n bytes
	auto zexpand (Byte* zout, int n) -> bool;

	auto parse_huffman_block() -> int;

	auto compute_huffman_codes() -> int;

	auto parse_uncompressed_block() -> int;

	static auto decode_malloc_guesssize_headerflag(
		const Byte* buffer,
		int len,
		int initial_size,
		int *outlen,
		int parse_header) -> Byte*;
};

auto Huffman::zbuild_huffman(const Byte *sizelist, int num) -> bool
{

	int i;
	int k = 0;
	int next_code[16];
	int sizes[17] = {};

	memset(fast, 0, sizeof(fast));
	for (i = 0; i < num; ++i)
	{
		++sizes[sizelist[i]];
	}
	sizes[0] = 0;
	for (i = 1; i < 16; ++i)
	{
		if (sizes[i] > (1 << i))
		{
			failure_reason = "bad sizes";
			return false;
			// return stbi__err("bad sizes", "Corrupt PNG");
		}
	}
	int code = 0;
	for (i = 1; i < 16; ++i)
	{
		next_code[i] = code;
		firstcode[i] = static_cast<U16>(code);
		firstsymbol[i] = static_cast<U16>(k);
		code = (code + sizes[i]);
		if (sizes[i] && (code - 1 >= 1 << i))
		{
			failure_reason = "bad codelengths";
			return false;
			// return stbi__err("bad codelengths", "Corrupt PNG");
		}
		maxcode[i] = code << (16 - i); // preshift for inner loop
		code <<= 1;
		k += sizes[i];
	}
	this->maxcode[16] = 0x10000; // sentinel
	for (i = 0; i < num; ++i)
	{
		if (const int s = sizelist[i])
		{
			const int c = next_code[s] - firstcode[s] + firstsymbol[s];
			auto fastv = static_cast<U16>((s << 9) | i);
			this->size[c] = static_cast<Byte>(s);
			this->value[c] = static_cast<U16>(i);
			if (s <= FAST_BITS)
			{
				int j = bit_reverse(next_code[s], s);
				while (j < (1 << FAST_BITS))
				{
					fast[j] = fastv;
					j += (1 << s);
				}
			}
			++next_code[s];
		}
	}
	return true;

}

auto Huffman::get_failure_reason () -> const char *
{
	return this->failure_reason;
}

auto Huffer::zhuffman_decode(Huffman *z) -> int
{
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
	if (const int b = z->fast[code_buffer & FAST_MASK])
	{
		const int s = b >> 9;
		code_buffer >>= s;
		num_bits -= s;
		return b & 511;
	}

	// zhuffman_decode_slowpath(z);
	// not resolved by fast table, so compute it the slow way
	// use jpeg approach, which requires MSbits at top
	const int k = bit_reverse(code_buffer, 16);
	int s;
	for (s = FAST_BITS + 1; ; ++s)
	{
		if (k < z->maxcode[s])
		{
			break;
		}
	}
	if (s >= 16)
	{
		// invalid code!
		return -1;
	}
	// code size is s, so:
	const int b = (k >> (16 - s)) - z->firstcode[s] + z->firstsymbol[s];
	if (b >= NSYMS)
	{
		// some data was corrupt somewhere!
		return -1;
	}
	if (z->size[b] != s)
	{
		// was originally an assert, but report failure instead.
		return -1;
	}
	code_buffer >>= s;
	num_bits -= s;
	return z->value[b];
}

auto Huffer::zexpand(Byte *zout, int n) -> bool
{
	this->zout = zout;
	const auto cur = static_cast<unsigned int>(this->zout - this->zout_start);
	auto limit = static_cast<unsigned>(this->zout_end - this->zout_start);
	if (BoundsU32.max - cur < static_cast<unsigned>(n))
	{
		return stbi__err("outofmem", "Out of memory");
	}
	while (cur + n > limit)
	{
		if (limit > BoundsU32.max / 2)
		{
			return stbi__err("outofmem", "Out of memory");
		}
		limit *= 2;
	}
	auto *q = static_cast<Byte*>(STBI_REALLOC_SIZED(this->zout_start, old_limit, limit));
	if (q == nullptr)
	{
		return stbi__err("outofmem", "Out of memory");
	}
	this->zout_start = q;
	this->zout = q + cur;
	this->zout_end = q + limit;
	return true;
}

auto Huffer::parse_huffman_block() -> int
{
	auto* zoutl = this->zout;
	while (true)
	{
		int z = zhuffman_decode(&z_length);
		if (z < 256)
		{
			if (z < 0)
			{
				// error in huffman codes
				return stbi__err("bad huffman code", "Corrupt PNG");
			}
			if (zoutl >= zout_end)
			{
				if (!zexpand(zoutl, 1)) return 0;
				zoutl = this->zout;
			}
			*zoutl++ = static_cast<Byte>(z);
		}
		else
		{
			if (z == 256)
			{
				this->zout = zoutl;
				if (hit_zeof_once && num_bits < 16)
				{
					// The first time we hit zeof, we inserted 16 extra zero bits into our bit
					// buffer so the decoder can just do its speculative decoding. But if we
					// actually consumed any of those bits (which is the case when num_bits < 16),
					// the stream actually read past the end so it is malformed.
					return stbi__err("unexpected end", "Corrupt PNG");
				}
				return true;
			}
			if (z >= 286)
			{
				return stbi__err("bad huffman code", "Corrupt PNG");
			}
			// per DEFLATE, length codes 286 and 287 must not appear in compressed data
			z -= 257;
			int len = LENGTH_BASE[z];
			if (LENGTH_EXTRA[z]) len += recieve(LENGTH_EXTRA[z]);
			z = zhuffman_decode(&z_distance);
			if (z < 0 || z >= 30)
			{
				return stbi__err("bad huffman code", "Corrupt PNG");
			}
			// per DEFLATE, distance codes 30 and 31 must not appear in compressed data
			int dist = DIST_BASE[z];
			if (DIST_EXTRA[z])
			{
				dist += recieve(DIST_EXTRA[z]);
			}
			if (zoutl - zout_start < dist)
			{
				return stbi__err("bad dist", "Corrupt PNG");
			}
			if (len > zout_end - zoutl)
			{
				if (!zexpand(zoutl, len))
				{
					return false;
				}
				zoutl = this->zout;
			}
			auto* p = zoutl - dist;
			if (dist == 1)
			{
				// run of one byte; common in images.
				auto v = *p;
				if (len)
				{
					do *zoutl++ = v; while (--len);
				}
			}
			else
			{
				if (len)
				{
					do *zoutl++ = *p++; while (--len);
				}
			}
		}
	}
}

auto Huffer::compute_huffman_codes() -> int
{
	Huffman z_codelength;
	//padding for maximum single op
	Byte lencodes[286 + 32 + 137];
	Byte codelength_sizes[19] = {};

	int hlit = recieve(5) + 257;
	int hdist = recieve(5) + 1;
	int hclen = recieve(4) + 4;
	int ntot = hlit + hdist;

	for (int i = 0; i < hclen; ++i)
	{
		codelength_sizes[length_dezigzag[i]] = static_cast<Byte>(recieve(3));
	}
	if (!z_codelength.zbuild_huffman(codelength_sizes, 19))
	{
		return stbi__err(z_codelength.get_failure_reason(), "");
	}

	int n = 0;
	while (n < ntot)
	{
		int c = this->zhuffman_decode(&z_codelength);
		if (c < 0 || c >= 19)
		{
			return stbi__err("bad codelengths", "Corrupt PNG");
		}
		if (c < 16)
		{
			lencodes[n++] = static_cast<Byte>(c);
		}
		else
		{
			Byte fill = 0;
			if (c == 16)
			{
				c = recieve(2) + 3;
				if (n == 0)
				{
					return stbi__err("bad codelengths", "Corrupt PNG");
				}
				fill = lencodes[n - 1];
			}
			else if (c == 17)
			{
				c = recieve(3) + 3;
			}
			else if (c == 18)
			{
				c = recieve(7) + 11;
			}
			else
			{
				return stbi__err("bad codelengths", "Corrupt PNG");
			}
			if (ntot - n < c)
			{
				return stbi__err("bad codelengths", "Corrupt PNG");
			}
			memset(lencodes + n, fill, c);
			n += c;
		}
	}
	if (n != ntot)
	{
		return stbi__err("bad codelengths", "Corrupt PNG");
	}
	if (!z_length.zbuild_huffman(lencodes, hlit))
	{
		return stbi__err(z_length.get_failure_reason(), "");
	}
	if (!z_distance.zbuild_huffman(lencodes + hlit, hdist))
	{
		return stbi__err(z_distance.get_failure_reason(), "");
	}
	return true;
}

auto Huffer::parse_uncompressed_block() -> int
{
	Byte header[4];
	if (num_bits & 7)
	{
		// discard
		recieve(num_bits & 7);
	}
	// drain the bit-packed data into header
	int k = 0;
	while (num_bits > 0)
	{
		// suppress MSVC run-time check
		header[k++] = static_cast<Byte>(code_buffer & 255);
		code_buffer >>= 8;
		num_bits -= 8;
	}
	if (num_bits < 0)
	{
		return stbi__err("zlib corrupt", "Corrupt PNG");
	}
	// now fill header the normal way
	while (k < 4)
	{
		header[k++] = get8();
	}
	const int len = header[1] * 256 + header[0];
	if (const int nlen = header[3] * 256 + header[2]; nlen != (len ^ 0xffff))
	{
		return stbi__err("zlib corrupt", "Corrupt PNG");
	}
	if (this->zbuffer + len > this->zbuffer_end)
	{
		return stbi__err("read past buffer", "Corrupt PNG");
	}
	if (zout + len > zout_end && !zexpand(zout, len))
	{
		return false;
	}
	memcpy(zout, zbuffer, len);
	zbuffer += len;
	zout += len;
	return true;
}

auto Huffer::decode_malloc_guesssize_headerflag(const Byte *buffer, int len, int initial_size, int *outlen,
	int parse_header) -> Byte *
{
	auto p = (Byte*)stbi_malloc(initial_size);
	if (p == nullptr)
	{
		return nullptr;
	}
	Huffer a;
	a.zbuffer = (Byte*) buffer;
	a.zbuffer_end = (Byte*) buffer + len;

	a.zout_start = p;
	a.zout = p;
	a.zout_end = p + initial_size;

	bool parse_header_result = true;
	{
		const int cmf = a.get8();
		/* int cinfo = cmf >> 4; */
		const int flg = a.get8();
		// zlib spec
		if (a.eof())
		{
			parse_header_result = stbi__err("bad zlib header", "Corrupt PNG");
		}
		// zlib spec
		if ((cmf * 256 + flg) % 31 != 0)
		{
			parse_header_result = stbi__err("bad zlib header", "Corrupt PNG");
		}
		// preset dictionary not allowed in png
		if (flg & 32)
		{
			parse_header_result = stbi__err("no preset dict", "Corrupt PNG");
		}
		// DEFLATE required for png
		if (const int cm = cmf & 15; cm != 8)
		{
			parse_header_result = stbi__err("bad compression", "Corrupt PNG");
		}
		// window = 1 << (8 + cinfo)... but who cares, we fully buffer output
		// parse_header_result = true;
	}

	if (parse_header && !parse_header_result)
	{
		goto fail;
	}
	a.num_bits = 0;
	a.code_buffer = 0;
	a.hit_zeof_once = 0;
	int final;
	do
	{
		final = a.recieve(1);
		int type = a.recieve(2);
		if (type == 0)
		{
			if (!a.parse_uncompressed_block())
			{
				goto fail;
			}
		}
		else if (type == 3)
		{
			goto fail;
		}
		else
		{
			if (type == 1)
			{
				// use fixed code lengths
				if (!a.z_length.zbuild_huffman(DEFAULT_LENGTH, NSYMS))
				{
					goto fail;
				}
				if (!a.z_distance.zbuild_huffman(DEFAULT_DISTANCE, 32))
				{
					goto fail;
				}
			}
			else
			{
				if (!a.compute_huffman_codes())
				{
					goto fail;
				}
			}
			if (!a.parse_huffman_block())
			{
				goto fail;
			}
		}
	} while (!final);


	if (outlen != nullptr)
	{
		*outlen = static_cast<int>(a.zout - a.zout_start);
	}
	return a.zout_start;

	fail:
	STBI_FREE(a.zout_start);
	return nullptr;
}

}
