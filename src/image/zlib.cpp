
#include "./zlib.hpp"

#include<cstdint>
#include<cstring>
#include<climits>


// public domain zlib decode    v0.2  Sean Barrett 2006-11-18
//    simple implementation
//      - all input must be provided in an upfront buffer
//      - all output is written to a single output buffer (can malloc/realloc)
//    performance
//      - fast huffman

// fast-way is faster to check than jpeg huffman, but slow way is slower
// accelerate all cases in default tables
static constexpr auto STBI__ZFAST_BITS = 9;
static constexpr auto STBI__ZFAST_MASK = ((1 << STBI__ZFAST_BITS) - 1);
// number of symbols in literal/length alphabet
static constexpr auto STBI__ZNSYMS = 288;


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
static const uint8_t zdefault_length[STBI__ZNSYMS] =
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
static const uint8_t zdefault_distance[32] =
{
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
};

struct ZHuffman
{
	uint16_t fast[1 << STBI__ZFAST_BITS];
	uint16_t firstcode[16];
	int maxcode[17];
	uint16_t firstsymbol[16];
	uint8_t size[STBI__ZNSYMS];
	uint16_t value[STBI__ZNSYMS];
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
	// STBI_ASSERT(bits <= 16);
	// to bit reverse n bits, reverse 16 and shift
	// e.g. 11 bits, bit reverse and shift away 5
	return bitreverse16(v) >> (16 - bits);
}

// zlib-from-memory implementation for PNG reading
//    because PNG allows splitting the zlib stream arbitrarily,
//    and it's annoying structurally to have PNG call ZLIB call PNG,
//    we require PNG read all the IDATs and combine them into a single
//    memory buffer

struct ZBuffer
{
	uint8_t * zbuffer;
	uint8_t *zbuffer_end;
	int num_bits;
	int hit_zeof_once;
	uint32_t code_buffer;

	uint8_t* zout;
	uint8_t* zout_start;
	uint8_t* zout_end;
	bool z_expandable;

	ZHuffman z_length;
	ZHuffman z_distance;

	Zlib::Context* context;

	auto error_occured (const char* message) -> bool
	{
		return context->error_occured(message);
	}
};

static int zeof(ZBuffer *z)
{
	return (z->zbuffer >= z->zbuffer_end);
}

static uint8_t zget8(ZBuffer *z)
{
	return zeof(z) ? 0 : *z->zbuffer++;
}

static void zfill_bits(ZBuffer *z)
{
	do
	{
		if (z->code_buffer >= (1U << z->num_bits))
		{
			z->zbuffer = z->zbuffer_end; /* treat this as EOF so we fail. */
			return;
		}
		z->code_buffer |= static_cast<unsigned int>(zget8(z)) << z->num_bits;
		z->num_bits += 8;
	} while (z->num_bits <= 24);
}

static unsigned int zreceive(ZBuffer *z, int n)
{
	unsigned int k;
	if (z->num_bits < n)
	{
		zfill_bits(z);
	}
	k = z->code_buffer & ((1 << n) - 1);
	z->code_buffer >>= n;
	z->num_bits -= n;
	return k;
}

static int zhuffman_decode_slowpath(ZBuffer *a, ZHuffman *z)
{
	int b, s, k;
	// not resolved by fast table, so compute it the slow way
	// use jpeg approach, which requires MSbits at top
	k = bit_reverse(a->code_buffer, 16);
	for (s = STBI__ZFAST_BITS + 1; ; ++s)
	{
		if (k < z->maxcode[s])
		{
			break;
		}
	}
	if (s >= 16)
	{
		return -1; // invalid code!
	}
	// code size is s, so:
	b = (k >> (16 - s)) - z->firstcode[s] + z->firstsymbol[s];
	if (b >= STBI__ZNSYMS)
	{
		return -1; // some data was corrupt somewhere!
	}
	if (z->size[b] != s)
	{
		return -1; // was originally an assert, but report failure instead.
	}
	a->code_buffer >>= s;
	a->num_bits -= s;
	return z->value[b];
}

static int zhuffman_decode(ZBuffer *a, ZHuffman *z)
{
	int b, s;
	if (a->num_bits < 16)
	{
		if (zeof(a))
		{
			if (!a->hit_zeof_once)
			{
				// This is the first time we hit eof, insert 16 extra padding btis
				// to allow us to keep going; if we actually consume any of them
				// though, that is invalid data. This is caught later.
				a->hit_zeof_once = 1;
				a->num_bits += 16; // add 16 implicit zero bits
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
			zfill_bits(a);
		}
	}
	b = z->fast[a->code_buffer & STBI__ZFAST_MASK];
	if (b)
	{
		s = b >> 9;
		a->code_buffer >>= s;
		a->num_bits -= s;
		return b & 511;
	}
	return zhuffman_decode_slowpath(a, z);
}

static int zexpand(ZBuffer *z, uint8_t*zout, int n) // need to make room for n bytes
{
	unsigned int old_limit;
	z->zout = zout;
	if (!z->z_expandable)
	{
		return z->error_occured("output buffer limit");
	}
	auto cur = (z->zout - z->zout_start);
	auto limit = old_limit = (z->zout_end - z->zout_start);
	if (UINT_MAX - cur < n)
	{
		return z->error_occured("outofmem");
	}
	while (cur + n > limit)
	{
		if (limit > UINT_MAX / 2)
		{
			return z->error_occured("outofmem");
		}
		limit *= 2;
	}
	auto q = z->context->realloc_t(z->zout_start, old_limit, limit);
	// STBI_NOTUSED(old_limit);
	if (q == nullptr)
	{
		return z->error_occured("outofmem");
	}
	z->zout_start = q;
	z->zout = q + cur;
	z->zout_end = q + limit;
	return 1;
}

static const int stbi__zlength_base[31] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13,
	15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
	67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0
};

static const int stbi__zlength_extra[31] =
		{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 0, 0};

static const int stbi__zdist_base[32] = {
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
	257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 0, 0
};

static const int stbi__zdist_extra[32] =
		{0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int zparse_huffman_block(ZBuffer *a)
{
	auto zout = a->zout;
	for (;;)
	{
		int z = zhuffman_decode(a, &a->z_length);
		if (z < 256)
		{
			if (z < 0)
			{
				// error in huffman codes
				return a->error_occured("bad huffman code");
			}
			if (zout >= a->zout_end)
			{
				if (!zexpand(a, zout, 1))
				{
					return 0;
				}
				zout = a->zout;
			}
			*zout++ = (char) z;
		}
		else
		{
			int len, dist;
			if (z == 256)
			{
				a->zout = zout;
				if (a->hit_zeof_once && a->num_bits < 16)
				{
					// The first time we hit zeof, we inserted 16 extra zero bits into our bit
					// buffer so the decoder can just do its speculative decoding. But if we
					// actually consumed any of those bits (which is the case when num_bits < 16),
					// the stream actually read past the end so it is malformed.
					return a->error_occured("unexpected end");
				}
				return 1;
			}
			if (z >= 286)
			{
				return a->error_occured("bad huffman code");
			}
			// per DEFLATE, length codes 286 and 287 must not appear in compressed data
			z -= 257;
			len = stbi__zlength_base[z];
			if (stbi__zlength_extra[z])
			{
				len += zreceive(a, stbi__zlength_extra[z]);
			}
			z = zhuffman_decode(a, &a->z_distance);
			if (z < 0 || z >= 30)
			{
				return a->error_occured("bad huffman code");
			}
			// per DEFLATE, distance codes 30 and 31 must not appear in compressed data
			dist = stbi__zdist_base[z];
			if (stbi__zdist_extra[z])
			{
				dist += zreceive(a, stbi__zdist_extra[z]);
			}
			if (zout - a->zout_start < dist)
			{
				return a->error_occured("bad dist");
			}
			if (len > a->zout_end - zout)
			{
				if (!zexpand(a, zout, len))
				{
					return 0;
				}
				zout = a->zout;
			}
			auto p = zout - dist;
			if (dist == 1)
			{
				// run of one byte; common in images.
				auto v = *p;
				if (len)
				{
					do *zout++ = v; while (--len);
				}
			}
			else
			{
				if (len)
				{
					do *zout++ = *p++; while (--len);
				}
			}
		}
	}
}

static int zbuild_huffman(ZBuffer* ctx, ZHuffman *z, const uint8_t *sizelist, int num)
{
	int i, k = 0;
	int next_code[16];
	int sizes[17] = {};

	std::memset(sizes, 0, sizeof(sizes));
	std::memset(z->fast, 0, sizeof(z->fast));
	for (i = 0; i < num; ++i)
	{
		++sizes[sizelist[i]];
	}
	sizes[0] = 0;
	for (i = 1; i < 16; ++i)
	{
		if (sizes[i] > (1 << i))
		{
			return ctx->error_occured("bad sizes");
		}
	}
	int code = 0;
	for (i = 1; i < 16; ++i)
	{
		next_code[i] = code;
		z->firstcode[i] = static_cast<uint16_t>(code);
		z->firstsymbol[i] = static_cast<uint16_t>(k);
		code = (code + sizes[i]);
		if (sizes[i])
		{
			if (code - 1 >= (1 << i))
			{
				return ctx->error_occured("bad codelengths");
			}
		}
		z->maxcode[i] = code << (16 - i); // preshift for inner loop
		code <<= 1;
		k += sizes[i];
	}
	z->maxcode[16] = 0x10000; // sentinel
	for (i = 0; i < num; ++i)
	{
		int s = sizelist[i];
		if (!s)
		{
			continue;
		}
		int c = next_code[s] - z->firstcode[s] + z->firstsymbol[s];
		auto fastv = static_cast<uint16_t>((s << 9) | i);
		z->size[c] = static_cast<uint8_t>(s);
		z->value[c] = static_cast<uint16_t>(i);
		if (s <= STBI__ZFAST_BITS)
		{
			int j = bit_reverse(next_code[s], s);
			while (j < (1 << STBI__ZFAST_BITS))
			{
				z->fast[j] = fastv;
				j += (1 << s);
			}
		}
		++next_code[s];
	}
	return 1;
}


static int zcompute_huffman_codes(ZBuffer *a)
{
	static const uint8_t length_dezigzag[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
	ZHuffman z_codelength;
	uint8_t lencodes[286 + 32 + 137]; //padding for maximum single op
	int i, n;

	int hlit = zreceive(a, 5) + 257;
	int hdist = zreceive(a, 5) + 1;
	int hclen = zreceive(a, 4) + 4;
	int ntot = hlit + hdist;

	uint8_t codelength_sizes[19] = {};
	for (i = 0; i < hclen; ++i)
	{
		int s = zreceive(a, 3);
		codelength_sizes[length_dezigzag[i]] = (uint8_t) s;
	}
	if (!zbuild_huffman(a, &z_codelength, codelength_sizes, 19))
	{
		return 0;
	}

	n = 0;
	while (n < ntot)
	{
		int c = zhuffman_decode(a, &z_codelength);
		if (c < 0 || c >= 19)
		{
			return a->error_occured("bad codelengths");
		}
		if (c < 16)
		{
			lencodes[n++] = (uint8_t) c;
		}
		else
		{
			uint8_t fill = 0;
			if (c == 16)
			{
				c = zreceive(a, 2) + 3;
				if (n == 0)
				{
					return a->error_occured("bad codelengths");
				}
				fill = lencodes[n - 1];
			}
			else if (c == 17)
			{
				c = zreceive(a, 3) + 3;
			}
			else if (c == 18)
			{
				c = zreceive(a, 7) + 11;
			}
			else
			{
				return a->error_occured("bad codelengths");
			}
			if (ntot - n < c)
			{
				return a->error_occured("bad codelengths");
			}
			std::memset(lencodes + n, fill, c);
			n += c;
		}
	}
	if (n != ntot)
	{
		return a->error_occured("bad codelengths");
	}
	if (!zbuild_huffman(a, &a->z_length, lencodes, hlit))
	{
		return 0;
	}
	if (!zbuild_huffman(a, &a->z_distance, lencodes + hlit, hdist))
	{
		return 0;
	}
	return 1;
}

static int zparse_uncompressed_block(ZBuffer *a)
{
	uint8_t header[4];
	int len, nlen, k;
	if (a->num_bits & 7)
	{
		zreceive(a, a->num_bits & 7); // discard
	}
	// drain the bit-packed data into header
	k = 0;
	while (a->num_bits > 0)
	{
		header[k++] = (uint8_t) (a->code_buffer & 255); // suppress MSVC run-time check
		a->code_buffer >>= 8;
		a->num_bits -= 8;
	}
	if (a->num_bits < 0)
	{
		return a->error_occured("zlib corrupt");
	}
	// now fill header the normal way
	while (k < 4)
	{
		header[k++] = zget8(a);
	}
	len = header[1] * 256 + header[0];
	nlen = header[3] * 256 + header[2];
	if (nlen != (len ^ 0xffff))
	{
		return a->error_occured("zlib corrupt");
	}
	if (a->zbuffer + len > a->zbuffer_end)
	{
		return a->error_occured("read past buffer");
	}
	if (a->zout + len > a->zout_end)
	{
		if (!zexpand(a, a->zout, len))
		{
			return 0;
		}
	}
	std::memcpy(a->zout, a->zbuffer, len);
	a->zbuffer += len;
	a->zout += len;
	return 1;
}


static int zdo_zlib(ZBuffer *a, uint8_t* obuf, size_t olen, bool exp, bool parse_header)
{
	a->zout_start = obuf;
	a->zout = obuf;
	a->zout_end = obuf + olen;
	a->z_expandable = exp;

	bool result = false;
	{
		if (parse_header)
		{
			bool hdrresult;
			int cmf = zget8(a);
			int cm = cmf & 15;
			/* int cinfo = cmf >> 4; */
			int flg = zget8(a);
			if (zeof(a))
			{
				hdrresult = a->error_occured("bad zlib header"); // zlib spec
				goto hdrendl;
			}
			if ((cmf * 256 + flg) % 31 != 0)
			{
				hdrresult = a->error_occured("bad zlib header"); // zlib spec
				goto hdrendl;
			}
			if (flg & 32)
			{
				hdrresult = a->error_occured("no preset dict"); // preset dictionary not allowed in png
				goto hdrendl;
			}
			if (cm != 8)
			{
				hdrresult = a->error_occured("bad compression"); // DEFLATE required for png
				goto hdrendl;
			}
			// window = 1 << (8 + cinfo)... but who cares, we fully buffer output
			hdrresult = true;

			hdrendl:
			if (!hdrresult)
			{
				goto endl;
			}
		}
		a->num_bits = 0;
		a->code_buffer = 0;
		a->hit_zeof_once = 0;
		int final;
		do
		{
			final = zreceive(a, 1);
			int type = zreceive(a, 2);
			if (type == 0)
			{
				if (!zparse_uncompressed_block(a))
				{
					goto endl;
				}
			}
			else if (type == 3)
			{
				goto endl;
			}
			else
			{
				if (type == 1)
				{
					// use fixed code lengths
					if (!zbuild_huffman(a, &a->z_length, zdefault_length, STBI__ZNSYMS))
					{
						goto endl;
					}
					if (!zbuild_huffman(a, &a->z_distance, zdefault_distance, 32))
					{
						goto endl;
					}
				}
				else
				{
					if (!zcompute_huffman_codes(a))
					{
						goto endl;
					}
				}
				if (!zparse_huffman_block(a))
				{
					goto endl;
				}
			}
		} while (!final);

		result = true;
		endl:
	}

	return result;
}


auto Zlib::Context::decode_malloc_guesssize_headerflag () -> uint8_t *
{
	const auto p = this->malloc_t<uint8_t>(this->initial_size);
	if (p == nullptr)
	{
		throw Zlib::Er("Out of memory");
	}
	ZBuffer a;
	a.zbuffer = this->buffer;
	a.zbuffer_end = this->buffer + this->len;
	if (zdo_zlib(&a, p, this->initial_size, true, this->parse_header))
	{
		this->out_len = a.zout - a.zout_start;
		return a.zout_start;
	}
	this->free_t(a.zout_start);
	return nullptr;
}

