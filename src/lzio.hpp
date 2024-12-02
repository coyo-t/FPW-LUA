/*
** $Id: lzio.h $
** Buffered streams
** See Copyright Notice in lua.h
*/


#ifndef lzio_h
#define lzio_h

#include<cstdint>
#include "lua.hpp"

#include "lmem.hpp"


constexpr int EOZ = -1; /* end of stream */

typedef struct Zio ZIO;


inline int zgetc (ZIO* z);

struct CoyoteBuffer
{
	uint8_t* buffer; // data
	size_t n; // limit
	size_t buffsize; // capacity

	void initbuffer (lua_State* L)
	{
		buffer = nullptr;
		buffsize = 0;
	}

	size_t sizebuffer() const
	{
		return buffsize;
	}

	size_t bufflen() const
	{
		return n;
	}

	void buffremove(size_t i)
	{
		n -= i;
	}

	void resetbuffer()
	{
		n = 0;
	}

	void resizebuffer(lua_State *L, size_t size)
	{
		buffer = luaM_realloc(
			L,
			buffer,
			buffsize,
			size
		);
		buffsize = size;
	}

	void luaZ_freebuffer(lua_State *L)
	{
		resizebuffer(L, 0);
	}
};

typedef struct Mbuffer
{
	char *buffer;
	size_t n;
	size_t buffsize;
} Mbuffer;


LUAI_FUNC void luaZ_initbuffer(lua_State *L, Mbuffer *buff);

LUAI_FUNC char *luaZ_buffer(Mbuffer *buff);

LUAI_FUNC size_t luaZ_sizebuffer(Mbuffer *buff);

LUAI_FUNC size_t luaZ_bufflen(Mbuffer *buff);

LUAI_FUNC void luaZ_buffremove(Mbuffer *buff, size_t i);

LUAI_FUNC void luaZ_resetbuffer(Mbuffer *buff);


LUAI_FUNC void luaZ_resizebuffer(lua_State *L, Mbuffer *buff, size_t size);


LUAI_FUNC void luaZ_freebuffer(lua_State *L, Mbuffer *buff);


LUAI_FUNC void luaZ_init(lua_State *L, ZIO *z, lua_Reader reader,
								void *data);

LUAI_FUNC size_t luaZ_read(ZIO *z, void *b, size_t n); /* read next n bytes */


/* --------- Private Part ------------------ */

struct Zio
{
	size_t n; /* bytes still unread */
	const char *p; /* current position in buffer */
	lua_Reader reader; /* reader function */
	void *data; /* additional data */
	lua_State *L; /* Lua state (for reader) */
};


LUAI_FUNC int luaZ_fill(ZIO *z);

#endif
