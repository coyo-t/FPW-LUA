/*
** $Id: lzio.c $
** Buffered streams
** See Copyright Notice in lua.h
*/

#define lzio_c
#define LUA_CORE

#include "lprefix.hpp"


#include <cstring>

#include "lzio.hpp"


/* --------------------------------------------------------------- read --- */

auto Zio::read(void *b, size_t n) -> size_t
{
	while (n)
	{
		if (this->n == 0)
		{
			/* no bytes in buffer? */
			if (this->fill() == EOZ) /* try to read more */
				return n; /* no more input; return number of missing bytes */
			this->n++; /* luaZ_fill consumed first byte; put it back */
			this->p--;
		}
		size_t m = (n <= this->n) ? n : this->n; /* min. between n and z->n */
		memcpy(b, this->p, m);
		this->n -= m;
		this->p += m;
		b = static_cast<char *>(b) + m;
		n -= m;
	}
	return 0;
}
