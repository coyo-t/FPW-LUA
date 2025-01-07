/*
** $Id: lstring.h $
** String table (keep all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#ifndef lstring_h
#define lstring_h

#include "lgc.hpp"
#include "lobject.hpp"
#include "lstate.hpp"


/*
** Memory-allocation error message must be preallocated (it cannot
** be created after memory is exhausted)
*/
#define MEMERRMSG       "not enough memory"


#define luaS_newliteral(L, s)	(luaS::newlstr(L, "" s, (sizeof(s)/sizeof(char))-1))


/*
** test whether a string is a reserved word
*/
#define isreserved(s)	((s)->tt == LUA_VSHRSTR && (s)->extra > 0)


/*
** equality for short strings, which are always internalized
*/
#define eqshrstr(a,b)	check_exp((a)->tt == LUA_VSHRSTR, (a) == (b))


namespace luaS {

LUAI_FUNC unsigned int hash (const char *str, size_t l, unsigned int seed);
LUAI_FUNC unsigned int hashlongstr (TString *ts);
LUAI_FUNC int eqlngstr (TString *a, TString *b);
LUAI_FUNC void resize (lua_State *L, int newsize);
LUAI_FUNC void clearcache (global_State *g);
LUAI_FUNC void init (lua_State *L);
LUAI_FUNC void remove (lua_State *L, TString *ts);
LUAI_FUNC Udata *newudata (lua_State *L, size_t s, int nuvalue);
LUAI_FUNC TString *newlstr (lua_State *L, const char *str, size_t l);
LUAI_FUNC TString *news (lua_State *L, const char *str);
LUAI_FUNC TString *createlngstrobj (lua_State *L, size_t l);

}





#endif
