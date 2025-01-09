
#ifndef COYOTE_BUFFER_LIB
#define COYOTE_BUFFER_LIB

#include <cstring>
#include <bit>

#include "llimits.hpp"
#include "../lua.hpp"
#include "../lauxlib.hpp"

namespace CoyoteBuffer {

using Byte = lu_byte;

enum class BufferType
{
	Fixed,
	Grow,
};

enum class BufferError
{
	NoAlienOvO,
	Underflow,
	Overflow,
};


struct Buffer
{
	size_t size;
	size_t cursor;
	std::endian order;
	Byte data[];

	void fill (Byte value)
	{
		std::memset(&(this->data), value, size);
	}

	static auto createsize (size_t size) -> size_t
	{
		return sizeof(Buffer) + (size * sizeof(Byte));
	}

};


#define COYOTE_BUFFER_REG "GML_BUFFER*"

static auto l_create_buffer (lua_State* L, size_t size) -> Buffer*
{
	return static_cast<Buffer*>(lua_newuserdatauv(L, Buffer::createsize(size), 0));
}

static int f_create (lua_State* L)
{
	lua_Integer count = luaL_checkinteger(L, 1);

	if (count < 0)
	{
		return luaL_error(L, "Buffer size %d less than 0", count);
	}

	auto f = l_create_buffer(L, count);
	f->fill(0);

	luaL_setmetatable(L, COYOTE_BUFFER_REG);

	return 1;
}


}






static constexpr luaL_Reg funcs[] = {
	{"create", CoyoteBuffer::f_create},
	luaL_Reg::end(),
};

LUALIB_API int createbufferlib (lua_State* L)
{
	luaL_newlib(L, funcs);
	return 1;
}


#endif
