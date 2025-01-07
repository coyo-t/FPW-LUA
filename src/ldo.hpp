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


/*
** Macro to check stack size and grow stack if needed.  Parameters
** 'pre'/'pos' allow the macro to preserve a pointer into the
** stack across reallocations, doing the work only when needed.
** It also allows the running of one GC step when the stack is
** reallocated.
** 'condmovestack' is used in heavy tests to force a stack reallocation
** at every check.
*/

/* In general, 'pre'/'pos' are empty (nothing to save) */
LUAI_FUNC auto luaD_checkstack (lua_State* L, int n) -> void;
LUAI_FUNC auto savestack (lua_State* L, StkId pt) -> ptrdiff_t;
LUAI_FUNC auto restorestack(lua_State* L, ptrdiff_t n) -> StkId;


/* macro to check stack size, preserving 'p' */
LUAI_FUNC void checkstackp (lua_State*L, int n, StkId& p);

/* macro to check stack size and GC, preserving 'p' */
LUAI_FUNC void checkstackGCp (lua_State* L, int n, StkId& p);

/* macro to check stack size and GC */
LUAI_FUNC void checkstackGC (lua_State* L, int fsize);

/* type of protected functions, to be ran by 'runprotected' */
typedef void (*Pfunc) (lua_State *L, void *ud);

LUAI_FUNC void luaD_seterrorobj (lua_State *L, int errcode, StkId oldtop);
LUAI_FUNC int luaD_protectedparser (lua_State *L, ZIO *z, const char *name,
                                                  const char *mode);
LUAI_FUNC void luaD_hook (lua_State *L, int event, int line,
                                        int fTransfer, int nTransfer);
LUAI_FUNC void luaD_hookcall (lua_State *L, CallInfo *ci);
LUAI_FUNC int luaD_pretailcall (lua_State *L, CallInfo *ci, StkId func,
                                              int narg1, int delta);


namespace luaD {
LUAI_FUNC CallInfo *precall (lua_State *L, StkId func, int nResults);
LUAI_FUNC void call (lua_State *L, StkId func, int nResults);
LUAI_FUNC void callnoyield (lua_State *L, StkId func, int nResults);
LUAI_FUNC int closeprotected (lua_State *L, ptrdiff_t level, int status);
LUAI_FUNC int pcall (lua_State *L, Pfunc func, void *u, ptrdiff_t oldtop, ptrdiff_t ef);
LUAI_FUNC void poscall (lua_State *L, CallInfo *ci, int nres);
LUAI_FUNC int reallocstack (lua_State *L, int newsize, int raiseerror);
LUAI_FUNC int growstack (lua_State *L, int n, int raiseerror);
LUAI_FUNC void shrinkstack (lua_State *L);
LUAI_FUNC void inctop (lua_State *L);

LUAI_FUNC l_noret lthrow (lua_State *L, int errcode);

LUAI_FUNC int rawrunprotected (lua_State *L, Pfunc f, void *ud);

}

#endif

