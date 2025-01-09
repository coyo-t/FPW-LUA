/*
** $Id: lundump.h $
** load precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#ifndef lundump_h
#define lundump_h

#include "llimits.hpp"
#include "lobject.hpp"
#include "lzio.hpp"


/* data to catch conversion errors */
constexpr auto LUAC_DATA =	"\x19\x93\r\n\x1a\n";

constexpr auto LUAC_INT =	0x5678;
constexpr auto LUAC_NUM =	static_cast<lua_Number>(370.5);

/*
** Encode major-minor version in one byte, one nibble for each
*/
constexpr auto LUAC_VERSION  (((LUA_VERSION_NUM / 100) * 16) + LUA_VERSION_NUM % 100);

constexpr auto LUAC_FORMAT =	0;	/* this is the official format */

/* load one chunk; from lundump.c */
LUAI_FUNC LClosure* luaU_undump (lua_State* L, ZIO* Z, const char* name);

/* dump one chunk; from ldump.c */
LUAI_FUNC int luaU_dump (lua_State* L, const Proto* f, lua_Writer w, void* data, int strip);

#endif
