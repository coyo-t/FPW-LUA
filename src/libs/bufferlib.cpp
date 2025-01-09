
#ifndef COYOTE_BUFFER_LIB
#define COYOTE_BUFFER_LIB

#include "llimits.hpp"
#include "../lua.hpp"
#include "../lauxlib.hpp"
#include <cstring>

namespace CoyoteBuffer {

using Byte = lu_byte;


struct Buffer
{
	size_t size;
	size_t cursor;
	Byte data[];

	static auto createsize (size_t size) -> size_t
	{
		return sizeof(Buffer) + (size * sizeof(Byte));
	}
};

enum class BufferError
{
	NO_ALIEN_OvO,
	UNDERFLOW,
	OVERFLOW,
};







#define COYOTE_BUFFER_REG "GML_BUFFER*"

// static Byte* checkbuffer (lua_State* L, int index)
// {
// 	return static_cast<Byte*>(luaL_checkudata(L, index, COYOTE_BUFFER_REG));
// }
//
// static lua_Integer buffer_tell (lua_State* L, int i)
// {
// 	lua_getiuservalue(L, i, 1);
// 	lua_Integer outs = lua_tointeger(L, -1);
// 	lua_pop(L, 1);
// 	return outs;
// }
//
// static lua_Integer buffer_get_size (lua_State* L, int i)
// {
// 	lua_getiuservalue(L, i, 2);
// 	lua_Integer outs = lua_tointeger(L, -1);
// 	lua_pop(L, 1);
// 	return outs;
// }
//
// static int f_create (lua_State* L)
// {
// 	lua_Integer count = luaL_checkinteger(L, 1);
//
// 	if (count < 0)
// 	{
// 		return luaL_error(L, "Buffer size %d less than 0", count);
// 	}
//
// 	auto* f = static_cast<Byte*>(lua_newuserdatauv(L, count, 2));
// 	std::memset(f, 0, static_cast<size_t>(count));
//
// 	// cursor
// 	lua_pushinteger(L, 0);
// 	lua_setiuservalue(L, -2, 1);
//
// 	// size
// 	lua_pushinteger(L, count);
// 	lua_setiuservalue(L, -2, 2);
//
// 	luaL_setmetatable(L, COYOTE_BUFFER_REG);
//
// 	return 1;
// }
//
//
// static int f_read_byte (lua_State* L)
// {
// 	auto f = checkbuffer(L, 1);
//
// 	auto cursor = buffer_tell(L, 1);
//
// 	if (cursor < 0)
// 	{
// 		return luaL_error(L, "Buffer underflow: %d", cursor);
// 	}
//
// 	auto size = buffer_get_size(L, 1);
//

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
