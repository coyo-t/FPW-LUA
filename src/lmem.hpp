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


//
LUAI_FUNC l_noret luaM_toobig(lua_State *L);

/* not to be called directly */
LUAI_FUNC void *luaM_realloc_(lua_State *L, void *block, size_t oldsize,
										size_t size);

LUAI_FUNC void *luaM_saferealloc_(lua_State *L, void *block, size_t oldsize,
											size_t size);

LUAI_FUNC void luaM_free_(lua_State *L, void *block, size_t osize);

LUAI_FUNC void *luaM_growaux_(lua_State *L, void *block, int nelems,
										int *size, int size_elem, int limit,
										const char *what);

LUAI_FUNC void *luaM_shrinkvector_(lua_State *L, void *block, int *nelem,
												int final_n, int size_elem);

LUAI_FUNC void *luaM_malloc_(lua_State *L, size_t size, int tag);
//

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
// #define luaM_testsize(n,e) (sizeof(n) >= sizeof(size_t) && cast_sizet((n)) + 1 > MAX_SIZET/(e))
bool luaM_testsize (int n, size_t e);

/*
** Computes the minimum between 'n' and 'MAX_SIZET/sizeof(t)', so that
** the result is not larger than 'n' and cannot overflow a 'size_t'
** when multiplied by the size of type 't'. (Assumes that 'n' is an
** 'int' or 'unsigned int' and that 'int' is not larger than 'size_t'.)
*/
// #define luaM_limitN(n,t) ((cast_sizet(n) <= MAX_SIZET/sizeof(t)) ? (n) : cast_uint((MAX_SIZET/sizeof(t))))

template<typename T>
size_t luaM_limitN (size_t n)
{
	return (cast_sizet(n) <= MAX_SIZET/sizeof(T))
		? n
		: cast_uint(MAX_SIZET/sizeof(T));
}


//

/*
** Arrays of chars do not need any test
*/
#define luaM_reallocvchar(L,b,on,n) cast_charp(luaM_saferealloc_(L, (b), (on)*sizeof(char), (n)*sizeof(char)))


// #define luaM_freemem(L, b, s)	luaM_free_(L, (b), (s))
template<typename T>
void luaM_freemem (lua_State* L, T* b, size_t s)
{
	luaM_free_(L, b, s);
}


// #define luaM_free(L, b)		luaM_free_(L, (b), sizeof(*(b)))
template<typename T>
void luaM_free (lua_State* L, T* b)
{
	luaM_free_(L, b, sizeof(*b));
}

#define luaM_freearray(L, b, n)   luaM_free_(L, (b), (n)*sizeof(*(b)))

//

//
// #define luaM_newmem(L,t)		cast(t*, luaM_malloc_(L, sizeof(t), 0))
template<typename T>
T* luaM_newmem (lua_State* L)
{
	return static_cast<T*>(luaM_malloc_(L, sizeof(T), 0));
}


// #define luaM_newvector(L,n,t)	cast(t*, luaM_malloc_(L, (n)*sizeof(t), 0))
template<typename T>
T* luaM_newvector (lua_State* L, int n)
{
	return static_cast<T*>(luaM_malloc_(L, (n)*sizeof(T), 0));
}


// #define luaM_newvectorchecked(L,n,t) (luaM_checksize(L,n,sizeof(t)), luaM_newvector<t>(L,n))
template<typename T>
T* luaM_newvectorchecked (lua_State* L, int n)
{
	if (luaM_testsize(n, sizeof(T)))
	{
		luaM_toobig(L);
	}
	return luaM_newvector<T>(L,n);
}


//

#define luaM_newobject(L,tag,s)	luaM_malloc_(L, (s), tag)

#define luaM_growvector(L,v,nelems,size,t,limit,e) \
	((v)=cast(t *, luaM_growaux_(L,v,nelems,&(size),sizeof(t), \
                         luaM_limitN<t>(limit),e)))

#define luaM_reallocvector(L, v,oldn,n,t) \
   (cast(t *, luaM_realloc_(L, v, cast_sizet(oldn) * sizeof(t), \
                                  cast_sizet(n) * sizeof(t))))

#define luaM_shrinkvector(L,v,size,fs,t) \
   ((v)=cast(t *, luaM_shrinkvector_(L, v, &(size), fs, sizeof(t))))


#endif
