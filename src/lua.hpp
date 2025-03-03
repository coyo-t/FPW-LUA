/*
** $Id: lua.h $
** Lua - A Scripting Language
** Lua.org, PUC-Rio, Brazil (http://www.lua.org)
** See Copyright Notice at the end of this file
*/


#ifndef lua_h
#define lua_h

#include <cstdarg>
#include <cstddef>

extern "C" {


#include "luaconf.hpp"


#define LUA_VERSION_MAJOR "5"
#define LUA_VERSION_MINOR "4"
#define LUA_VERSION_RELEASE "7"

constexpr auto LUA_VERSION_NUM = 504;
constexpr auto LUA_VERSION_RELEASE_NUM = LUA_VERSION_NUM * 100 + 7;

#define LUA_VERSION	"Lua " LUA_VERSION_MAJOR "." LUA_VERSION_MINOR
#define LUA_RELEASE	LUA_VERSION "." LUA_VERSION_RELEASE
#define LUA_COPYRIGHT	LUA_RELEASE "  Copyright (C) 1994-2024 Lua.org, PUC-Rio"
#define LUA_AUTHORS	"R. Ierusalimschy, L. H. de Figueiredo, W. Celes"


/* mark for precompiled code ('<esc>Lua') */
#define LUA_SIGNATURE "\x1bLua"

/* option for multiple returns in 'lua_pcall' and 'lua_call' */
constexpr auto LUA_MULTRET = -1;


/*
** Pseudo-indices
** (-LUAI_MAXSTACK is the minimum valid index; we keep some free empty
** space after that to help overflow detection)
*/
constexpr auto LUA_REGISTRYINDEX =	-LUAI_MAXSTACK - 1000;

LUA_API int lua_upvalueindex (int i);
LUA_API int lua_getregistryindex ();

/* thread status */
constexpr auto LUA_OK = 0;
constexpr auto LUA_YIELD = 1;
constexpr auto LUA_ERRRUN = 2;
constexpr auto LUA_ERRSYNTAX = 3;
constexpr auto LUA_ERRMEM = 4;
constexpr auto LUA_ERRERR = 5;


using lua_State = struct lua_State;


/*
** basic types
*/
constexpr auto LUA_TNONE = -1;

constexpr auto LUA_TNIL = 0;
constexpr auto LUA_TBOOLEAN = 1;
constexpr auto LUA_TLIGHTUSERDATA = 2;
constexpr auto LUA_TNUMBER = 3;
constexpr auto LUA_TSTRING = 4;
constexpr auto LUA_TTABLE = 5;
constexpr auto LUA_TFUNCTION = 6;
constexpr auto LUA_TUSERDATA = 7;
constexpr auto LUA_TTHREAD = 8;

constexpr auto LUA_NUMTYPES = 9;


/* minimum Lua stack available to a C function */
constexpr auto LUA_MINSTACK =	20;


/* predefined values in the registry */
constexpr auto LUA_RIDX_MAINTHREAD = 1;
constexpr auto LUA_RIDX_GLOBALS = 2;
constexpr auto LUA_RIDX_LAST = LUA_RIDX_GLOBALS;


/* type of numbers in Lua */
using lua_Number = LUA_NUMBER;

/* type for integer functions */
using lua_Integer = LUA_INTEGER;

/* unsigned integer type */
using lua_Unsigned = LUA_UNSIGNED;

/* type for continuation-function contexts */
using lua_KContext = LUA_KCONTEXT;


/*
** Type for C functions registered with Lua
*/
using lua_CFunction = auto (*)(lua_State* L) -> int;

/*
** Type for continuation functions
*/
using lua_KFunction = auto (*)(lua_State *L, int status, lua_KContext ctx) -> int;

/*
** Type for functions that read/write blocks when loading/dumping Lua chunks
*/
using lua_Reader = auto (*)(lua_State *L, void *ud, size_t *sz) -> const char*;
using lua_Writer = auto (*)(lua_State *L, const void *p, size_t sz, void *ud) -> int;

/*
** Type for memory-allocation functions
*/
using lua_Alloc = auto (*)(void *ud, void *ptr, size_t osize, size_t nsize) -> void*;

/*
** Type for warning functions
*/
using lua_WarnFunction = auto (*)(void *ud, const char *msg, int tocont) -> void;


/*
** Type used by the debug API to collect debug information
*/
using lua_Debug = struct lua_Debug;

/*
** Functions to be called by the debugger in specific events
*/
using lua_Hook = auto (*)(lua_State *L, lua_Debug *ar) -> void;


/*
** RCS ident string
*/
extern const char lua_ident[];


/*
** state manipulation
*/
LUA_APIA lua_newstate(lua_Alloc f, void *ud) -> lua_State*;
LUA_APIA lua_close(lua_State *L) -> void;
LUA_APIA lua_newthread(lua_State *L) -> lua_State*;
LUA_APIA lua_closethread(lua_State *L, lua_State *from) -> int;
LUA_APIA lua_atpanic(lua_State *L, lua_CFunction panicf) -> lua_CFunction;
LUA_APIA lua_version(lua_State *L) -> lua_Number;


/*
** basic stack manipulation
*/
LUA_APIA lua_absindex(lua_State *L, int idx) -> int;
LUA_APIA lua_gettop(lua_State *L) -> int;
LUA_APIA lua_settop(lua_State *L, int idx) -> void;
LUA_APIA lua_pushvalue(lua_State *L, int idx) -> void;
LUA_APIA lua_rotate(lua_State *L, int idx, int n) -> void;
LUA_APIA lua_copy(lua_State *L, int fromidx, int toidx) -> void;
LUA_APIA lua_checkstack(lua_State *L, int n) -> int;
LUA_APIA lua_xmove(lua_State *from, lua_State *to, int n) -> void;


/*
** access functions (stack -> C)
*/

LUA_APIA lua_isnumber(lua_State *L, int idx) -> int;
LUA_APIA lua_isstring(lua_State *L, int idx) -> int;
LUA_APIA lua_iscfunction(lua_State *L, int idx) -> int;
LUA_APIA lua_isinteger(lua_State *L, int idx) -> int;
LUA_APIA lua_isuserdata(lua_State *L, int idx) -> int;
LUA_APIA lua_type(lua_State *L, int idx) -> int;
LUA_APIA lua_typename(lua_State *L, int tp) -> const char*;
LUA_APIA lua_tonumberx(lua_State *L, int idx, int *isnum) -> lua_Number;
LUA_APIA lua_tointegerx(lua_State *L, int idx, int *isnum) -> lua_Integer;
LUA_APIA lua_toboolean(lua_State *L, int idx) -> int;
LUA_APIA lua_tolstring(lua_State *L, int idx, size_t *len) -> const char*;
LUA_APIA lua_rawlen(lua_State *L, int idx) -> lua_Unsigned;
LUA_APIA lua_tocfunction(lua_State *L, int idx) -> lua_CFunction;
LUA_APIA lua_touserdata(lua_State *L, int idx) -> void*;
LUA_APIA lua_tothread(lua_State *L, int idx) -> lua_State*;
LUA_APIA lua_topointer(lua_State *L, int idx) -> const void*;


/*
** Comparison and arithmetic functions
*/

/* ORDER TM, ORDER OP */
constexpr auto LUA_OPADD  = 0;
constexpr auto LUA_OPSUB  = 1;
constexpr auto LUA_OPMUL  = 2;
constexpr auto LUA_OPMOD  = 3;
constexpr auto LUA_OPPOW  = 4;
constexpr auto LUA_OPDIV  = 5;
constexpr auto LUA_OPIDIV = 6;
constexpr auto LUA_OPBAND = 7;
constexpr auto LUA_OPBOR  = 8;
constexpr auto LUA_OPBXOR = 9;
constexpr auto LUA_OPSHL  = 10;
constexpr auto LUA_OPSHR  = 11;
constexpr auto LUA_OPUNM  = 12;
constexpr auto LUA_OPBNOT = 13;

// enum class LuaArithOp : int
// {
// 	ADD = 0,
// 	SUB = 1,
// 	MUL = 2,
// 	MOD = 3,
// 	POW = 4,
// 	DIV = 5,
// 	IDIV = 6,
// 	BAND = 7,
// 	BOR = 8,
// 	BXOR = 9,
// 	SHL = 10,
// 	SHR = 11,
// 	UNM = 12,
// 	BNOT = 13,
// };

enum class LuaCompareOp : int
{
	EQ = 0,
	LT = 1,
	LE = 2,
};

LUA_APIA lua_arith(lua_State *L, int op) -> void;
LUA_APIA lua_rawequal(lua_State *L, int idx1, int idx2) -> int;
LUA_APIA lua_compare(lua_State *L, int idx1, int idx2, LuaCompareOp op) -> int;


/*
** push functions (C -> stack)
*/
LUA_APIA lua_pushnil(lua_State *L) -> void;
LUA_APIA lua_pushnumber(lua_State *L, lua_Number n) -> void;
LUA_APIA lua_pushinteger(lua_State *L, lua_Integer n) -> void;
LUA_APIA lua_pushlstring(lua_State *L, const char *s, size_t len) -> const char*;
LUA_APIA lua_pushstring(lua_State *L, const char *s) -> const char*;
LUA_APIA lua_pushvfstring(lua_State *L, const char *fmt, va_list argp) -> const char*;
LUA_APIA lua_pushfstring(lua_State *L, const char *fmt, ...) -> const char *;
LUA_APIA lua_pushcclosure(lua_State *L, lua_CFunction fn, int n) -> void;
LUA_APIA lua_pushboolean(lua_State *L, int b) -> void;
LUA_APIA lua_pushlightuserdata(lua_State *L, void *p) -> void;
LUA_APIA lua_pushthread(lua_State *L) -> int;


/*
** get functions (Lua -> stack)
*/
LUA_APIA lua_getglobal(lua_State *L, const char *name) -> int;
LUA_APIA lua_gettable(lua_State *L, int idx) -> int;
LUA_APIA lua_getfield(lua_State *L, int idx, const char *k) -> int;
LUA_APIA lua_geti(lua_State *L, int idx, lua_Integer n) -> int;
LUA_APIA lua_rawget(lua_State *L, int idx) -> int;
LUA_APIA lua_rawgeti(lua_State *L, int idx, lua_Integer n) -> int;
LUA_APIA lua_rawgetp(lua_State *L, int idx, const void *p) -> int;
LUA_APIA lua_createtable(lua_State *L, int narr, int nrec) -> void;
LUA_APIA lua_newuserdatauv(lua_State *L, size_t sz, int nuvalue) -> void*;
LUA_APIA lua_getmetatable(lua_State *L, int objindex) -> int;
LUA_APIA lua_getiuservalue(lua_State *L, int idx, int n) -> int;


/*
** set functions (stack -> Lua)
*/
LUA_API void (lua_setglobal)(lua_State *L, const char *name);
LUA_API void (lua_settable)(lua_State *L, int idx);
LUA_API void (lua_setfield)(lua_State *L, int idx, const char *k);
LUA_API void (lua_seti)(lua_State *L, int idx, lua_Integer n);
LUA_API void (lua_rawset)(lua_State *L, int idx);
LUA_API void (lua_rawseti)(lua_State *L, int idx, lua_Integer n);
LUA_API void (lua_rawsetp)(lua_State *L, int idx, const void *p);
LUA_API int (lua_setmetatable)(lua_State *L, int objindex);
LUA_API int (lua_setiuservalue)(lua_State *L, int idx, int n);


/*
** 'load' and 'call' functions (load and run Lua code)
*/
LUA_API void (lua_callk)(lua_State *L, int nargs, int nresults,
								lua_KContext ctx, lua_KFunction k);

LUA_API void lua_call(lua_State* L, int n, int r);

LUA_API int (lua_pcallk)(lua_State *L, int nargs, int nresults, int errfunc,
								lua_KContext ctx, lua_KFunction k);

LUA_API int lua_pcall(lua_State* L, int n, int r, int f);

LUA_API int (lua_load)(lua_State *L, lua_Reader reader, void *dt,
								const char *chunkname, const char *mode);

LUA_API int (lua_dump)(lua_State *L, lua_Writer writer, void *data, int strip);


/*
** coroutine functions
*/
LUA_API int (lua_yieldk)(lua_State *L, int nresults, lua_KContext ctx,
								lua_KFunction k);

LUA_API int (lua_resume)(lua_State *L, lua_State *from, int narg,
								int *nres);

LUA_API int (lua_status)(lua_State *L);

LUA_API int (lua_isyieldable)(lua_State *L);

LUA_API int lua_yield (lua_State* L, int nresults);

/*
** Warning-related functions
*/
LUA_API void (lua_setwarnf)(lua_State *L, lua_WarnFunction f, void *ud);

LUA_API void (lua_warning)(lua_State *L, const char *msg, int tocont);


/*
** garbage-collection function and options
*/

constexpr auto LUA_GCSTOP       = 0;
constexpr auto LUA_GCRESTART    = 1;
constexpr auto LUA_GCCOLLECT    = 2;
constexpr auto LUA_GCCOUNT      = 3;
constexpr auto LUA_GCCOUNTB     = 4;
constexpr auto LUA_GCSTEP       = 5;
constexpr auto LUA_GCSETPAUSE   = 6;
constexpr auto LUA_GCSETSTEPMUL = 7;
constexpr auto LUA_GCISRUNNING  = 9;
constexpr auto LUA_GCGEN        = 10;
constexpr auto LUA_GCINC        = 11;

LUA_APIA lua_gc(lua_State *L, int what, ...) -> int;


/*
** miscellaneous functions
*/

LUA_APIA lua_error(lua_State *L) -> int;
LUA_APIA lua_next(lua_State *L, int idx) -> int;
LUA_APIA lua_concat(lua_State *L, int n) -> void;
LUA_APIA lua_len(lua_State *L, int idx) -> void;
LUA_APIA lua_stringtonumber(lua_State *L, const char *s) -> size_t;
LUA_APIA lua_getallocf(lua_State *L, void **ud) -> lua_Alloc;
LUA_APIA lua_setallocf(lua_State *L, lua_Alloc f, void *ud) -> void;
LUA_APIA lua_toclose(lua_State *L, int idx) -> void;
LUA_APIA lua_closeslot(lua_State *L, int idx) -> void;


/*
** {==============================================================
** some useful macros
** ===============================================================
*/

LUA_APIA lua_getextraspace (lua_State* L) -> void*;

LUA_APIA lua_tonumber (lua_State* L, int i) -> lua_Number;
LUA_APIA lua_tointeger(lua_State* L, int i) -> lua_Integer;

LUA_APIA lua_pop(lua_State*L, int n) -> void;
LUA_APIA lua_newtable(lua_State* L) -> void;
LUA_APIA lua_register(lua_State* L, const char* n, lua_CFunction f) -> void;
LUA_APIA lua_pushcfunction(lua_State *L, lua_CFunction f) -> void;

LUA_APIA lua_isfunction (lua_State* L, int n) -> bool;
LUA_APIA lua_istable (lua_State* L, int n) -> bool;
LUA_APIA lua_islightuserdata (lua_State* L, int n) -> bool;
LUA_APIA lua_isnil (lua_State* L, int n) -> bool;
LUA_APIA lua_isboolean (lua_State* L, int n) -> bool;
LUA_APIA lua_isthread (lua_State* L, int n) -> bool;
LUA_APIA lua_isnone (lua_State* L, int n) -> bool;
LUA_APIA lua_isnoneornil (lua_State* L, int  n) -> bool;


// kept as macro. the "" s is used so the compiler yells at you
// if s is not a string literal
#define lua_pushliteral(L, s)	lua_pushstring(L, "" s)

LUA_APIA lua_pushglobaltable(lua_State* L) -> void;
LUA_APIA lua_tostring(lua_State* L, int i) -> const char*;

LUA_APIA lua_insert (lua_State* L, int idx) -> void;
LUA_APIA lua_remove (lua_State* L, int idx) -> void;
LUA_APIA lua_replace (lua_State* L, int idx) -> void;

/* }============================================================== */


constexpr auto LUA_NUMTAGS = LUA_NUMTYPES;

/* }============================================================== */

/*
** {======================================================================
** Debug API
** =======================================================================
*/


/*
** Event codes
*/
constexpr auto LUA_HOOKCALL     = 0;
constexpr auto LUA_HOOKRET      = 1;
constexpr auto LUA_HOOKLINE     = 2;
constexpr auto LUA_HOOKCOUNT    = 3;
constexpr auto LUA_HOOKTAILCALL = 4;


/*
** Event masks
*/
constexpr auto LUA_MASKCALL  = 1 << LUA_HOOKCALL;
constexpr auto LUA_MASKRET   = 1 << LUA_HOOKRET;
constexpr auto LUA_MASKLINE  = 1 << LUA_HOOKLINE;
constexpr auto LUA_MASKCOUNT = 1 << LUA_HOOKCOUNT;


LUA_APIA lua_getstack(lua_State *L, int level, lua_Debug *ar) -> int;
LUA_APIA lua_getinfo(lua_State *L, const char *what, lua_Debug *ar) -> int;
LUA_APIA lua_getlocal(lua_State *L, const lua_Debug *ar, int n) -> const char*;
LUA_APIA lua_setlocal(lua_State *L, const lua_Debug *ar, int n) -> const char*;
LUA_APIA lua_getupvalue(lua_State *L, int funcindex, int n) -> const char*;
LUA_APIA lua_setupvalue(lua_State *L, int funcindex, int n) -> const char*;
LUA_APIA lua_upvalueid(lua_State *L, int fidx, int n) -> void*;
LUA_APIA lua_upvaluejoin(lua_State *L, int fidx1, int n1, int fidx2, int n2) -> void;
LUA_APIA lua_sethook(lua_State *L, lua_Hook func, int mask, int count) -> void;
LUA_APIA lua_gethook(lua_State *L) -> lua_Hook;
LUA_APIA lua_gethookmask(lua_State *L) -> int;
LUA_APIA lua_gethookcount(lua_State *L) -> int;
LUA_APIA lua_setcstacklimit(lua_State *L, unsigned int limit) -> int;

struct lua_Debug
{
	int event;
	const char *name; /* (n) */
	const char *namewhat; /* (n) 'global', 'local', 'field', 'method' */
	const char *what; /* (S) 'Lua', 'C', 'main', 'tail' */
	const char *source; /* (S) */
	size_t srclen; /* (S) */
	int currentline; /* (l) */
	int linedefined; /* (S) */
	int lastlinedefined; /* (S) */
	unsigned char nups; /* (u) number of upvalues */
	unsigned char nparams; /* (u) number of parameters */
	char isvararg; /* (u) */
	char istailcall; /* (t) */
	unsigned short ftransfer; /* (r) index of first value transferred */
	unsigned short ntransfer; /* (r) number of transferred values */
	char short_src[LUA_IDSIZE]; /* (S) */
	/* private part */
	struct CallInfo *i_ci; /* active function */
};

/* }====================================================================== */


/******************************************************************************
* Copyright (C) 1994-2024 Lua.org, PUC-Rio.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/
}

#endif
