

#ifndef ZLIB_HPP
#define ZLIB_HPP

#include<cstdint>

struct ZlibContext
{
	using MallocCallback = auto (std::size_t size) -> void*;
	using FreeCallback = auto (void* addr) -> void;
	using ReallocCallback = auto (
		void* addr,
		std::size_t old_size,
		std::size_t new_size
	) -> void*;

	MallocCallback*
	malloc = nullptr;

	FreeCallback*
	free = nullptr;

	ReallocCallback*
	realloc = nullptr;

	std::uint8_t* buffer = nullptr;
	std::size_t len = 0;
	std::size_t initial_size = 0;
	std::size_t out_len = 0;
	std::uint8_t parse_header = false;

	template<typename T>
	auto realloc_t (T* p, std::size_t olds, std::size_t news) -> T*
	{
		if (realloc != nullptr)
		{
			return static_cast<T*>(realloc(p, olds, news));
		}
		return nullptr;
	}

	template<typename T>
	auto malloc_t (std::size_t count) -> T*
	{
		if (malloc != nullptr)
		{
			return static_cast<T*>(malloc(sizeof(T) * count));
		}
		return nullptr;
	}
};


extern auto
stbi_zlib_decode_malloc_guesssize_headerflag(ZlibContext *context) -> std::uint8_t*;

// extern char *stbi_zlib_decode_malloc_guesssize_headerflag(
// 	const char *buffer,
// 	int len,
// 	int initial_size,
// 	int *outlen,
// 	int parse_header
// );


#endif //ZLIB_HPP
