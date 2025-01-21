
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
static constexpr auto ZFAST_BITS = 9;
static constexpr auto ZFAST_MASK = ((1 << ZFAST_BITS) - 1);
// number of symbols in literal/length alphabet
static constexpr auto ZNSYMS = 288;


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
static const uint8_t DEFAULT_LENGTH[ZNSYMS] = {
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

static const uint8_t DEFAULT_DISTANCE[32] = {
	5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5,
};

static const uint8_t LENGTH_DE_ZIGZAG[19] = {
	16, 17, 18, 0,
	8,  7,  9,  6,
	10, 5,  11, 4,
	12, 3,  13, 2,
	14, 1,  15
};

static const int ZLENGTH_BASE[31] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13,
	15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
	67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0
};

static const int ZLENGTH_EXTRA[31] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 2, 2, 2, 2,
	3, 3, 3, 3, 4, 4, 4, 4,
	5, 5, 5, 5, 0, 0, 0,
};

static const int ZDIST_BASE[32] = {
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
	257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 0, 0
};

static const int ZDIST_EXTRA[32] = {
	0,  0,  0,  0,  1,  1, 2,
	2,  3,  3,  4,  4,  5, 5,
	6,  6,  7,  7,  8,  8, 9,
	9, 10, 10, 11, 11, 12, 12, 13, 13
};

static int bitreverse16(uint32_t n)
{
	n = ((n & 0xAAAA) >> 1) | ((n & 0x5555) << 1);
	n = ((n & 0xCCCC) >> 2) | ((n & 0x3333) << 2);
	n = ((n & 0xF0F0) >> 4) | ((n & 0x0F0F) << 4);
	n = ((n & 0xFF00) >> 8) | ((n & 0x00FF) << 8);
	return n;
}

static int bit_reverse(uint32_t v, size_t bits)
{
	// STBI_ASSERT(bits <= 16);
	// to bit reverse n bits, reverse 16 and shift
	// e.g. 11 bits, bit reverse and shift away 5
	return bitreverse16(v) >> (16 - bits);
}

struct ZHuffman
{
	uint16_t fast[1 << ZFAST_BITS];
	uint16_t firstcode[16];
	int maxcode[17];
	uint16_t firstsymbol[16];
	uint8_t size[ZNSYMS];
	uint16_t value[ZNSYMS];

	auto zbuild_huffman(const uint8_t *sizelist, int num) -> void
	{
		int sizes[17] = {};
		std::memset(this->fast, 0, sizeof(this->fast));
		for (int i = 0; i < num; ++i)
		{
			++sizes[sizelist[i]];
		}
		sizes[0] = 0;
		for (int i = 1; i < 16; ++i)
		{
			if (sizes[i] > (1 << i))
			{
				throw Zlib::Err("bad sizes");
			}
		}
		int next_code[16];
		int code = 0;
		int k = 0;
		for (int i = 1; i < 16; ++i)
		{
			next_code[i] = code;
			this->firstcode[i] = static_cast<uint16_t>(code);
			this->firstsymbol[i] = static_cast<uint16_t>(k);
			code = (code + sizes[i]);
			if (sizes[i])
			{
				if (code - 1 >= (1 << i))
				{
					throw Zlib::Err("bad codelengths");
				}
			}
			this->maxcode[i] = code << (16 - i); // preshift for inner loop
			code <<= 1;
			k += sizes[i];
		}
		this->maxcode[16] = 0x10000; // sentinel
		for (int i = 0; i < num; ++i)
		{
			const int s = sizelist[i];
			if (!s)
			{
				continue;
			}
			const int c = next_code[s] - this->firstcode[s] + this->firstsymbol[s];
			auto fastv = static_cast<uint16_t>((s << 9) | i);
			this->size[c] = static_cast<uint8_t>(s);
			this->value[c] = static_cast<uint16_t>(i);
			if (s <= ZFAST_BITS)
			{
				int j = bit_reverse(next_code[s], s);
				while (j < (1 << ZFAST_BITS))
				{
					this->fast[j] = fastv;
					j += (1 << s);
				}
			}
			++next_code[s];
		}
	}
};

// zlib-from-memory implementation for PNG reading
//    because PNG allows splitting the zlib stream arbitrarily,
//    and it's annoying structurally to have PNG call ZLIB call PNG,
//    we require PNG read all the IDATs and combine them into a single
//    memory buffer

struct ZBuffer
{
	uint8_t *zbuffer;
	uint8_t *zbuffer_end;
	size_t num_bits;
	int hit_zeof_once;
	uint32_t code_buffer;

	uint8_t* zout;
	uint8_t* zout_start;
	uint8_t* zout_end;
	bool z_expandable;

	ZHuffman z_length;
	ZHuffman z_distance;

	Zlib::Context* context;

	auto eof () const -> bool
	{
		return zbuffer >= zbuffer_end;
	}
	auto read_u8() -> uint8_t
	{
		return eof() ? 0 : *zbuffer++;
	}
	auto zfill_bits () -> void
	{
		do
		{
			if (code_buffer >= (1U << num_bits))
			{
				zbuffer = zbuffer_end; // treat this as EOF so we fail.
				return;
			}
			code_buffer |= static_cast<unsigned int>(read_u8()) << num_bits;
			num_bits += 8;
		} while (num_bits <= 24);
	}
	template<size_t N, size_t OFS = 0>
	auto read_bits_t () -> uint32_t
	{
		static_assert(0 < N && N <= 32);
		if (num_bits < N)
		{
			zfill_bits();
		}
		static constexpr auto NSH = 1 << (N-1);
		static constexpr auto MASK = (NSH - 1) | NSH;
		const auto k = code_buffer & MASK;
		code_buffer >>= N;
		num_bits -= N;
		return k + OFS;
	}
	auto read_bits (size_t n) -> uint32_t
	{
		if (num_bits < n)
		{
			zfill_bits();
		}
		auto k = code_buffer & ((1 << n) - 1);
		code_buffer >>= n;
		num_bits -= n;
		return k;
	}
	auto zexpand (uint8_t* zout, int n) -> void
	{
		// need to make room for n bytes
		this->zout = zout;
		if (!this->z_expandable)
		{
			throw Zlib::Err("output buffer limit");
		}
		auto cur = (this->zout - this->zout_start);
		unsigned int old_limit;
		auto limit = old_limit = (this->zout_end - this->zout_start);
		if (UINT_MAX - cur < n)
		{
			throw Zlib::Err("outofmem");
		}
		while (cur + n > limit)
		{
			if (limit > UINT_MAX / 2)
			{
				throw Zlib::Err("outofmem");
			}
			limit *= 2;
		}
		auto q = this->context->realloc_t(this->zout_start, old_limit, limit);
		// STBI_NOTUSED(old_limit);
		if (q == nullptr)
		{
			throw Zlib::Err("outofmem");
		}
		this->zout = q + cur;
		this->zout_start = q;
		this->zout_end = q + limit;
	}
	auto zhuffman_decode(const ZHuffman& z) -> int
	{
		if (num_bits < 16)
		{
			if (this->eof())
			{
				if (!this->hit_zeof_once)
				{
					// This is the first time we hit eof, insert 16 extra padding btis
					// to allow us to keep going; if we actually consume any of them
					// though, that is invalid data. This is caught later.
					this->hit_zeof_once = 1;
					this->num_bits += 16; // add 16 implicit zero bits
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
				this->zfill_bits();
			}
		}
		if (const int b = z.fast[this->code_buffer & ZFAST_MASK])
		{
			const int s = b >> 9;
			this->code_buffer >>= s;
			this->num_bits -= s;
			return b & 511;
		}

		int s2;
		// not resolved by fast table, so compute it the slow way
		// use jpeg approach, which requires MSbits at top
		const auto k2 = bit_reverse(code_buffer, 16);
		for (s2 = ZFAST_BITS + 1; ; ++s2)
		{
			if (k2 < z.maxcode[s2])
			{
				break;
			}
		}
		if (s2 >= 16)
		{
			return -1; // invalid code!
		}
		// code size is s, so:
		const int b2 = (k2 >> (16 - s2)) - z.firstcode[s2] + z.firstsymbol[s2];
		if (b2 >= ZNSYMS)
		{
			return -1; // some data was corrupt somewhere!
		}
		if (z.size[b2] != s2)
		{
			return -1; // was originally an assert, but report failure instead.
		}
		this->code_buffer >>= s2;
		this->num_bits -= s2;
		return z.value[b2];
	}
};


auto Zlib::Context::decode_malloc_guesssize_headerflag () -> uint8_t *
{
	const auto p = this->malloc_t<uint8_t>(this->initial_size);
	if (p == nullptr)
	{
		throw Zlib::Err("Out of memory");
	}
	ZBuffer a;
	a.zbuffer = this->buffer;
	a.zbuffer_end = this->buffer + this->len;
	try
	{
		// zdo_zlib(&a, p, this->initial_size, true, this->parse_header);
		a.zout_start = p;
		a.zout = p;
		a.zout_end = p + this->initial_size;
		a.z_expandable = this->parse_header;


		if (parse_header)
		{
			int cmf = a.read_u8();
			int cm = cmf & 15;
			/* int cinfo = cmf >> 4; */
			int flg = a.read_u8();
			if (a.eof())
			{
				// zlib spec
				throw Zlib::Err("bad zlib header");
			}
			if ((cmf * 256 + flg) % 31 != 0)
			{
				// zlib spec
				throw Zlib::Err("bad zlib header");
			}
			if (flg & 32)
			{
				// preset dictionary not allowed in png
				throw Zlib::Err("no preset dict");
			}
			if (cm != 8)
			{
				// DEFLATE required for png
				throw Zlib::Err("bad compression");
			}
			// window = 1 << (8 + cinfo)... but who cares, we fully buffer output
		}
		a.num_bits = 0;
		a.code_buffer = 0;
		a.hit_zeof_once = 0;
		uint8_t final;
		do
		{
			final = a.read_bits_t<1>();
			if (const auto type = a.read_bits_t<2>(); type == 0)
			{
				uint8_t header[4];
				if (a.num_bits & 7)
				{
					a.read_bits(a.num_bits & 7); // discard
				}
				// drain the bit-packed data into header

				auto pev_bits = a.num_bits;
				int k = 0;
				while (a.num_bits > 0)
				{
					// suppress MSVC run-time check
					header[k++] = static_cast<uint8_t>(a.code_buffer & 255);
					a.code_buffer >>= 8;
					a.num_bits -= 8;
					if (a.num_bits > pev_bits)
					{
						throw Zlib::Err("zlib corrupt");
					}
				}
				// now fill header the normal way
				while (k < 4)
				{
					header[k++] = a.read_u8();
				}
				int len = header[1] * 256 + header[0];
				int nlen = header[3] * 256 + header[2];
				if (nlen != (len ^ 0xffff))
				{
					throw Zlib::Err("zlib corrupt");
				}
				if (a.zbuffer + len > a.zbuffer_end)
				{
					throw Zlib::Err("read past buffer");
				}
				if (a.zout + len > a.zout_end)
				{
					a.zexpand(a.zout, len);
				}
				std::memcpy(a.zout, a.zbuffer, len);
				a.zbuffer += len;
				a.zout += len;
			}
			else if (type == 3)
			{
				throw Zlib::Err("zdo_zlib: type == 3");
			}
			else
			{
				if (type == 1)
				{
					// use fixed code lengths
					a.z_length.zbuild_huffman(DEFAULT_LENGTH, ZNSYMS);
					a.z_distance.zbuild_huffman(DEFAULT_DISTANCE, 32);
				}
				else
				{
					// zcompute_huffman_codes
					ZHuffman z_codelength;
					uint8_t lencodes[286 + 32 + 137]; //padding for maximum single op

					const int hlit  = a.read_bits_t<5, 257>();
					const int hdist = a.read_bits_t<5, 1>();
					const int hclen = a.read_bits_t<4, 4>();
					const int ntot = hlit + hdist;

					uint8_t codelength_sizes[19] = {};
					for (int i = 0; i < hclen; ++i)
					{
						int s = a.read_bits(3);
						codelength_sizes[LENGTH_DE_ZIGZAG[i]] = (uint8_t) s;
					}
					z_codelength.zbuild_huffman(codelength_sizes, 19);

					int n = 0;
					while (n < ntot)
					{
						int c = a.zhuffman_decode(z_codelength);
						if (c < 0 || c >= 19)
						{
							throw Zlib::Err("bad codelengths");
						}
						if (c < 16)
						{
							lencodes[n++] = static_cast<uint8_t>(c);
						}
						else
						{
							uint8_t fill = 0;
							if (c == 16)
							{
								c = a.read_bits_t<2, 3>();
								if (n == 0)
								{
									throw Zlib::Err("bad codelengths");
								}
								fill = lencodes[n - 1];
							}
							else if (c == 17)
							{
								c = a.read_bits_t<3, 3>();
							}
							else if (c == 18)
							{
								c = a.read_bits_t<7, 11>();
							}
							else
							{
								throw Zlib::Err("bad codelengths");
							}
							if (ntot - n < c)
							{
								throw Zlib::Err("bad codelengths");
							}
							std::memset(lencodes + n, fill, c);
							n += c;
						}
					}
					if (n != ntot)
					{
						throw Zlib::Err("bad codelengths");
					}
					a.z_length.zbuild_huffman(lencodes, hlit);
					a.z_distance.zbuild_huffman(lencodes + hlit, hdist);
				}

				// zparse_huffman_block
				auto zout = a.zout;
				for (;;)
				{
					if (int z = a.zhuffman_decode(a.z_length); z < 256)
					{
						if (z < 0)
						{
							// error in huffman codes
							throw Zlib::Err("bad huffman code");
						}
						if (zout >= a.zout_end)
						{
							a.zexpand(zout, 1);
							zout = a.zout;
						}
						*zout++ = static_cast<char>(z);
					}
					else
					{
						if (z == 256)
						{
							a.zout = zout;
							if (a.hit_zeof_once && a.num_bits < 16)
							{
								// The first time we hit zeof, we inserted 16 extra zero bits into our bit
								// buffer so the decoder can just do its speculative decoding. But if we
								// actually consumed any of those bits (which is the case when num_bits < 16),
								// the stream actually read past the end so it is malformed.
								throw Zlib::Err("unexpected end");
							}
						}
						else
						{
							if (z >= 286)
							{
								throw Zlib::Err("bad huffman code");
							}
							// per DEFLATE, length codes 286 and 287 must not appear in compressed data
							z -= 257;
							int len = ZLENGTH_BASE[z];
							if (ZLENGTH_EXTRA[z])
							{
								len += a.read_bits(ZLENGTH_EXTRA[z]);
							}
							z = a.zhuffman_decode(a.z_distance);
							if (z < 0 || z >= 30)
							{
								throw Zlib::Err("bad huffman code");
							}
							// per DEFLATE, distance codes 30 and 31 must not appear in compressed data
							int dist = ZDIST_BASE[z];
							if (ZDIST_EXTRA[z])
							{
								dist += a.read_bits(ZDIST_EXTRA[z]);
							}
							if (zout - a.zout_start < dist)
							{
								throw Zlib::Err("bad dist");
							}
							if (len > a.zout_end - zout)
							{
								a.zexpand(zout, len);
								zout = a.zout;
							}
							auto p2 = zout - dist;
							if (dist == 1)
							{
								// run of one byte; common in images.
								auto v = *p2;
								if (len)
								{
									do *zout++ = v; while (--len);
								}
							}
							else
							{
								if (len)
								{
									do *zout++ = *p2++; while (--len);
								}
							}
						}
					}
				}
			}
		} while (!final);

//
		this->out_len = a.zout - a.zout_start;
		return a.zout_start;
	}
	catch (Zlib::Err& e)
	{
		this->free_t(a.zout_start);
		throw e;
	}
}

