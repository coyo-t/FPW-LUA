#ifndef ZLIB_HPP
#define ZLIB_HPP
#include "numberz.hpp"

namespace Coyote {

	using Coyote::Byte;
	using Coyote::U16;

	// fast-way is faster to check than jpeg huffman, but slow way is slower
	// accelerate all cases in default tables
	static constexpr auto FAST_BITS =  9;
	static constexpr auto FAST_MASK =  ((1 << FAST_BITS) - 1);

	// number of symbols in literal/length alphabet
	static constexpr auto NSYMS = 288;


	// zlib-style huffman encoding
	// (jpegs packs from left, zlib from right, so can't share code)
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








	// zlib-from-memory implementation for PNG reading
	//    because PNG allows splitting the zlib stream arbitrarily,
	//    and it's annoying structurally to have PNG call ZLIB call PNG,
	//    we require PNG read all the IDATs and combine them into a single
	//    memory buffer

	struct Huffer {
		Byte* zbuffer;
		Byte* zbuffer_end;
		int num_bits;
		int hit_zeof_once;
		U32 code_buffer;

		Byte* zout;
		Byte* zout_start;
		Byte* zout_end;

		Huffman z_length, z_distance;

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
}


#endif //ZLIB_HPP
