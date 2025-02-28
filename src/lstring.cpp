/*
** $Id: lstring.c $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#define lstring_c
#define LUA_CORE

#include "lprefix.hpp"


#include <string.h>

#include "lua.hpp"

#include "ldebug.hpp"
#include "ldo.hpp"
#include "lmem.hpp"
#include "lobject.hpp"
#include "lstate.hpp"
#include "lstring.hpp"


/*
** Maximum size for string table.
*/
#define MAXSTRTB	cast_int(luaM::limitN<TString*>(MAX_INT))


/*
** equality for long strings
*/
int luaS::eqlngstr(TString *a, TString *b)
{
	size_t len = a->u.lnglen;
	lua_assert(a->tt == LUA_VLNGSTR && b->tt == LUA_VLNGSTR);
	return (a == b) || /* same instance or... */
			((len == b->u.lnglen) && /* equal length and ... */
			(memcmp(getlngstr(a), getlngstr(b), len) == 0)); /* equal contents */
}


unsigned int luaS::hash(const char *str, size_t l, unsigned int seed)
{
	unsigned int h = seed ^ cast_uint(l);
	for (; l > 0; l--)
		h ^= ((h << 5) + (h >> 2) + cast_byte(str[l - 1]));
	return h;
}


unsigned int luaS::hashlongstr(TString *ts)
{
	lua_assert(ts->tt == LUA_VLNGSTR);
	if (ts->extra == 0)
	{
		/* no hash? */
		size_t len = ts->u.lnglen;
		ts->hash = luaS::hash(getlngstr(ts), len, ts->hash);
		ts->extra = 1; /* now it has its hash */
	}
	return ts->hash;
}


static void tablerehash(TString **vect, int osize, int nsize)
{
	int i;
	for (i = osize; i < nsize; i++) /* clear new elements */
		vect[i] = NULL;
	for (i = 0; i < osize; i++)
	{
		/* rehash old part of the array */
		TString *p = vect[i];
		vect[i] = NULL;
		while (p)
		{
			/* for each string in the list */
			TString *hnext = p->u.hnext; /* save next */
			unsigned int h = lmod(p->hash, nsize); /* new position */
			p->u.hnext = vect[h]; /* chain it into array */
			vect[h] = p;
			p = hnext;
		}
	}
}


/*
** Resize the string table. If allocation fails, keep the current size.
** (This can degrade performance, but any non-zero size should work
** correctly.)
*/
void luaS::resize(lua_State *L, int nsize)
{
	stringtable *tb = &G(L)->strt;
	int osize = tb->size;
	if (nsize < osize) /* shrinking table? */
		tablerehash(tb->hash, osize, nsize); /* depopulate shrinking part */
	TString **newvect = luaM::reallocvector(L, tb->hash, osize, nsize);
	if (l_unlikely(newvect == NULL))
	{
		/* reallocation failed? */
		if (nsize < osize) /* was it shrinking table? */
			tablerehash(tb->hash, nsize, osize); /* restore to original size */
		/* leave table as it was */
	}
	else
	{
		/* allocation succeeded */
		tb->hash = newvect;
		tb->size = nsize;
		if (nsize > osize)
			tablerehash(newvect, osize, nsize); /* rehash for new size */
	}
}


/*
** Clear API string cache. (Entries cannot be empty, so fill them with
** a non-collectable string.)
*/
void luaS::clearcache(global_State *g)
{
	int i, j;
	for (i = 0; i < STRCACHE_N; i++)
		for (j = 0; j < STRCACHE_M; j++)
		{
			if (iswhite(g->strcache[i][j])) /* will entry be collected? */
				g->strcache[i][j] = g->memerrmsg; /* replace it with something fixed */
		}
}


/*
** Initialize the string table and the string cache
*/
void luaS::init(lua_State *L)
{
	global_State *g = G(L);
	int i, j;
	stringtable *tb = &G(L)->strt;
	tb->hash = luaM::newvector<TString*>(L, MINSTRTABSIZE);
	tablerehash(tb->hash, 0, MINSTRTABSIZE); /* clear array */
	tb->size = MINSTRTABSIZE;
	/* pre-create memory-error message */
	g->memerrmsg = luaS::newliteral(L, MEMERRMSG);
	luaC_fix(L, obj2gco(g->memerrmsg)); /* it should never be collected */
	for (i = 0; i < STRCACHE_N; i++) /* fill cache with valid strings */
		for (j = 0; j < STRCACHE_M; j++)
			g->strcache[i][j] = g->memerrmsg;
}


/*
** creates a new string object
*/
static TString *createstrobj(lua_State *L, size_t l, int tag, unsigned int h)
{
	/* total size of TString object */
	size_t totalsize = TString::sizel(l);
	GCObject *o = luaC_newobj(L, tag, totalsize);
	TString *ts = gco2ts(o);
	ts->hash = h;
	ts->extra = 0;
	getstr(ts)[l] = '\0'; /* ending 0 */
	return ts;
}


TString *luaS::createlngstrobj(lua_State *L, size_t l)
{
	TString *ts = createstrobj(L, l, LUA_VLNGSTR, G(L)->seed);
	ts->u.lnglen = l;
	ts->shrlen = 0xFF; /* signals that it is a long string */
	return ts;
}


void luaS::remove(lua_State *L, TString *ts)
{
	stringtable *tb = &G(L)->strt;
	TString **p = &tb->hash[lmod(ts->hash, tb->size)];
	while (*p != ts) /* find previous element */
		p = &(*p)->u.hnext;
	*p = (*p)->u.hnext; /* remove element from its list */
	tb->nuse--;
}


static void growstrtab(lua_State *L, stringtable *tb)
{
	if (l_unlikely(tb->nuse == MAX_INT))
	{
		/* too many strings? */
		luaC_fullgc(L, 1); /* try to free some... */
		if (tb->nuse == MAX_INT) /* still too many? */
			luaD::lthrow(L, LUA_ERRMEM); /* cannot even create a message... */
	}
	if (tb->size <= MAXSTRTB / 2) /* can grow string table? */
		luaS::resize(L, tb->size * 2);
}


/*
** Checks whether short string exists and reuses it or creates a new one.
*/
static TString *internshrstr(lua_State *L, const char *str, size_t l)
{
	TString *ts;
	global_State *g = G(L);
	stringtable *tb = &g->strt;
	unsigned int h = luaS::hash(str, l, g->seed);
	TString **list = &tb->hash[lmod(h, tb->size)];
	lua_assert(str != NULL); /* otherwise 'memcmp'/'memcpy' are undefined */
	for (ts = *list; ts != NULL; ts = ts->u.hnext)
	{
		if (l == ts->shrlen && (memcmp(str, getshrstr(ts), l * sizeof(char)) == 0))
		{
			/* found! */
			if (isdead(g, ts)) /* dead (but not collected yet)? */
				changewhite(ts); /* resurrect it */
			return ts;
		}
	}
	/* else must create a new string */
	if (tb->nuse >= tb->size)
	{
		/* need to grow string table? */
		growstrtab(L, tb);
		list = &tb->hash[lmod(h, tb->size)]; /* rehash with new size */
	}
	ts = createstrobj(L, l, LUA_VSHRSTR, h);
	ts->shrlen = cast_byte(l);
	memcpy(getshrstr(ts), str, l * sizeof(char));
	ts->u.hnext = *list;
	*list = ts;
	tb->nuse++;
	return ts;
}


/*
** new string (with explicit length)
*/
TString *luaS::newlstr(lua_State *L, const char *str, size_t l)
{
	if (l <= LUAI_MAXSHORTLEN) /* short string? */
		return internshrstr(L, str, l);
	TString *ts;
	if (l_unlikely(l * sizeof(char) >= (MAX_SIZE - sizeof(TString))))
		luaM::toobig(L);
	ts = luaS::createlngstrobj(L, l);
	memcpy(getlngstr(ts), str, l * sizeof(char));
	return ts;
}


/*
** Create or reuse a zero-terminated string, first checking in the
** cache (using the string address as a key). The cache can contain
** only zero-terminated strings, so it is safe to use 'strcmp' to
** check hits.
*/
TString *luaS::news(lua_State *L, const char *str)
{
	unsigned int i = point2uint(str) % STRCACHE_N; /* hash */
	int j;
	TString **p = G(L)->strcache[i];
	for (j = 0; j < STRCACHE_M; j++)
	{
		if (strcmp(str, getstr(p[j])) == 0) /* hit? */
			return p[j]; /* that is it */
	}
	/* normal route */
	for (j = STRCACHE_M - 1; j > 0; j--)
		p[j] = p[j - 1]; /* move out last element */
	/* new element is first in the list */
	p[0] = luaS::newlstr(L, str, strlen(str));
	return p[0];
}


Udata *luaS::newudata(lua_State *L, size_t s, int nuvalue)
{
	Udata *u;
	int i;
	GCObject *o;
	if (l_unlikely(s > MAX_SIZE - udatamemoffset(nuvalue)))
		luaM::toobig(L);
	o = luaC_newobj(L, LUA_VUSERDATA, sizeudata(nuvalue, s));
	u = gco2u(o);
	u->len = s;
	u->nuvalue = nuvalue;
	u->metatable = NULL;
	for (i = 0; i < nuvalue; i++)
		setnilvalue(&u->uv[i].uv);
	return u;
}
