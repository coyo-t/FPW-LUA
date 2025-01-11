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

	auto initbuffer ([[maybe_unused]] lua_State* L) -> void
	{
		this->buffer = nullptr;
		this->buffsize = 0;
	}

	auto getbuffer () -> char*
	{
		return this->buffer;
	}

	auto sizebuffer () -> size_t
	{
		return this->buffsize;
	}

	auto bufflen () -> size_t
	{
		return this->n;
	}

	auto buffremove (size_t amount) -> void
	{
		this->n -= amount;
	}

	auto resetbuffer () -> void
	{
		this->n = 0;
	}

	auto resizebuffer (lua_State* L, size_t size) -> void
	{
		this->buffer = luaM::reallocvchar(
			L,
			this->buffer,
			this->buffsize,
			size
		);
		this->buffsize = size;
	}

	auto freebuffer (lua_State*L) -> void
	{
		this->resizebuffer(L, 0);
	}
};


// template<typename N>
// void luaZ_resizebuffer (lua_State* L, Mbuffer* buff, N size)
// {
// 	buff->buffer = luaM::reallocvchar(
// 		L,
// 		(buff)->buffer,
// 		(buff)->buffsize,
// 		size
// 	);
// 	(buff)->buffsize = size;
// }


// void luaZ_freebuffer (lua_State* L, Mbuffer* buff);


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
