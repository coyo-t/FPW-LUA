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
}


#endif //ZLIB_HPP
