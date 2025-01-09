/*
** $Id: lfunc.h $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lua.h
*/

#ifndef lfunc_h
#define lfunc_h


#include "lobject.hpp"


#define sizeCclosure(n)	(cast_int(offsetof(CClosure, upvalue)) + \
                         cast_int(sizeof(TValue)) * (n))

#define sizeLclosure(n)	(cast_int(offsetof(LClosure, upvals)) + \
                         cast_int(sizeof(TValue *)) * (n))


/* test whether thread is in 'twups' list */
#define isintwups(L)	(L->twups != L)


/*
** maximum number of upvalues in a closure (both C and Lua). (Value
** must fit in a VM register.)
*/
#define MAXUPVAL	255


#define upisopen(up)	((up)->v.p != &(up)->u.value)


#define uplevel(up)	check_exp(upisopen(up), cast(StkId, (up)->v.p))


/*
** maximum number of misses before giving up the cache of closures
** in prototypes
*/
#define MAXMISS		10



/* special status to close upvalues preserving the top of the stack */
#define CLOSEKTOP	(-1)

namespace luaF {

LUAI_FUNC Proto *newproto (lua_State *L);
LUAI_FUNC CClosure *newCclosure (lua_State *L, int nupvals);
LUAI_FUNC LClosure *newLclosure (lua_State *L, int nupvals);
LUAI_FUNC void initupvals (lua_State *L, LClosure *cl);
LUAI_FUNC UpVal *findupval (lua_State *L, StkId level);
LUAI_FUNC void newtbcupval (lua_State *L, StkId level);
LUAI_FUNC void closeupval (lua_State *L, StkId level);
LUAI_FUNC StkId close (lua_State *L, StkId level, int status, int yy);
LUAI_FUNC void unlinkupval (UpVal *uv);
LUAI_FUNC void freeproto (lua_State *L, Proto *f);
LUAI_FUNC const char *getlocalname (const Proto *func, int local_number,
                                         int pc);
}


#endif
