

#ifndef ZLIB_HPP
#define ZLIB_HPP

#include<cstdint>

struct ZlibContext
{
	using MallocCallback = auto (std::size_t size) -> void*;
	using FreeCallback = auto (void* addr) -> void;
	using ReallocCallback = auto (void* addr, std::size_t new_size) -> void*;

	MallocCallback*
	malloc = nullptr;

	FreeCallback*
	free = nullptr;

	ReallocCallback*
	realloc = nullptr;

	std::size_t out_len = 0;
};


extern char *stbi_zlib_decode_malloc_guesssize_headerflag(
	const char *buffer,
	int len,
	int initial_size,
	int *outlen,
	int parse_header
);




#endif //ZLIB_HPP
