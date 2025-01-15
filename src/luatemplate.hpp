
#ifndef LUATEMPLATE_HPP
#define LUATEMPLATE_HPP

#include "lua.hpp"

template<typename T>
T* lua_newuserdatauvt (lua_State* L, int nuvalue)
{
	return static_cast<T*>(lua_newuserdatauv(L, sizeof(T), nuvalue));
}

#endif //LUATEMPLATE_HPP
