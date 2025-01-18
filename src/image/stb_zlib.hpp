#ifndef STB_ZLIB_HPP
#define STB_ZLIB_HPP

#include <cstdint>

std::uint8_t* stbi_zlib_decode_malloc_guesssize_headerflag(
	const std::uint8_t *buffer,
	int len,
	int initial_size,
	int *outlen,
	int parse_header);

#endif //STB_ZLIB_HPP
