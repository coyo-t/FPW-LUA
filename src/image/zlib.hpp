

#ifndef ZLIB_HPP
#define ZLIB_HPP

#include<cstdint>
#include<stdexcept>

namespace Zlib {

	using std::uint8_t;
	using std::size_t;

	struct Err final : std::exception
	{
		const char* reason;

		explicit Err(const char * str): reason(str)
		{
		}
	};

	struct Context
	{
		using MallocCallback = auto (void* self, size_t size) -> void*;
		using FreeCallback = auto (void* self, void* addr) -> void;
		using ReallocCallback = auto (
			void* self,
			void* addr,
			size_t old_size,
			size_t new_size
		) -> void*;

		void* allocation_self = nullptr;

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

		template<typename T>
		auto free_t (T* p)
		{
			if (free != nullptr)
			{
				if (p != nullptr)
				{
					free(allocation_self, p);
				}
			}
		}

		template<typename T>
		auto realloc_t (T* p, size_t olds, size_t news) -> T*
		{
			if (p == nullptr)
				return nullptr;
			return realloc != nullptr
				? static_cast<T*>(realloc(allocation_self, p, olds, news))
				: nullptr;
		}

		template<typename T>
		auto malloc_t (size_t count) -> T*
		{
			return malloc != nullptr
				? static_cast<T*>(malloc(allocation_self, sizeof(T) * count))
				: nullptr;
		}

		auto decode_malloc_guesssize_headerflag() -> uint8_t*;
	};
}



#endif //ZLIB_HPP
