/*
** $Id: lundump.c $
** load precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#define lundump_c
#define LUA_CORE

#include "lprefix.hpp"


#include <climits>
#include <cstring>

#include "lua.hpp"

#include "ldebug.hpp"
#include "ldo.hpp"
#include "lfunc.hpp"
#include "lmem.hpp"
#include "lobject.hpp"
#include "lstring.hpp"
#include "lundump.hpp"
#include "lzio.hpp"


#if !defined(luai_verifycode)
#define luai_verifycode(L,f)  /* empty */
#endif


struct LoadState
{
	lua_State *L;
	ZIO *Z;
	const char *name;
};


static l_noret error(LoadState *S, const char *why)
{
	luaO_pushfstring(S->L, "%s: bad binary format (%s)", S->name, why);
	luaD::lthrow(S->L, LUA_ERRSYNTAX);
}


/*
** All high-level loads go through loadVector; you can change it to
** adapt to the endianness of the input
*/
// #define loadVector(S,b,n)	loadBlock(S,b,(n)*sizeof((b)[0]))
template<typename T>
static void loadVector (LoadState *s, T* b, size_t size)
{
	if (s->Z->read(b, size * sizeof(T)) != 0)
		error(s, "truncated chunk");
	// loadBlock(s, b, size * sizeof(T));
}

// #define loadVar(S,x)		loadVector(S,&x,1)
template<typename T>
static void loadVar (LoadState* s, T*x)
{
	loadVector(s, x, 1);
}

static lu_byte loadByte(LoadState *S)
{
	int b = S->Z->zgetc();
	if (b == EOZ)
		error(S, "truncated chunk");
	return cast_byte(b);
}


static size_t loadUnsigned(LoadState *S, size_t limit)
{
	size_t x = 0;
	int b;
	limit >>= 7;
	do
	{
		b = loadByte(S);
		if (x >= limit)
			error(S, "integer overflow");
		x = (x << 7) | (b & 0x7f);
	} while ((b & 0x80) == 0);
	return x;
}


static size_t loadSize(LoadState *S)
{
	return loadUnsigned(S, MAX_SIZET);
}


static int loadInt(LoadState *S)
{
	return cast_int(loadUnsigned(S, INT_MAX));
}


static lua_Number loadNumber(LoadState *S)
{
	lua_Number x;
	loadVar(S, &x);
	return x;
}


static lua_Integer loadInteger(LoadState *S)
{
	lua_Integer x;
	loadVar(S, &x);
	return x;
}


/*
** Load a nullable string into prototype 'p'.
*/
static TString *loadStringN(LoadState *S, Proto *p)
{
	lua_State *L = S->L;
	TString *ts;
	size_t size = loadSize(S);
	if (size == 0) /* no string? */
		return NULL;
	if (--size <= LUAI_MAXSHORTLEN)
	{
		/* short string? */
		char buff[LUAI_MAXSHORTLEN];
		loadVector(S, buff, size); /* load string into buffer */
		ts = luaS::newlstr(L, buff, size); /* create string */
	}
	else
	{
		/* long string */
		ts = luaS::createlngstrobj(L, size); /* create string */
		setsvalue2s(L, L->top.p, ts); /* anchor it ('loadVector' can GC) */
		luaD::inctop(L);
		loadVector(S, getlngstr(ts), size); /* load directly in final place */
		L->top.p--; /* pop string */
	}
	luaC_objbarrier(L, p, ts);
	return ts;
}


/*
** Load a non-nullable string into prototype 'p'.
*/
static TString *loadString(LoadState *S, Proto *p)
{
	TString *st = loadStringN(S, p);
	if (st == NULL)
		error(S, "bad format for constant string");
	return st;
}


static void loadCode(LoadState *S, Proto *f)
{
	int n = loadInt(S);
	f->code = luaM::newvectorchecked<Instruction>(S->L, n);
	f->sizecode = n;
	loadVector(S, f->code, n);
}


static void loadFunction(LoadState *S, Proto *f, TString *psource);


static void loadConstants(LoadState *S, Proto *f)
{
	int i;
	int n = loadInt(S);
	f->k = luaM::newvectorchecked<TValue>(S->L, n);
	f->sizek = n;
	for (i = 0; i < n; i++)
		setnilvalue(&f->k[i]);
	for (i = 0; i < n; i++)
	{
		TValue *o = &f->k[i];
		int t = loadByte(S);
		switch (t)
		{
			case LUA_VNIL:
				setnilvalue(o);
				break;
			case LUA_VFALSE:
				setbfvalue(o);
				break;
			case LUA_VTRUE:
				setbtvalue(o);
				break;
			case LUA_VNUMFLT:
				setfltvalue(o, loadNumber(S));
				break;
			case LUA_VNUMINT:
				setivalue(o, loadInteger(S));
				break;
			case LUA_VSHRSTR:
			case LUA_VLNGSTR:
				setsvalue2n(S->L, o, loadString(S, f));
				break;
			default: lua_assert(0);
		}
	}
}


static void loadProtos(LoadState *S, Proto *f)
{
	int i;
	int n = loadInt(S);
	f->p = luaM::newvectorchecked<Proto*>(S->L, n);
	f->sizep = n;
	for (i = 0; i < n; i++)
		f->p[i] = nullptr;
	for (i = 0; i < n; i++)
	{
		f->p[i] = luaF::newproto(S->L);
		luaC_objbarrier(S->L, f, f->p[i]);
		loadFunction(S, f->p[i], f->source);
	}
}


/*
** Load the upvalues for a function. The names must be filled first,
** because the filling of the other fields can raise read errors and
** the creation of the error message can call an emergency collection;
** in that case all prototypes must be consistent for the GC.
*/
static void loadUpvalues(LoadState *S, Proto *f)
{
	int i, n;
	n = loadInt(S);
	f->upvalues = luaM::newvectorchecked<Upvaldesc>(S->L, n);
	f->sizeupvalues = n;
	for (i = 0; i < n; i++) /* make array valid for GC */
		f->upvalues[i].name = nullptr;

	for (i = 0; i < n; i++)
	{
		auto upv = f->upvalues[i];
		/* following calls can raise errors */
		upv.instack = loadByte(S);
		upv.idx = loadByte(S);
		upv.kind = loadByte(S);
	}
}


static void loadDebug(LoadState *S, Proto *f)
{
	int i, n;
	n = loadInt(S);
	f->lineinfo = luaM::newvectorchecked<ls_byte>(S->L, n);
	f->sizelineinfo = n;
	loadVector(S, f->lineinfo, n);
	n = loadInt(S);
	f->abslineinfo = luaM::newvectorchecked<AbsLineInfo>(S->L, n);
	f->sizeabslineinfo = n;
	for (i = 0; i < n; i++)
	{
		f->abslineinfo[i].pc = loadInt(S);
		f->abslineinfo[i].line = loadInt(S);
	}
	n = loadInt(S);
	f->locvars = luaM::newvectorchecked<LocVar>(S->L, n);
	f->sizelocvars = n;
	for (i = 0; i < n; i++)
		f->locvars[i].varname = nullptr;
	for (i = 0; i < n; i++)
	{
		f->locvars[i].varname = loadStringN(S, f);
		f->locvars[i].startpc = loadInt(S);
		f->locvars[i].endpc = loadInt(S);
	}
	n = loadInt(S);
	if (n != 0) /* does it have debug information? */
		n = f->sizeupvalues; /* must be this many */
	for (i = 0; i < n; i++)
		f->upvalues[i].name = loadStringN(S, f);
}


static void loadFunction(LoadState *S, Proto *f, TString *psource)
{
	f->source = loadStringN(S, f);
	if (f->source == NULL) /* no source in dump? */
		f->source = psource; /* reuse parent's source */
	f->linedefined = loadInt(S);
	f->lastlinedefined = loadInt(S);
	f->numparams = loadByte(S);
	f->is_vararg = loadByte(S);
	f->maxstacksize = loadByte(S);
	loadCode(S, f);
	loadConstants(S, f);
	loadUpvalues(S, f);
	loadProtos(S, f);
	loadDebug(S, f);
}


static void checkliteral(LoadState *S, const char *s, const char *msg)
{
	char buff[sizeof(LUA_SIGNATURE) + sizeof(LUAC_DATA)]; /* larger than both */
	size_t len = strlen(s);
	loadVector(S, buff, len);
	if (memcmp(s, buff, len) != 0)
		error(S, msg);
}


static void fchecksize(LoadState *S, size_t size, const char *tname)
{
	if (loadByte(S) != size)
		error(S, luaO_pushfstring(S->L, "%s size mismatch", tname));
}


// #define checksize(S,t)	fchecksize(S,sizeof(t),#t)
template<typename T>
void checksize(LoadState* s)
{
	fchecksize(s, sizeof(T), typeid(T).name());
}

static void checkHeader(LoadState *S)
{
	/* skip 1st char (already read and checked) */
	checkliteral(S, &LUA_SIGNATURE[1], "not a binary chunk");
	if (loadByte(S) != LUAC_VERSION)
		error(S, "version mismatch");
	if (loadByte(S) != LUAC_FORMAT)
		error(S, "format mismatch");
	checkliteral(S, LUAC_DATA, "corrupted chunk");
	checksize<Instruction>(S);
	checksize<lua_Integer>(S);
	checksize<lua_Number>(S);
	if (loadInteger(S) != LUAC_INT)
		error(S, "integer format mismatch");
	if (loadNumber(S) != LUAC_NUM)
		error(S, "float format mismatch");
}


/*
** Load precompiled chunk.
*/
LClosure *luaU::undump(lua_State *L, ZIO *Z, const char *name)
{
	LoadState S;
	if (*name == '@' || *name == '=')
		S.name = name + 1;
	else if (*name == LUA_SIGNATURE[0])
		S.name = "binary string";
	else
		S.name = name;
	S.L = L;
	S.Z = Z;
	checkHeader(&S);
	LClosure *cl = luaF::newLclosure(L, loadByte(&S));
	setclLvalue2s(L, L->top.p, cl);
	luaD::inctop(L);
	cl->p = luaF::newproto(L);
	luaC_objbarrier(L, cl, cl->p);
	loadFunction(&S, cl->p, NULL);
	lua_assert(cl->nupvalues == cl->p->sizeupvalues);
	luai_verifycode(L, cl->p);
	return cl;
}
