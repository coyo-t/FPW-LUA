/*
** $Id: lzio.h $
** Buffered streams
** See Copyright Notice in lua.h
*/


#ifndef lzio_h
#define lzio_h

#include "lua.hpp"

#include "lmem.hpp"


/* end of stream */
constexpr auto EOZ =	-1;

typedef struct Zio ZIO;


int zgetc (ZIO* z);

struct Mbuffer
{
	char *buffer;
	size_t n;
	size_t buffsize;

	auto initbuffer (lua_State* L) -> void;

	auto getbuffer () -> char*;
};

// void luaZ_initbuffer (lua_State* L, Mbuffer* buff);
// char* luaZ_buffer (Mbuffer* buff);
size_t luaZ_sizebuffer (Mbuffer* buff);
size_t luaZ_bufflen (Mbuffer* buff);

template<typename N>
void luaZ_buffremove (Mbuffer* buff, N i)
{
	((buff)->n -= (i));
}

void luaZ_resetbuffer (Mbuffer* buff);


template<typename N>
void luaZ_resizebuffer (lua_State* L, Mbuffer* buff, N size)
{
	buff->buffer = luaM::reallocvchar(
		L,
		(buff)->buffer,
		(buff)->buffsize,
		size
	);
	(buff)->buffsize = size;
}


void luaZ_freebuffer (lua_State* L, Mbuffer* buff);


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
