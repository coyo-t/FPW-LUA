
#include "./zlib.hpp"

#include<cstdint>
#include<cstring>
#include<climits>

using stbi__uint16 = std::uint16_t;
using stbi__uint32 = std::uint32_t;
using stbi_uc = std::uint8_t;

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

// zlib-style huffman encoding
// (jpegs packs from left, zlib from right, so can't share code)
struct ZHuffman
{
	stbi__uint16 fast[1 << STBI__ZFAST_BITS];
	stbi__uint16 firstcode[16];
	int maxcode[17];
	stbi__uint16 firstsymbol[16];
	stbi_uc size[STBI__ZNSYMS];
	stbi__uint16 value[STBI__ZNSYMS];
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

static int zbuild_huffman(ZHuffman *z, const stbi_uc *sizelist, int num)
{
	int i, k = 0;
	int next_code[16], sizes[17];

	// DEFLATE spec for generating codes
	memset(sizes, 0, sizeof(sizes));
	memset(z->fast, 0, sizeof(z->fast));
	for (i = 0; i < num; ++i)
	{
		++sizes[sizelist[i]];
	}
	sizes[0] = 0;
	for (i = 1; i < 16; ++i)
	{
		if (sizes[i] > (1 << i))
		{
			return stbi__err("bad sizes", "Corrupt PNG");
		}
	}
	int code = 0;
	for (i = 1; i < 16; ++i)
	{
		next_code[i] = code;
		z->firstcode[i] = (stbi__uint16) code;
		z->firstsymbol[i] = (stbi__uint16) k;
		code = (code + sizes[i]);
		if (sizes[i])
		{
			if (code - 1 >= (1 << i))
			{
				return stbi__err("bad codelengths", "Corrupt PNG");
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
		stbi__uint16 fastv = (stbi__uint16) ((s << 9) | i);
		z->size[c] = (stbi_uc) s;
		z->value[c] = (stbi__uint16) i;
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

// zlib-from-memory implementation for PNG reading
//    because PNG allows splitting the zlib stream arbitrarily,
//    and it's annoying structurally to have PNG call ZLIB call PNG,
//    we require PNG read all the IDATs and combine them into a single
//    memory buffer

struct ZBuffer
{
	stbi_uc *zbuffer, *zbuffer_end;
	int num_bits;
	int hit_zeof_once;
	stbi__uint32 code_buffer;

	char *zout;
	char *zout_start;
	char *zout_end;
	int z_expandable;

	ZHuffman z_length, z_distance;
};

static int zeof(ZBuffer *z)
{
	return (z->zbuffer >= z->zbuffer_end);
}

static stbi_uc zget8(ZBuffer *z)
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
		z->code_buffer |= (unsigned int) zget8(z) << z->num_bits;
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

static int zexpand(ZBuffer *z, char *zout, int n) // need to make room for n bytes
{
	char *q;
	unsigned int cur, limit, old_limit;
	z->zout = zout;
	if (!z->z_expandable)
	{
		return stbi__err("output buffer limit", "Corrupt PNG");
	}
	cur = (unsigned int) (z->zout - z->zout_start);
	limit = old_limit = (unsigned) (z->zout_end - z->zout_start);
	if (UINT_MAX - cur < (unsigned) n)
	{
		return stbi__err("outofmem", "Out of memory");
	}
	while (cur + n > limit)
	{
		if (limit > UINT_MAX / 2)
		{
			return stbi__err("outofmem", "Out of memory");
		}
		limit *= 2;
	}
	q = (char *) STBI_REALLOC_SIZED(z->zout_start, old_limit, limit);
	STBI_NOTUSED(old_limit);
	if (q == nullptr)
	{
		return stbi__err("outofmem", "Out of memory");
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
	char *zout = a->zout;
	for (;;)
	{
		int z = zhuffman_decode(a, &a->z_length);
		if (z < 256)
		{
			if (z < 0)
			{
				return stbi__err("bad huffman code", "Corrupt PNG"); // error in huffman codes
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
					return stbi__err("unexpected end", "Corrupt PNG");
				}
				return 1;
			}
			if (z >= 286)
			{
				return stbi__err("bad huffman code", "Corrupt PNG");
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
				return stbi__err("bad huffman code", "Corrupt PNG");
			}
			// per DEFLATE, distance codes 30 and 31 must not appear in compressed data
			dist = stbi__zdist_base[z];
			if (stbi__zdist_extra[z])
			{
				dist += zreceive(a, stbi__zdist_extra[z]);
			}
			if (zout - a->zout_start < dist)
			{
				return stbi__err("bad dist", "Corrupt PNG");
			}
			if (len > a->zout_end - zout)
			{
				if (!zexpand(a, zout, len))
				{
					return 0;
				}
				zout = a->zout;
			}
			stbi_uc *p = (stbi_uc *) (zout - dist);
			if (dist == 1)
			{
				// run of one byte; common in images.
				stbi_uc v = *p;
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

static int zcompute_huffman_codes(ZBuffer *a)
{
	static const stbi_uc length_dezigzag[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
	ZHuffman z_codelength;
	stbi_uc lencodes[286 + 32 + 137]; //padding for maximum single op
	stbi_uc codelength_sizes[19];
	int i, n;

	int hlit = zreceive(a, 5) + 257;
	int hdist = zreceive(a, 5) + 1;
	int hclen = zreceive(a, 4) + 4;
	int ntot = hlit + hdist;

	memset(codelength_sizes, 0, sizeof(codelength_sizes));
	for (i = 0; i < hclen; ++i)
	{
		int s = zreceive(a, 3);
		codelength_sizes[length_dezigzag[i]] = (stbi_uc) s;
	}
	if (!zbuild_huffman(&z_codelength, codelength_sizes, 19))
	{
		return 0;
	}

	n = 0;
	while (n < ntot)
	{
		int c = zhuffman_decode(a, &z_codelength);
		if (c < 0 || c >= 19)
		{
			return stbi__err("bad codelengths", "Corrupt PNG");
		}
		if (c < 16)
		{
			lencodes[n++] = (stbi_uc) c;
		}
		else
		{
			stbi_uc fill = 0;
			if (c == 16)
			{
				c = zreceive(a, 2) + 3;
				if (n == 0)
				{
					return stbi__err("bad codelengths", "Corrupt PNG");
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
	if (!zbuild_huffman(&a->z_length, lencodes, hlit))
	{
		return 0;
	}
	if (!zbuild_huffman(&a->z_distance, lencodes + hlit, hdist))
	{
		return 0;
	}
	return 1;
}

static int zparse_uncompressed_block(ZBuffer *a)
{
	stbi_uc header[4];
	int len, nlen, k;
	if (a->num_bits & 7)
	{
		zreceive(a, a->num_bits & 7); // discard
	}
	// drain the bit-packed data into header
	k = 0;
	while (a->num_bits > 0)
	{
		header[k++] = (stbi_uc) (a->code_buffer & 255); // suppress MSVC run-time check
		a->code_buffer >>= 8;
		a->num_bits -= 8;
	}
	if (a->num_bits < 0)
	{
		return stbi__err("zlib corrupt", "Corrupt PNG");
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
		return stbi__err("zlib corrupt", "Corrupt PNG");
	}
	if (a->zbuffer + len > a->zbuffer_end)
	{
		return stbi__err("read past buffer", "Corrupt PNG");
	}
	if (a->zout + len > a->zout_end)
	{
		if (!zexpand(a, a->zout, len))
		{
			return 0;
		}
	}
	memcpy(a->zout, a->zbuffer, len);
	a->zbuffer += len;
	a->zout += len;
	return 1;
}

static int zparse_zlib_header(ZBuffer *a)
{
	int cmf = zget8(a);
	int cm = cmf & 15;
	/* int cinfo = cmf >> 4; */
	int flg = zget8(a);
	if (zeof(a))
	{
		return stbi__err("bad zlib header", "Corrupt PNG"); // zlib spec
	}
	if ((cmf * 256 + flg) % 31 != 0)
	{
		return stbi__err("bad zlib header", "Corrupt PNG"); // zlib spec
	}
	if (flg & 32)
	{
		return stbi__err("no preset dict", "Corrupt PNG"); // preset dictionary not allowed in png
	}
	if (cm != 8)
	{
		return stbi__err("bad compression", "Corrupt PNG"); // DEFLATE required for png
	}
	// window = 1 << (8 + cinfo)... but who cares, we fully buffer output
	return 1;
}

static const stbi_uc zdefault_length[STBI__ZNSYMS] =
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
static const stbi_uc zdefault_distance[32] =
{
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
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

static int zparse_zlib(ZBuffer *a, int parse_header)
{
	int final, type;
	if (parse_header)
	{
		if (!zparse_zlib_header(a))
		{
			return 0;
		}
	}
	a->num_bits = 0;
	a->code_buffer = 0;
	a->hit_zeof_once = 0;
	do
	{
		final = zreceive(a, 1);
		type = zreceive(a, 2);
		if (type == 0)
		{
			if (!zparse_uncompressed_block(a))
			{
				return 0;
			}
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
				if (!zbuild_huffman(&a->z_length, zdefault_length, STBI__ZNSYMS))
				{
					return 0;
				}
				if (!zbuild_huffman(&a->z_distance, zdefault_distance, 32))
				{
					return 0;
				}
			}
			else
			{
				if (!zcompute_huffman_codes(a))
				{
					return 0;
				}
			}
			if (!zparse_huffman_block(a))
			{
				return 0;
			}
		}
	} while (!final);
	return 1;
}

static int zdo_zlib(ZBuffer *a, char *obuf, int olen, int exp, int parse_header)
{
	a->zout_start = obuf;
	a->zout = obuf;
	a->zout_end = obuf + olen;
	a->z_expandable = exp;

	return zparse_zlib(a, parse_header);
}


char *stbi_zlib_decode_malloc_guesssize_headerflag(
	const char *buffer,
	int len,
	int initial_size,
	int *outlen,
	int parse_header)
{
	ZBuffer a;
	auto p = (char *) stbi__malloc(initial_size);
	if (p == nullptr)
	{
		return nullptr;
	}
	a.zbuffer = (stbi_uc *) buffer;
	a.zbuffer_end = (stbi_uc *) buffer + len;
	if (zdo_zlib(&a, p, initial_size, 1, parse_header))
	{
		if (outlen != nullptr)
		{
			*outlen = (int) (a.zout - a.zout_start);
		}
		return a.zout_start;
	}
	STBI_FREE(a.zout_start);
	return nullptr;
}

