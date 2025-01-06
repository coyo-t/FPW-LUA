/*
** $Id: lmem.h $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#ifndef lmem_h
#define lmem_h


#include <stddef.h>

#include "llimits.hpp"
#include "lua.hpp"

namespace luaM {

LUAI_FUNC l_noret toobig(lua_State *L);


/* not to be called directly */
LUAI_FUNC void *realloc_(lua_State *L, void *block, size_t oldsize,
										size_t size);

LUAI_FUNC void *saferealloc_(lua_State *L, void *block, size_t oldsize,
											size_t size);

LUAI_FUNC void free_(lua_State *L, void *block, size_t osize);

LUAI_FUNC void *growaux_(lua_State *L, void *block, int nelems,
										int *size, int size_elem, int limit,
										const char *what);

LUAI_FUNC void *shrinkvector_(lua_State *L, void *block, int *nelem,
												int final_n, int size_elem);

LUAI_FUNC void *malloc_(lua_State *L, size_t size, int tag);

/*
** This macro tests whether it is safe to multiply 'n' by the size of
** type 't' without overflows. Because 'e' is always constant, it avoids
** the runtime division MAX_SIZET/(e).
** (The macro is somewhat complex to avoid warnings:  The 'sizeof'
** comparison avoids a runtime comparison when overflow cannot occur.
** The compiler should be able to optimize the real test by itself, but
** when it does it, it may give a warning about "comparison is always
** false due to limited range of data type"; the +1 tricks the compiler,
** avoiding this warning but also this optimization.)
*/
// #define luaM::luaM_testsize(n,e) (sizeof(n) >= sizeof(size_t) && cast_sizet((n)) + 1 > MAX_SIZET/(e))
bool testsize (int n, size_t e);

/*
** Computes the minimum between 'n' and 'MAX_SIZET/sizeof(t)', so that
** the result is not larger than 'n' and cannot overflow a 'size_t'
** when multiplied by the size of type 't'. (Assumes that 'n' is an
** 'int' or 'unsigned int' and that 'int' is not larger than 'size_t'.)
*/
template<typename T>
size_t limitN (size_t n)
{
	return (cast_sizet(n) <= MAX_SIZET/sizeof(T))
		? n
		: cast_uint(MAX_SIZET/sizeof(T));
}


//

/*
** Arrays of chars do not need any test
*/
template<typename T>
T* reallocvchar (lua_State* L, T* b, size_t on, size_t n)
{
	return static_cast<T*>(luaM::saferealloc_(L, (b), (on)*sizeof(T), (n)*sizeof(T)));
}

template<typename T>
void freemem (lua_State* L, T* b, size_t s)
{
	luaM::free_(L, b, s);
}

template<typename T>
void free (lua_State* L, T* b)
{
	luaM::free_(L, b, sizeof(*b));
}

template<typename T>
void freearray (lua_State* L, T* b, size_t n)
{
	luaM::free_(L, b, n*sizeof(*b));
}

//

//
template<typename T>
T* newmem (lua_State* L)
{
	return static_cast<T*>(luaM::malloc_(L, sizeof(T), 0));
}

template<typename T>
T* newvector (lua_State* L, int n)
{
	return static_cast<T*>(luaM::malloc_(L, (n)*sizeof(T), 0));
}


template<typename T>
T* newvectorchecked (lua_State* L, int n)
{
	if (luaM::testsize(n, sizeof(T)))
	{
		luaM::toobig(L);
	}
	return luaM::newvector<T>(L,n);
}


//

template<typename T, typename Tag>
T* newobject (lua_State* L, Tag tag, size_t s)
{
	return static_cast<T *>(luaM::malloc_(L, (s), tag));
}

template<typename T, typename S>
T* growvector (lua_State* L, T* v, size_t nelems, S* size, size_t limit, const char* e)
{
	return static_cast<T*>(luaM::growaux_(L,v,nelems, size, sizeof(T), luaM::limitN<T>(limit), e));
}

template<typename T>
T* reallocvector (lua_State* L, T* v, size_t oldn, size_t n)
{
	return static_cast<T*>(luaM::realloc_(L, v, cast_sizet(oldn) * sizeof(T), cast_sizet(n) * sizeof(T)));
}

template<typename T, typename S>
T* shrinkvector (lua_State* L, T* v, S* size, size_t fs)
{
	return static_cast<T*>(luaM::shrinkvector_(L, v, size, fs, sizeof(T)));
}
}




#endif
