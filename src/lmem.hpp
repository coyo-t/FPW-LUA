/*
** $Id: lmem.h $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#ifndef lmem_h
#define lmem_h


#include <cstddef>

#include<cstdint>

#include "ldo.hpp"
#include "llimits.hpp"
#include "lua.hpp"


namespace luaM {
	inline l_noret error (lua_State* L)
	{
		luaD_throw(L, LUA_ERRMEM);
	}

	l_noret toobig(lua_State *L);

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
	template<typename N>
	constexpr bool testsize (N n, size_t e)
	{
		return sizeof(n) >= sizeof(size_t) && static_cast<size_t>(n) + 1 > MAX_SIZET/e;
	}

	template<typename N>
	void checksize (lua_State* L, N n, size_t e)
	{
		if (testsize(n,e))
		{
			toobig(L);
		}
	}

	/*
	** Computes the minimum between 'n' and 'MAX_SIZET/sizeof(t)', so that
	** the result is not larger than 'n' and cannot overflow a 'size_t'
	** when multiplied by the size of type 't'. (Assumes that 'n' is an
	** 'int' or 'unsigned int' and that 'int' is not larger than 'size_t'.)
	*/
	template<typename T, typename N>
	constexpr bool limitN (N n)
	{
		if (static_cast<size_t>(n) <= MAX_SIZET/sizeof(T))
		{
			return n;
		}
		return static_cast<uint32_t>(MAX_SIZET/sizeof(T));
	}

	/* not to be called directly */
	LUAI_FUNC void *realloc_(
		lua_State *L,
		void *block,
		size_t oldsize,
		size_t size
	);

	LUAI_FUNC void *saferealloc_(
		lua_State *L,
		void *block,
		size_t oldsize,
		size_t size
	);

	LUAI_FUNC void free_(lua_State *L, void *block, size_t osize);

	LUAI_FUNC void *growaux_(
		lua_State *L,
		void *block,
		int nelems,
		int *size,
		int size_elem,
		int limit,
		const char *what
	);

	LUAI_FUNC void *shrinkvector_(
		lua_State *L,
		void *block,
		int *nelem,
		int final_n,
		int size_elem
	);

	LUAI_FUNC void *malloc_(lua_State *L, size_t size, int tag);

	/*
	** Arrays of chars do not need any test
	*/
	template<typename T>
	T* realloc (lua_State* L, T* block, size_t oldsize, size_t newsize)
	{
		return luaM::saferealloc_(L, block, oldsize*sizeof(T), newsize*sizeof(T));
	}

	template<typename T>
	void freemem(lua_State* L, T* b, size_t s)
	{
		luaM::free_(L, b, s);
	}

	template<typename T>
	void free(lua_State* L, T* b)
	{
		luaM::free_(L, b, sizeof(T));
	}

	template<typename T>
	void freearray(lua_State* L, T* b, size_t n)
	{
		luaM::free_(L, b, n*sizeof(T));
	}

	template<typename T>
	T* newmem(lua_State* L)
	{
		return luaM::malloc_(L, sizeof(T), 0);
	}
	template<typename T>
	T* newvector(lua_State* L, size_t n)
	{
		return luaM::malloc_(L, n*sizeof(T), 0);
	}

	template<typename T>
	T* newvectorchecked(lua_State* L, size_t n)
	{
		luaM::checksize(L,n,sizeof(T));
		return luaM::newvector<T>(L,n);
	}

	void* newobject(lua_State* L, int tag, size_t s)
	{
	// #define luaM_newobject(L,tag,s)	luaM_malloc_(L, (s), tag)
		return luaM::malloc_(L, s, tag);
	}

	template<typename T>
	void growvector (
		lua_State* L,
		T** v,
		int nelems,
		int* size,
		int limit,
		const char* what)
	{
	// #define luaM_growvector(L,v,nelems,size,t,limit,e) \
	// ((v)=cast(t *, luaM_growaux_(L,v,nelems,&(size),sizeof(t), \
	// luaM_limitN(limit,t),e)))
		*v=luaM::growaux_(L,*v,nelems,size,sizeof(T), luaM::limitN<T>(limit),what);
	}

	template<typename T>
	T* reallocvector(lua_State* L, T* v, size_t oldn, size_t newn)
	{
		// #define luaM_reallocvector(L, v,oldn,n,t) \
		// (cast(t *, luaM_realloc_(L, v, cast_sizet(oldn) * sizeof(t), \
		// cast_sizet(n) * sizeof(t))))
		return luaM_realloc_(
			L,
			v,
			oldn * sizeof(T),
			newn * sizeof(T)
		);
	}

	// TODO: might not have recreated this one 1:1. double pointers hurt braen!!!
	template<typename T>
	void shrinkvector(lua_State* L, T** v, int* size, int final_n)
	{
	// #define luaM_shrinkvector(L,v,size,fs,t) \
	// ((v)=cast(t *, luaM_shrinkvector_(L, v, &(size), fs, sizeof(t))))
		*v = luaM::shrinkvector_(L, *v, size, final_n, sizeof(T));
	}
}




#endif
