/*
** $Id: ldo.h $
** Stack and Call structure of Lua
** See Copyright Notice in lua.h
*/

#ifndef ldo_h
#define ldo_h


#include "llimits.hpp"
#include "lobject.hpp"
#include "lstate.hpp"
#include "lzio.hpp"

namespace luaD {

/* In general, 'pre'/'pos' are empty (nothing to save) */
LUAI_FUNCA checkstack (lua_State* L, int n) -> void;
LUAI_FUNCA savestack (lua_State* L, StkId pt) -> ptrdiff_t;
LUAI_FUNCA restorestack(lua_State* L, ptrdiff_t n) -> StkId;

/* macro to check stack size, preserving 'p' */
LUAI_FUNCA checkstackp (lua_State*L, int n, StkId& p) -> void;

/* macro to check stack size and GC, preserving 'p' */
LUAI_FUNCA checkstackGCp (lua_State* L, int n, StkId& p) -> void;

/* macro to check stack size and GC */
LUAI_FUNCA checkstackGC (lua_State* L, int fsize) -> void;

/* type of protected functions, to be ran by 'runprotected' */
using Pfunc = auto (*) (lua_State *L, void *ud) -> void;

LUAI_FUNCA seterrorobj (lua_State *L, int errcode, StkId oldtop) -> void;
LUAI_FUNCA protectedparser (
	lua_State *L,
	ZIO *z,
	const char *name,
	const char *mode
) -> int;

LUAI_FUNCA hook (
	lua_State *L,
	int event,
	int line,
	int fTransfer,
	int nTransfer
) -> void;

LUAI_FUNCA hookcall (lua_State *L, CallInfo *ci) -> void;
LUAI_FUNCA pretailcall (
	lua_State *L,
	CallInfo *ci,
	StkId func,
	int narg1,
	int delta
) -> int;
LUAI_FUNCA precall (lua_State *L, StkId func, int nResults) -> CallInfo*;
LUAI_FUNCA call (lua_State *L, StkId func, int nResults) -> void;
LUAI_FUNCA callnoyield (lua_State *L, StkId func, int nResults) -> void;
LUAI_FUNCA closeprotected (lua_State *L, ptrdiff_t level, int status) -> int;
LUAI_FUNCA pcall (
	lua_State *L,
	Pfunc func,
	void *u,
	ptrdiff_t oldtop,
	ptrdiff_t ef
) -> int;
LUAI_FUNCA poscall (lua_State *L, CallInfo *ci, int nres) -> void;
LUAI_FUNCA reallocstack (lua_State *L, int newsize, int raiseerror) -> int;
LUAI_FUNCA growstack (lua_State *L, int n, int raiseerror) -> int;
LUAI_FUNCA shrinkstack (lua_State *L) -> void;
LUAI_FUNCA inctop (lua_State *L) -> void;

LUAI_FUNC l_noret lthrow (lua_State *L, int errcode);

LUAI_FUNCA rawrunprotected (lua_State *L, Pfunc f, void *ud) -> int;

}

#endif

