

#ifndef ZLIB_HPP
#define ZLIB_HPP

#include<cstdint>
#include<stdexcept>

namespace Zlib {
	using std::uint8_t;
	using std::size_t;

	struct Er final : std::exception
	{
		const char* reason;

		explicit Er(const char * str);
	};

	struct Context
	{
		using MallocCallback = auto (size_t size) -> void*;
		using FreeCallback = auto (void* addr) -> void;
		using ReallocCallback = auto (
			void* addr,
			size_t old_size,
			size_t new_size
		) -> void*;

		MallocCallback*
		malloc = nullptr;

		FreeCallback*
		free = nullptr;

		ReallocCallback*
		realloc = nullptr;

		uint8_t* buffer = nullptr;
		uint8_t parse_header = false;
		size_t len = 0;
		size_t initial_size = 0;
		size_t out_len = 0;

		const char* error = nullptr;

		auto error_occured (const char* msg) -> bool
		{
			error = msg;
			return false;
		}

		template<typename T>
		auto free_t (T* p)
		{
			if (free != nullptr)
			{
				if (p != nullptr)
				{
					free(p);
				}
			}
		}

		template<typename T>
		auto realloc_t (T* p, size_t olds, size_t news) -> T*
		{
			if (p == nullptr)
				return nullptr;
			return realloc != nullptr ? static_cast<T*>(realloc(p, olds, news)) : nullptr;
		}

		template<typename T>
		auto malloc_t (size_t count) -> T*
		{
			return malloc != nullptr ? static_cast<T*>(malloc(sizeof(T) * count)) : nullptr;
		}

		auto decode_malloc_guesssize_headerflag() -> uint8_t*;
	};
}



#endif //ZLIB_HPP
