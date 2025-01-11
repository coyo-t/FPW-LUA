/*
** $Id: lzio.c $
** Buffered streams
** See Copyright Notice in lua.h
*/

#define lzio_c
#define LUA_CORE

#include "lprefix.hpp"


#include <string.h>

#include "lua.hpp"

#include "llimits.hpp"
#include "lmem.hpp"
#include "lstate.hpp"
#include "lzio.hpp"


// int zgetc(ZIO *z)
// {
// 	return (((z)->n--)>0 ?  cast_uchar(*(z)->p++) : z->fill());
// }


// void luaZ_freebuffer(lua_State *L, Mbuffer *buff)
// {
// 	luaZ_resizebuffer(L, buff, 0);
// }

void luaZ_init(lua_State *L, ZIO *z, lua_Reader reader, void *data)
{
	z->L = L;
	z->reader = reader;
	z->data = data;
	z->n = 0;
	z->p = nullptr;
}


/* --------------------------------------------------------------- read --- */
size_t luaZ_read(ZIO *z, void *b, size_t n)
{
	while (n)
	{
		size_t m;
		if (z->n == 0)
		{
			/* no bytes in buffer? */
			if (z->fill() == EOZ) /* try to read more */
				return n; /* no more input; return number of missing bytes */
			else
			{
				z->n++; /* luaZ_fill consumed first byte; put it back */
				z->p--;
			}
		}
		m = (n <= z->n) ? n : z->n; /* min. between n and z->n */
		memcpy(b, z->p, m);
		z->n -= m;
		z->p += m;
		b = (char *) b + m;
		n -= m;
	}
	return 0;
}
