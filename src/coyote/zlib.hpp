#ifndef ZLIB_HPP
#define ZLIB_HPP
#include "numberz.hpp"

namespace Coyote {

	using Coyote::Byte;
	using Coyote::U16;

	// zlib-style huffman encoding
	// (jpegs packs from left, zlib from right, so can't share code)
	struct Huffman;

	// zlib-from-memory implementation for PNG reading
	//    because PNG allows splitting the zlib stream arbitrarily,
	//    and it's annoying structurally to have PNG call ZLIB call PNG,
	//    we require PNG read all the IDATs and combine them into a single
	//    memory buffer
	struct Huffer;
}


#endif //ZLIB_HPP
