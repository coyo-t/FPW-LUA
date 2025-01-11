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


/* --------- Private Part ------------------ */

struct Zio
{
	/* bytes still unread */
	size_t n;
	/* current position in buffer */
	const char *p;
	/* reader function */
	lua_Reader reader;
	/* additional data */
	void *data;
	/* Lua state (for reader) */
	lua_State *L;


	auto fill () -> int
	{
		size_t size;
		lua_State *L = this->L;
		const char *buff;
		lua_unlock(L);
		buff = this->reader(L, this->data, &size);
		lua_lock(L);
		if (buff == nullptr || size == 0)
			return EOZ;
		/* discount char being returned */
		this->n = size - 1;
		this->p = buff;
		return cast_uchar(*(this->p++));
	}

	auto zgetc () -> int
	{
		return (this->n-- > 0) ? cast_uchar(*this->p++) : this->fill();
	}

	auto init (lua_State *L, lua_Reader reader, void *data) -> void
	{
		this->L = L;
		this->reader = reader;
		this->data = data;
		this->n = 0;
		this->p = nullptr;
	}

	/* read next n bytes */
	auto read (void *b, size_t n) -> size_t;
};

#endif
