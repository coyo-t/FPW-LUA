#ifndef lua_coyote_exts
#define lua_coyote_exts

#include "lua.hpp"

extern "C" {

#define LC_MACRO_GETTER(NAME, MACRO) \
LUA_API \
int luacoyote_get_ ## NAME (void);\
int luacoyote_get_ ## NAME (void) { return (MACRO); }


LC_MACRO_GETTER(ridx_mainthread, LUA_RIDX_MAINTHREAD)
LC_MACRO_GETTER(ridx_globals, LUA_RIDX_GLOBALS)
LC_MACRO_GETTER(ridx_last, LUA_RIDX_LAST)

LC_MACRO_GETTER(registry_index, LUA_REGISTRYINDEX)


LUA_API
int luacoyote_get_upval_index (int index);
int luacoyote_get_upval_index (int index)
{
	return lua_upvalueindex(index);
}

LUA_API
void luacoyote_call (lua_State* L, int nargs, int nresults);
void luacoyote_call (lua_State* L, int nargs, int nresults)
{
	lua_call(L, nargs, nresults);
}

LUA_API
int luacoyote_pcall (lua_State* L, int n, int r, int f);
int luacoyote_pcall (lua_State* L, int n, int r, int f)
{
	return lua_pcall(L, n, r, f);
}

LUA_API
int luacoyote_yield (lua_State* L, int nresults);
int luacoyote_yield (lua_State* L, int nresults)
{
	return lua_yield(L, nresults);
}

LUA_API
void* luacoyote_getextraspace (lua_State* L);
void* luacoyote_getextraspace (lua_State* L)
{
	return lua_getextraspace(L);
}

LUA_API
lua_Number luacoyote_tonumber (lua_State* L, int i);
lua_Number luacoyote_tonumber (lua_State* L, int i)
{
	return lua_tonumber(L, i);
}

LUA_API
lua_Integer luacoyote_tointeger (lua_State* L, int i);
lua_Integer luacoyote_tointeger (lua_State* L, int i)
{
	return lua_tointeger(L, i);
}

LUA_API
void luacoyote_pop (lua_State* L, int n);
void luacoyote_pop (lua_State* L, int n)
{
	lua_pop(L, n);
}

LUA_API
void luacoyote_newtable (lua_State* L);
void luacoyote_newtable (lua_State* L)
{
	lua_newtable(L);
}

LUA_API
void luacoyote_register (lua_State* L, const char* name, lua_CFunction f);
void luacoyote_register (lua_State* L, const char* name, lua_CFunction f)
{
	lua_register(L, name, f);
}

LUA_API
void luacoyote_pushcfunction (lua_State* L, lua_CFunction f);
void luacoyote_pushcfunction (lua_State* L, lua_CFunction f)
{
	lua_pushcfunction(L, f);
}

LUA_API
int luacoyote_isfunction (lua_State* L, int n);
int luacoyote_isfunction (lua_State* L, int n)
{
	return lua_isfunction(L, n);
}

LUA_API
int luacoyote_istable (lua_State* L, int n);
int luacoyote_istable (lua_State* L, int n)
{
	return lua_istable(L, n);
}

LUA_API
int luacoyote_islightuserdata (lua_State* L, int n);
int luacoyote_islightuserdata (lua_State* L, int n)
{
	return lua_islightuserdata(L, n);
}

LUA_API
int luacoyote_isnil (lua_State* L, int n);
int luacoyote_isnil (lua_State* L, int n)
{
	return lua_isnil(L, n);
}

LUA_API
int luacoyote_isboolean (lua_State* L, int n);
int luacoyote_isboolean (lua_State* L, int n)
{
	return lua_isboolean(L, n);
}

LUA_API
int luacoyote_isthread (lua_State* L, int n);
int luacoyote_isthread (lua_State* L, int n)
{
	return lua_isthread(L, n);
}

LUA_API
int luacoyote_isnone (lua_State* L, int n);
int luacoyote_isnone (lua_State* L, int n)
{
	return lua_isnone(L, n);
}

LUA_API
int luacoyote_isnoneornil (lua_State* L, int n);
int luacoyote_isnoneornil (lua_State* L, int n)
{
	return lua_isnoneornil(L, n);
}

LUA_API
void luacoyote_pushglobaltable (lua_State* L);
void luacoyote_pushglobaltable (lua_State* L)
{
	lua_pushglobaltable(L);
}

LUA_API
const char* luacoyote_tostring (lua_State* L, int index);
const char* luacoyote_tostring (lua_State* L, int index)
{
	return lua_tostring(L, index);
}

LUA_API
void luacoyote_insert (lua_State* L, int index);
void luacoyote_insert (lua_State* L, int index)
{
	lua_insert(L, index);
}

LUA_API
void luacoyote_remove (lua_State* L, int index);
void luacoyote_remove (lua_State* L, int index)
{
	lua_remove(L, index);
}

LUA_API
void luacoyote_replace (lua_State* L, int index);
void luacoyote_replace (lua_State* L, int index)
{
	lua_replace(L, index);
}

}

#endif