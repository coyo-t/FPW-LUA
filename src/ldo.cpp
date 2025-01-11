/*
** $Id: ldo.c $
** Stack and Call structure of Lua
** See Copyright Notice in lua.h
*/

#define ldo_c
#define LUA_CORE

#include "lprefix.hpp"


#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "lua.hpp"

#include "lapi.hpp"
#include "ldebug.hpp"
#include "ldo.hpp"
#include "lfunc.hpp"
#include "lgc.hpp"
#include "lmem.hpp"
#include "lobject.hpp"
#include "lopcodes.hpp"
#include "lparser.hpp"
#include "lstate.hpp"
#include "lstring.hpp"
#include "ltable.hpp"
#include "ltm.hpp"
#include "dump/lundump.hpp"
#include "lvm.hpp"
#include "lzio.hpp"


#define errorstatus(s)	((s) > LUA_YIELD)


/*
** {======================================================
** Error-recovery functions
** =======================================================
*/

/*
** LUAI_THROW/LUAI_TRY define how Lua does exception handling. By
** default, Lua handles errors with exceptions when compiling as
** C++ code, with _longjmp/_setjmp when asked to use them, and with
** longjmp/setjmp otherwise.
*/
#if !defined(LUAI_THROW)				/* { */

#if defined(__cplusplus) && !defined(LUA_USE_LONGJMP)	/* { */

/* C++ exceptions */
#define LUAI_THROW(L,c)		throw(c)
#define LUAI_TRY(L,c,a) \
	try { a } catch(...) { if ((c)->status == 0) (c)->status = -1; }
#define luai_jmpbuf		int  /* dummy variable */

#endif							/* } */

#endif							/* } */


/* chain list of long jump buffers */
struct lua_longjmp
{
	struct lua_longjmp *previous;
	luai_jmpbuf b;
	volatile int status; /* error code */
};


auto luaD::checkstack(lua_State *L, int n) -> void
{
	if (l_unlikely(L->stack_last.p - L->top.p <= (n)))
	{
		luaD::growstack(L, n, 1);
	}
}

auto luaD::savestack(lua_State *L, StkId pt) -> ptrdiff_t
{
	return (cast_charp(pt) - cast_charp(L->stack.p));
}

auto luaD::restorestack(lua_State *L, ptrdiff_t n) -> StkId
{
	return cast(StkId, cast_charp(L->stack.p) + (n));
}

void luaD::checkstackp(lua_State *L, int n, StkId&p)
{
	if (l_unlikely(L->stack_last.p - L->top.p <= (n)))
	{
		auto pevStack = luaD::savestack(L, p);
		luaD::growstack(L, n, 1);
		p = cast(StkId, cast_charp(L->stack.p) + (pevStack));
	}
}

void luaD::checkstackGCp(lua_State *L, int n, StkId&p)
{
	if (l_unlikely(L->stack_last.p - L->top.p <= (n)))
	{
		auto pevStack = luaD::savestack(L, p);
		luaC_checkGC(L);
		luaD::growstack(L, n, 1);
		p = cast(StkId, cast_charp(L->stack.p) + (pevStack));
	}

}

void luaD::checkstackGC(lua_State *L, int fsize)
{
	if (l_unlikely(L->stack_last.p - L->top.p <= ((fsize))))
	{
		luaC_checkGC(L);
		luaD::growstack(L, (fsize), 1);
	}
}

void luaD::seterrorobj(lua_State *L, int errcode, StkId oldtop)
{
	switch (errcode)
	{
		case LUA_ERRMEM: {
			/* memory error? */
			setsvalue2s(L, oldtop, G(L)->memerrmsg); /* reuse preregistered msg. */
			break;
		}
		case LUA_ERRERR: {
			setsvalue2s(L, oldtop, luaS::newliteral(L, "error in error handling"));
			break;
		}
		case LUA_OK: {
			/* special case only for closing upvalues */
			setnilvalue(s2v(oldtop)); /* no error message */
			break;
		}
		default: {
			lua_assert(errorstatus(errcode)); /* real error */
			setobjs2s(L, oldtop, L->top.p - 1); /* error message on current top */
			break;
		}
	}
	L->top.p = oldtop + 1;
}


l_noret luaD::lthrow(lua_State *L, int errcode)
{
	if (L->errorJmp)
	{
		/* thread has an error handler? */
		L->errorJmp->status = errcode; /* set status */
		LUAI_THROW(L, L->errorJmp); /* jump to it */
	}
	else
	{
		/* thread has no error handler */
		global_State *g = G(L);
		errcode = luaE_resetthread(L, errcode); /* close all upvalues */
		if (g->mainthread->errorJmp)
		{
			/* main thread has a handler? */
			setobjs2s(L, g->mainthread->top.p++, L->top.p - 1); /* copy error obj. */
			luaD::lthrow(g->mainthread, errcode); /* re-throw in main thread */
		}
		else
		{
			/* no handler at all; abort */
			if (g->panic)
			{
				/* panic function? */
				lua_unlock(L);
				g->panic(L); /* call panic function (last chance to jump out) */
			}
			abort();
		}
	}
}


int luaD::rawrunprotected(lua_State *L, luaD::Pfunc f, void *ud)
{
	l_uint32 oldnCcalls = L->nCcalls;
	struct lua_longjmp lj;
	lj.status = LUA_OK;
	lj.previous = L->errorJmp; /* chain new error handler */
	L->errorJmp = &lj;
	LUAI_TRY(L, &lj, (*f)(L, ud);
	);
	L->errorJmp = lj.previous; /* restore old error handler */
	L->nCcalls = oldnCcalls;
	return lj.status;
}

/* }====================================================== */


/*
** {==================================================================
** Stack reallocation
** ===================================================================
*/


/*
** Change all pointers to the stack into offsets.
*/
static void relstack(lua_State *L)
{
	L->top.offset = luaD::savestack(L, L->top.p);
	L->tbclist.offset = luaD::savestack(L, L->tbclist.p);
	for (UpVal *up = L->openupval; up != nullptr; up = up->u.open.next)
		up->v.offset = luaD::savestack(L, uplevel(up));

	for (CallInfo *ci = L->ci; ci != nullptr; ci = ci->previous)
	{
		ci->top.offset = luaD::savestack(L, ci->top.p);
		ci->func.offset = luaD::savestack(L, ci->func.p);
	}
}


/*
** Change back all offsets into pointers.
*/
static void correctstack(lua_State *L)
{
	CallInfo *ci;
	UpVal *up;
	L->top.p = luaD::restorestack(L, L->top.offset);
	L->tbclist.p = luaD::restorestack(L, L->tbclist.offset);
	for (up = L->openupval; up != NULL; up = up->u.open.next)
		up->v.p = s2v(luaD::restorestack(L, up->v.offset));
	for (ci = L->ci; ci != NULL; ci = ci->previous)
	{
		ci->top.p = luaD::restorestack(L, ci->top.offset);
		ci->func.p = luaD::restorestack(L, ci->func.offset);
		if (ci->isLua())
			ci->u.l.trap = 1; /* signal to update 'trap' in 'luaV_execute' */
	}
}


/* some space for error handling */
constexpr auto ERRORSTACKSIZE =	LUAI_MAXSTACK + 200;

/*
** Reallocate the stack to a new size, correcting all pointers into it.
** In ISO C, any pointer use after the pointer has been deallocated is
** undefined behavior. So, before the reallocation, all pointers are
** changed to offsets, and after the reallocation they are changed back
** to pointers. As during the reallocation the pointers are invalid, the
** reallocation cannot run emergency collections.
**
** In case of allocation error, raise an error or return false according
** to 'raiseerror'.
*/
int luaD::reallocstack(lua_State *L, int newsize, int raiseerror)
{
	int oldsize = L->stacksize();
	StkId newstack;
	int oldgcstop = G(L)->gcstopem;
	lua_assert(newsize <= LUAI_MAXSTACK || newsize == ERRORSTACKSIZE);
	relstack(L); /* change pointers to offsets */
	G(L)->gcstopem = 1; /* stop emergency collection */
	newstack = luaM::reallocvector(L, L->stack.p, oldsize + EXTRA_STACK,
											newsize + EXTRA_STACK);
	G(L)->gcstopem = oldgcstop; /* restore emergency collection */
	if (l_unlikely(newstack == NULL))
	{
		/* reallocation failed? */
		correctstack(L); /* change offsets back to pointers */
		if (raiseerror)
			luaD::lthrow(L, LUA_ERRMEM);
		else return 0; /* do not raise an error */
	}
	L->stack.p = newstack;
	correctstack(L); /* change offsets back to pointers */
	L->stack_last.p = L->stack.p + newsize;
	for (int i = oldsize + EXTRA_STACK; i < newsize + EXTRA_STACK; i++)
		setnilvalue(s2v(newstack + i)); /* erase new segment */
	return 1;
}


/*
** Try to grow the stack by at least 'n' elements. When 'raiseerror'
** is true, raises any error; otherwise, return 0 in case of errors.
*/
int luaD::growstack(lua_State *L, int n, int raiseerror)
{
	int size = L->stacksize();
	if (l_unlikely(size > LUAI_MAXSTACK))
	{
		/* if stack is larger than maximum, thread is already using the
			extra space reserved for errors, that is, thread is handling
			a stack error; cannot grow further than that. */
		lua_assert(stacksize(L) == ERRORSTACKSIZE);
		if (raiseerror)
			luaD::lthrow(L, LUA_ERRERR); /* error inside message handler */
		return 0; /* if not 'raiseerror', just signal it */
	}
	else if (n < LUAI_MAXSTACK)
	{
		/* avoids arithmetic overflows */
		int newsize = 2 * size; /* tentative new size */
		int needed = cast_int(L->top.p - L->stack.p) + n;
		if (newsize > LUAI_MAXSTACK) /* cannot cross the limit */
			newsize = LUAI_MAXSTACK;
		if (newsize < needed) /* but must respect what was asked for */
			newsize = needed;
		if (l_likely(newsize <= LUAI_MAXSTACK))
			return luaD::reallocstack(L, newsize, raiseerror);
	}
	/* else stack overflow */
	/* add extra size to be able to handle the error message */
	luaD::reallocstack(L, ERRORSTACKSIZE, raiseerror);
	if (raiseerror)
		luaG_runerror(L, "stack overflow");
	return 0;
}


/*
** Compute how much of the stack is being used, by computing the
** maximum top of all call frames in the stack and the current top.
*/
static int stackinuse(lua_State *L)
{
	CallInfo *ci;
	int res;
	StkId lim = L->top.p;
	for (ci = L->ci; ci != NULL; ci = ci->previous)
	{
		if (lim < ci->top.p) lim = ci->top.p;
	}
	lua_assert(lim <= L->stack_last.p + EXTRA_STACK);
	res = cast_int(lim - L->stack.p) + 1; /* part of stack in use */
	if (res < LUA_MINSTACK)
		res = LUA_MINSTACK; /* ensure a minimum size */
	return res;
}


/*
** If stack size is more than 3 times the current use, reduce that size
** to twice the current use. (So, the final stack size is at most 2/3 the
** previous size, and half of its entries are empty.)
** As a particular case, if stack was handling a stack overflow and now
** it is not, 'max' (limited by LUAI_MAXSTACK) will be smaller than
** stacksize (equal to ERRORSTACKSIZE in this case), and so the stack
** will be reduced to a "regular" size.
*/
void luaD::shrinkstack(lua_State *L)
{
	int inuse = stackinuse(L);
	int max = (inuse > LUAI_MAXSTACK / 3) ? LUAI_MAXSTACK : inuse * 3;
	/* if thread is currently not handling a stack overflow and its
		size is larger than maximum "reasonable" size, shrink it */
	if (inuse <= LUAI_MAXSTACK && L->stacksize() > max)
	{
		int nsize = (inuse > LUAI_MAXSTACK / 2) ? LUAI_MAXSTACK : inuse * 2;
		luaD::reallocstack(L, nsize, 0); /* ok if that fails */
	}
	else /* don't change stack */
		condmovestack(L, {}, {}); /* (change only for debugging) */
	luaE_shrinkCI(L); /* shrink CI list */
}


void luaD::inctop(lua_State *L)
{
	luaD::checkstack(L, 1);
	L->top.p++;
}

/* }================================================================== */


/*
** Call a hook for the given event. Make sure there is a hook to be
** called. (Both 'L->hook' and 'L->hookmask', which trigger this
** function, can be changed asynchronously by signals.)
*/
void luaD::hook(lua_State *L, int event, int line,
					int ftransfer, int ntransfer)
{
	lua_Hook hook = L->hook;
	if (hook && L->allowhook)
	{
		/* make sure there is a hook */
		int mask = CIST_HOOKED;
		CallInfo *ci = L->ci;
		ptrdiff_t top = luaD::savestack(L, L->top.p); /* preserve original 'top' */
		ptrdiff_t ci_top = luaD::savestack(L, ci->top.p); /* idem for 'ci->top' */
		lua_Debug ar;
		ar.event = event;
		ar.currentline = line;
		ar.i_ci = ci;
		if (ntransfer != 0)
		{
			mask |= CIST_TRAN; /* 'ci' has transfer information */
			ci->u2.transferinfo.ftransfer = ftransfer;
			ci->u2.transferinfo.ntransfer = ntransfer;
		}
		if (ci->isLua() && L->top.p < ci->top.p)
			L->top.p = ci->top.p; /* protect entire activation register */
		luaD::checkstack(L, LUA_MINSTACK); /* ensure minimum stack size */
		if (ci->top.p < L->top.p + LUA_MINSTACK)
			ci->top.p = L->top.p + LUA_MINSTACK;
		L->allowhook = 0; /* cannot call hooks inside a hook */
		ci->callstatus |= mask;
		lua_unlock(L);
		(*hook)(L, &ar);
		lua_lock(L);
		lua_assert(!L->allowhook);
		L->allowhook = 1;
		ci->top.p = luaD::restorestack(L, ci_top);
		L->top.p = luaD::restorestack(L, top);
		ci->callstatus &= ~mask;
	}
}


/*
** Executes a call hook for Lua functions. This function is called
** whenever 'hookmask' is not zero, so it checks whether call hooks are
** active.
*/
void luaD::hookcall(lua_State *L, CallInfo *ci)
{
	L->oldpc = 0; /* set 'oldpc' for new function */
	if (L->hookmask & LUA_MASKCALL)
	{
		/* is call hook on? */
		int event = (ci->callstatus & CIST_TAIL)
							? LUA_HOOKTAILCALL
							: LUA_HOOKCALL;
		Proto *p = ci_func(ci)->p;
		ci->u.l.savedpc++; /* hooks assume 'pc' is already incremented */
		luaD::hook(L, event, -1, 1, p->numparams);
		ci->u.l.savedpc--; /* correct 'pc' */
	}
}


/*
** Executes a return hook for Lua and C functions and sets/corrects
** 'oldpc'. (Note that this correction is needed by the line hook, so it
** is done even when return hooks are off.)
*/
static void rethook(lua_State *L, CallInfo *ci, int nres)
{
	if (L->hookmask & LUA_MASKRET)
	{
		/* is return hook on? */
		StkId firstres = L->top.p - nres; /* index of first result */
		int delta = 0; /* correction for vararg functions */
		int ftransfer;
		if (ci->isLua())
		{
			Proto *p = ci_func(ci)->p;
			if (p->is_vararg)
				delta = ci->u.l.nextraargs + p->numparams + 1;
		}
		ci->func.p += delta; /* if vararg, back to virtual 'func' */
		ftransfer = cast(unsigned short, firstres - ci->func.p);
		luaD::hook(L, LUA_HOOKRET, -1, ftransfer, nres); /* call it */
		ci->func.p -= delta;
	}
	if ((ci = ci->previous)->isLua())
		L->oldpc = pcRel(ci->u.l.savedpc, ci_func(ci)->p); /* set 'oldpc' */
}


/*
** Check whether 'func' has a '__call' metafield. If so, put it in the
** stack, below original 'func', so that 'luaD::precall' can call it. Raise
** an error if there is no '__call' metafield.
*/
static StkId tryfuncTM(lua_State *L, StkId func)
{
	const TValue *tm;
	StkId p;
	luaD::checkstackGCp(L, 1, func); /* space for metamethod */
	tm = luaT_gettmbyobj(L, s2v(func), TM_CALL); /* (after previous GC) */
	if (l_unlikely(ttisnil(tm)))
		luaG_callerror(L, s2v(func)); /* nothing to call */
	for (p = L->top.p; p > func; p--) /* open space for metamethod */
		setobjs2s(L, p, p-1);
	L->top.p++; /* stack space pre-allocated by the caller */
	setobj2s(L, func, tm); /* metamethod is the new function to be called */
	return func;
}


/*
** Given 'nres' results at 'firstResult', move 'wanted' of them to 'res'.
** Handle most typical cases (zero results for commands, one result for
** expressions, multiple results for tail calls/single parameters)
** separated.
*/
l_sinline void moveresults(lua_State *L, StkId res, int nres, int wanted)
{
	StkId firstresult;
	int i;
	switch (wanted)
	{
		/* handle typical cases separately */
		case 0: /* no values needed */
			L->top.p = res;
			return;
		case 1: /* one value needed */
			if (nres == 0) /* no results? */
				setnilvalue(s2v(res)); /* adjust with nil */
			else /* at least one result */
				setobjs2s(L, res, L->top.p - nres); /* move it to proper place */
			L->top.p = res + 1;
			return;
		case LUA_MULTRET:
			wanted = nres; /* we want all results */
			break;
		default: /* two/more results and/or to-be-closed variables */
			if (hastocloseCfunc(wanted))
			{
				/* to-be-closed variables? */
				L->ci->callstatus |= CIST_CLSRET; /* in case of yields */
				L->ci->u2.nres = nres;
				res = luaF::close(L, res, CLOSEKTOP, 1);
				L->ci->callstatus &= ~CIST_CLSRET;
				if (L->hookmask)
				{
					/* if needed, call hook after '__close's */
					auto savedres = luaD::savestack(L, res);
					rethook(L, L->ci, nres);
					res = luaD::restorestack(L, savedres); /* hook can move stack */
				}
				wanted = decodeNresults(wanted);
				if (wanted == LUA_MULTRET)
					wanted = nres; /* we want all results */
			}
			break;
	}
	/* generic case */
	firstresult = L->top.p - nres; /* index of first result */
	if (nres > wanted) /* extra results? */
		nres = wanted; /* don't need them */
	for (i = 0; i < nres; i++) /* move all results to correct place */
		setobjs2s(L, res + i, firstresult + i);
	for (; i < wanted; i++) /* complete wanted number of results */
		setnilvalue(s2v(res + i));
	L->top.p = res + wanted; /* top points after the last result */
}


/*
** Finishes a function call: calls hook if necessary, moves current
** number of results to proper place, and returns to previous call
** info. If function has to close variables, hook must be called after
** that.
*/
void luaD::poscall(lua_State *L, CallInfo *ci, int nres)
{
	int wanted = ci->nresults;
	if (l_unlikely(L->hookmask && !hastocloseCfunc(wanted)))
		rethook(L, ci, nres);
	/* move results to proper place */
	moveresults(L, ci->func.p, nres, wanted);
	/* function cannot be in any of these cases when returning */
	lua_assert(!(ci->callstatus &
		(CIST_HOOKED | CIST_YPCALL | CIST_FIN | CIST_TRAN | CIST_CLSRET)));
	L->ci = ci->previous; /* back to caller (after closing variables) */
}


#define next_ci(L)  (L->ci->next ? L->ci->next : luaE_extendCI(L))


l_sinline CallInfo *prepCallInfo(lua_State *L, StkId func, int nret,
											int mask, StkId top)
{
	CallInfo *ci = L->ci = next_ci(L); /* new frame */
	ci->func.p = func;
	ci->nresults = nret;
	ci->callstatus = mask;
	ci->top.p = top;
	return ci;
}


/*
** precall for C functions
*/
l_sinline int precallC(lua_State *L, StkId func, int nresults,
								lua_CFunction f)
{
	int n; /* number of returns */
	CallInfo *ci;
	luaD::checkstackGCp(L, LUA_MINSTACK, func); /* ensure minimum stack size */
	L->ci = ci = prepCallInfo(L, func, nresults, CIST_C,
										L->top.p + LUA_MINSTACK);
	lua_assert(ci->top.p <= L->stack_last.p);
	if (l_unlikely(L->hookmask & LUA_MASKCALL))
	{
		int narg = cast_int(L->top.p - func) - 1;
		luaD::hook(L, LUA_HOOKCALL, -1, 1, narg);
	}
	lua_unlock(L);
	n = (*f)(L); /* do the actual call */
	lua_lock(L);
	api_checknelems(L, n);
	luaD::poscall(L, ci, n);
	return n;
}


/*
** Prepare a function for a tail call, building its call info on top
** of the current call info. 'narg1' is the number of arguments plus 1
** (so that it includes the function itself). Return the number of
** results, if it was a C function, or -1 for a Lua function.
*/
int luaD::pretailcall(lua_State *L, CallInfo *ci, StkId func,
							int narg1, int delta)
{
retry:
	switch (ttypetag(s2v(func)))
	{
		case LUA_VCCL: /* C closure */
			return precallC(L, func, LUA_MULTRET, clCvalue(s2v(func))->f);
		case LUA_VLCF: /* light C function */
			return precallC(L, func, LUA_MULTRET, fvalue(s2v(func)));
		case LUA_VLCL: {
			/* Lua function */
			Proto *p = clLvalue(s2v(func))->p;
			int fsize = p->maxstacksize; /* frame size */
			int nfixparams = p->numparams;
			int i;
			luaD::checkstackGCp(L, fsize - delta, func);
			ci->func.p -= delta; /* restore 'func' (if vararg) */
			for (i = 0; i < narg1; i++) /* move down function and arguments */
				setobjs2s(L, ci->func.p + i, func + i);
			func = ci->func.p; /* moved-down function */
			for (; narg1 <= nfixparams; narg1++)
				setnilvalue(s2v(func + narg1)); /* complete missing arguments */
			ci->top.p = func + 1 + fsize; /* top for new function */
			lua_assert(ci->top.p <= L->stack_last.p);
			ci->u.l.savedpc = p->code; /* starting point */
			ci->callstatus |= CIST_TAIL;
			L->top.p = func + narg1; /* set top */
			return -1;
		}
		default: {
			/* not a function */
			func = tryfuncTM(L, func); /* try to get '__call' metamethod */
			/* return luaD::pretailcall(L, ci, func, narg1 + 1, delta); */
			narg1++;
			goto retry; /* try again */
		}
	}
}


/*
** Prepares the call to a function (C or Lua). For C functions, also do
** the call. The function to be called is at '*func'.  The arguments
** are on the stack, right after the function.  Returns the CallInfo
** to be executed, if it was a Lua function. Otherwise (a C function)
** returns NULL, with all the results on the stack, starting at the
** original function position.
*/
CallInfo *luaD::precall(lua_State *L, StkId func, int nresults)
{
retry:
	switch (ttypetag(s2v(func)))
	{
		case LUA_VCCL: /* C closure */
			precallC(L, func, nresults, clCvalue(s2v(func))->f);
			return NULL;
		case LUA_VLCF: /* light C function */
			precallC(L, func, nresults, fvalue(s2v(func)));
			return NULL;
		case LUA_VLCL: {
			/* Lua function */
			CallInfo *ci;
			Proto *p = clLvalue(s2v(func))->p;
			int narg = cast_int(L->top.p - func) - 1; /* number of real arguments */
			int nfixparams = p->numparams;
			int fsize = p->maxstacksize; /* frame size */
			luaD::checkstackGCp(L, fsize, func);
			L->ci = ci = prepCallInfo(L, func, nresults, 0, func + 1 + fsize);
			ci->u.l.savedpc = p->code; /* starting point */
			for (; narg < nfixparams; narg++)
				setnilvalue(s2v(L->top.p++)); /* complete missing arguments */
			lua_assert(ci->top.p <= L->stack_last.p);
			return ci;
		}
		default: {
			/* not a function */
			func = tryfuncTM(L, func); /* try to get '__call' metamethod */
			/* return luaD::precall(L, func, nresults); */
			goto retry; /* try again with metamethod */
		}
	}
}


/*
** Call a function (C or Lua) through C. 'inc' can be 1 (increment
** number of recursive invocations in the C stack) or nyci (the same
** plus increment number of non-yieldable calls).
** This function can be called with some use of EXTRA_STACK, so it should
** check the stack before doing anything else. 'luaD::precall' already
** does that.
*/
l_sinline void ccall(lua_State *L, StkId func, int nResults, l_uint32 inc)
{
	CallInfo *ci;
	L->nCcalls += inc;
	if (l_unlikely(L->getCcalls() >= LUAI_MAXCCALLS))
	{
		luaD::checkstackp(L, 0, func); /* free any use of EXTRA_STACK */
		luaE_checkcstack(L);
	}
	if ((ci = luaD::precall(L, func, nResults)) != nullptr)
	{
		/* Lua function? */
		ci->callstatus = CIST_FRESH; /* mark that it is a "fresh" execute */
		luaV_execute(L, ci); /* call it */
	}
	L->nCcalls -= inc;
}


/*
** External interface for 'ccall'
*/
void luaD::call(lua_State *L, StkId func, int nResults)
{
	ccall(L, func, nResults, 1);
}


/*
** Similar to 'luaD::call', but does not allow yields during the call.
*/
void luaD::callnoyield(lua_State *L, StkId func, int nResults)
{
	ccall(L, func, nResults, nyci);
}


/*
** Finish the job of 'lua_pcallk' after it was interrupted by an yield.
** (The caller, 'finishCcall', does the final call to 'adjustresults'.)
** The main job is to complete the 'luaD::pcall' called by 'lua_pcallk'.
** If a '__close' method yields here, eventually control will be back
** to 'finishCcall' (when that '__close' method finally returns) and
** 'finishpcallk' will run again and close any still pending '__close'
** methods. Similarly, if a '__close' method errs, 'precover' calls
** 'unroll' which calls ''finishCcall' and we are back here again, to
** close any pending '__close' methods.
** Note that, up to the call to 'luaF::close', the corresponding
** 'CallInfo' is not modified, so that this repeated run works like the
** first one (except that it has at least one less '__close' to do). In
** particular, field CIST_RECST preserves the error status across these
** multiple runs, changing only if there is a new error.
*/
static int finishpcallk(lua_State *L, CallInfo *ci)
{
	int status = ci->getcistrecst(); /* get original status */
	if (l_likely(status == LUA_OK)) /* no error? */
		status = LUA_YIELD; /* was interrupted by an yield */
	else
	{
		/* error */
		StkId func = luaD::restorestack(L, ci->u2.funcidx);
		L->allowhook = ci->getoah(); /* restore 'allowhook' */
		func = luaF::close(L, func, status, 1); /* can yield or raise an error */
		luaD::seterrorobj(L, status, func);
		luaD::shrinkstack(L); /* restore stack size in case of overflow */
		ci->setcistrecst(LUA_OK); /* clear original status */
	}
	ci->callstatus &= ~CIST_YPCALL;
	L->errfunc = ci->u.c.old_errfunc;
	/* if it is here, there were errors or yields; unlike 'lua_pcallk',
		do not change status */
	return status;
}


/*
** Completes the execution of a C function interrupted by an yield.
** The interruption must have happened while the function was either
** closing its tbc variables in 'moveresults' or executing
** 'lua_callk'/'lua_pcallk'. In the first case, it just redoes
** 'luaD::poscall'. In the second case, the call to 'finishpcallk'
** finishes the interrupted execution of 'lua_pcallk'.  After that, it
** calls the continuation of the interrupted function and finally it
** completes the job of the 'luaD::call' that called the function.  In
** the call to 'adjustresults', we do not know the number of results
** of the function called by 'lua_callk'/'lua_pcallk', so we are
** conservative and use LUA_MULTRET (always adjust).
*/
static void finishCcall(lua_State *L, CallInfo *ci)
{
	int n; /* actual number of results from C function */
	if (ci->callstatus & CIST_CLSRET)
	{
		/* was returning? */
		lua_assert(hastocloseCfunc(ci->nresults));
		n = ci->u2.nres; /* just redo 'luaD::poscall' */
		/* don't need to reset CIST_CLSRET, as it will be set again anyway */
	}
	else
	{
		int status = LUA_YIELD; /* default if there were no errors */
		/* must have a continuation and must be able to call it */
		lua_assert(ci->u.c.k != NULL && yieldable(L));
		if (ci->callstatus & CIST_YPCALL) /* was inside a 'lua_pcallk'? */
			status = finishpcallk(L, ci); /* finish it */
		adjustresults(L, LUA_MULTRET); /* finish 'lua_callk' */
		lua_unlock(L);
		n = (*ci->u.c.k)(L, status, ci->u.c.ctx); /* call continuation */
		lua_lock(L);
		api_checknelems(L, n);
	}
	luaD::poscall(L, ci, n); /* finish 'luaD::call' */
}


/*
** Executes "full continuation" (everything in the stack) of a
** previously interrupted coroutine until the stack is empty (or another
** interruption long-jumps out of the loop).
*/
static void unroll(lua_State *L, void *ud)
{
	CallInfo *ci;
	UNUSED(ud);
	while ((ci = L->ci) != &L->base_ci)
	{
		/* something in the stack */
		if (!ci->isLua()) /* C function? */
			finishCcall(L, ci); /* complete its execution */
		else
		{
			/* Lua function */
			luaV_finishOp(L); /* finish interrupted instruction */
			luaV_execute(L, ci); /* execute down to higher C 'boundary' */
		}
	}
}


/*
** Try to find a suspended protected call (a "recover point") for the
** given thread.
*/
static CallInfo *findpcall(lua_State *L)
{
	CallInfo *ci;
	for (ci = L->ci; ci != NULL; ci = ci->previous)
	{
		/* search for a pcall */
		if (ci->callstatus & CIST_YPCALL)
			return ci;
	}
	return NULL; /* no pending pcall */
}


/*
** Signal an error in the call to 'lua_resume', not in the execution
** of the coroutine itself. (Such errors should not be handled by any
** coroutine error handler and should not kill the coroutine.)
*/
static int resume_error(lua_State *L, const char *msg, int narg)
{
	L->top.p -= narg; /* remove args from the stack */
	setsvalue2s(L, L->top.p, luaS::news(L, msg)); /* push error message */
	api_incr_top(L);
	lua_unlock(L);
	return LUA_ERRRUN;
}


/*
** Do the work for 'lua_resume' in protected mode. Most of the work
** depends on the status of the coroutine: initial state, suspended
** inside a hook, or regularly suspended (optionally with a continuation
** function), plus erroneous cases: non-suspended coroutine or dead
** coroutine.
*/
static void resume(lua_State *L, void *ud)
{
	int n = *(cast(int*, ud)); /* number of arguments */
	StkId firstArg = L->top.p - n; /* first argument */
	CallInfo *ci = L->ci;
	if (L->status == LUA_OK) /* starting a coroutine? */
		ccall(L, firstArg - 1, LUA_MULTRET, 0); /* just call its body */
	else
	{
		/* resuming from previous yield */
		lua_assert(L->status == LUA_YIELD);
		L->status = LUA_OK; /* mark that it is running (again) */
		if (ci->isLua())
		{
			/* yielded inside a hook? */
			/* undo increment made by 'luaG_traceexec': instruction was not
				executed yet */
			lua_assert(ci->callstatus & CIST_HOOKYIELD);
			ci->u.l.savedpc--;
			L->top.p = firstArg; /* discard arguments */
			luaV_execute(L, ci); /* just continue running Lua code */
		}
		else
		{
			/* 'common' yield */
			if (ci->u.c.k != NULL)
			{
				/* does it have a continuation function? */
				lua_unlock(L);
				n = (*ci->u.c.k)(L, LUA_YIELD, ci->u.c.ctx); /* call continuation */
				lua_lock(L);
				api_checknelems(L, n);
			}
			luaD::poscall(L, ci, n); /* finish 'luaD::call' */
		}
		unroll(L, NULL); /* run continuation */
	}
}


/*
** Unrolls a coroutine in protected mode while there are recoverable
** errors, that is, errors inside a protected call. (Any error
** interrupts 'unroll', and this loop protects it again so it can
** continue.) Stops with a normal end (status == LUA_OK), an yield
** (status == LUA_YIELD), or an unprotected error ('findpcall' doesn't
** find a recover point).
*/
static int precover(lua_State *L, int status)
{
	CallInfo *ci;
	while (errorstatus(status) && (ci = findpcall(L)) != NULL)
	{
		L->ci = ci; /* go down to recovery functions */
		ci->setcistrecst(status); /* status to finish 'pcall' */
		status = luaD::rawrunprotected(L, unroll, NULL);
	}
	return status;
}


LUA_API int lua_resume(lua_State *L, lua_State *from, int nargs,
								int *nresults)
{
	int status;
	lua_lock(L);
	if (L->status == LUA_OK)
	{
		/* may be starting a coroutine */
		if (L->ci != &L->base_ci) /* not in base level? */
			return resume_error(L, "cannot resume non-suspended coroutine", nargs);
		else if (L->top.p - (L->ci->func.p + 1) == nargs) /* no function? */
			return resume_error(L, "cannot resume dead coroutine", nargs);
	}
	else if (L->status != LUA_YIELD) /* ended with errors? */
		return resume_error(L, "cannot resume dead coroutine", nargs);
	L->nCcalls = (from) ? from->getCcalls() : 0;
	if (L->getCcalls() >= LUAI_MAXCCALLS)
		return resume_error(L, "C stack overflow", nargs);
	L->nCcalls++;
	luai_userstateresume(L, nargs);
	api_checknelems(L, (L->status == LUA_OK) ? nargs + 1 : nargs);
	status = luaD::rawrunprotected(L, resume, &nargs);
	/* continue running after recoverable errors */
	status = precover(L, status);
	if (l_likely(!errorstatus(status)))
		lua_assert(status == L->status); /* normal end or yield */
	else
	{
		/* unrecoverable error */
		L->status = cast_byte(status); /* mark thread as 'dead' */
		luaD::seterrorobj(L, status, L->top.p); /* push error message */
		L->ci->top.p = L->top.p;
	}
	*nresults = (status == LUA_YIELD)
						? L->ci->u2.nyield
						: cast_int(L->top.p - (L->ci->func.p + 1));
	lua_unlock(L);
	return status;
}


LUA_API int lua_isyieldable(lua_State *L)
{
	return L->yieldable();
}


LUA_API int lua_yieldk(lua_State *L, int nresults, lua_KContext ctx,
								lua_KFunction k)
{
	CallInfo *ci;
	luai_userstateyield(L, nresults);
	lua_lock(L);
	ci = L->ci;
	api_checknelems(L, nresults);
	if (l_unlikely(!L->yieldable()))
	{
		if (L != G(L)->mainthread)
			luaG_runerror(L, "attempt to yield across a C-call boundary");
		else
			luaG_runerror(L, "attempt to yield from outside a coroutine");
	}
	L->status = LUA_YIELD;
	ci->u2.nyield = nresults; /* save number of results */
	if (ci->isLua())
	{
		/* inside a hook? */
		lua_assert(!isLuacode(ci));
		api_check(L, nresults == 0, "hooks cannot yield values");
		api_check(L, k == NULL, "hooks cannot continue after yielding");
	}
	else
	{
		if ((ci->u.c.k = k) != NULL) /* is there a continuation? */
			ci->u.c.ctx = ctx; /* save context */
		luaD::lthrow(L, LUA_YIELD);
	}
	lua_assert(ci->callstatus & CIST_HOOKED); /* must be inside a hook */
	lua_unlock(L);
	return 0; /* return to 'luaD::hook' */
}


/*
** Auxiliary structure to call 'luaF::close' in protected mode.
*/
struct CloseP
{
	StkId level;
	int status;
};


/*
** Auxiliary function to call 'luaF::close' in protected mode.
*/
static void closepaux(lua_State *L, void *ud)
{
	struct CloseP *pcl = cast(struct CloseP *, ud);
	luaF::close(L, pcl->level, pcl->status, 0);
}


/*
** Calls 'luaF::close' in protected mode. Return the original status
** or, in case of errors, the new status.
*/
int luaD::closeprotected(lua_State *L, ptrdiff_t level, int status)
{
	CallInfo *old_ci = L->ci;
	lu_byte old_allowhooks = L->allowhook;
	for (;;)
	{
		/* keep closing upvalues until no more errors */
		struct CloseP pcl;
		pcl.level = luaD::restorestack(L, level);
		pcl.status = status;
		status = luaD::rawrunprotected(L, &closepaux, &pcl);
		if (l_likely(status == LUA_OK)) /* no more errors? */
			return pcl.status;
		else
		{
			/* an error occurred; restore saved state and repeat */
			L->ci = old_ci;
			L->allowhook = old_allowhooks;
		}
	}
}


/*
** Call the C function 'func' in protected mode, restoring basic
** thread information ('allowhook', etc.) and in particular
** its stack level in case of errors.
*/
int luaD::pcall(lua_State *L, luaD::Pfunc func, void *u,
					ptrdiff_t old_top, ptrdiff_t ef)
{
	int status;
	CallInfo *old_ci = L->ci;
	lu_byte old_allowhooks = L->allowhook;
	ptrdiff_t old_errfunc = L->errfunc;
	L->errfunc = ef;
	status = luaD::rawrunprotected(L, func, u);
	if (l_unlikely(status != LUA_OK))
	{
		/* an error occurred? */
		L->ci = old_ci;
		L->allowhook = old_allowhooks;
		status = luaD::closeprotected(L, old_top, status);
		luaD::seterrorobj(L, status, luaD::restorestack(L, old_top));
		luaD::shrinkstack(L); /* restore stack size in case of overflow */
	}
	L->errfunc = old_errfunc;
	return status;
}


/*
** Execute a protected parser.
*/
struct SParser
{
	/* data to 'f_parser' */
	ZIO *z;
	Mbuffer buff; /* dynamic structure used by the scanner */
	Dyndata dyd; /* dynamic structures used by the parser */
	const char *mode;
	const char *name;
};


static void checkmode(lua_State *L, const char *mode, const char *x)
{
	if (mode && strchr(mode, x[0]) == NULL)
	{
		luaO_pushfstring(L, "attempt to load a %s chunk (mode is '%s')", x, mode);
		luaD::lthrow(L, LUA_ERRSYNTAX);
	}
}


static void f_parser(lua_State *L, void *ud)
{
	LClosure *cl;
	struct SParser *p = cast(struct SParser *, ud);
	int c = zgetc(p->z); /* read first character */
	if (c == LUA_SIGNATURE[0])
	{
		checkmode(L, p->mode, "binary");
		cl = luaU::undump(L, p->z, p->name);
	}
	else
	{
		checkmode(L, p->mode, "text");
		cl = luaY_parser(L, p->z, &p->buff, &p->dyd, p->name, c);
	}
	lua_assert(cl->nupvalues == cl->p->sizeupvalues);
	luaF::initupvals(L, cl);
}


int luaD::protectedparser(lua_State *L, ZIO *z, const char *name,
								const char *mode)
{
	struct SParser p;
	int status;
	L->incnny(); /* cannot yield during parsing */
	p.z = z;
	p.name = name;
	p.mode = mode;
	p.dyd.actvar.arr = NULL;
	p.dyd.actvar.size = 0;
	p.dyd.gt.arr = NULL;
	p.dyd.gt.size = 0;
	p.dyd.label.arr = NULL;
	p.dyd.label.size = 0;
	p.buff.initbuffer(L);
	// luaZ_initbuffer(L, &p.buff);
	status = luaD::pcall(L, f_parser, &p, luaD::savestack(L, L->top.p), L->errfunc);
	p.buff.freebuffer(L);
	// luaZ_freebuffer(L, &p.buff);
	luaM::freearray(L, p.dyd.actvar.arr, p.dyd.actvar.size);
	luaM::freearray(L, p.dyd.gt.arr, p.dyd.gt.size);
	luaM::freearray(L, p.dyd.label.arr, p.dyd.label.size);
	L->decnny();
	return status;
}


