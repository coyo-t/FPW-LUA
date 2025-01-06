/*
** $Id: ldump.c $
** save precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#define ldump_c
#define LUA_CORE

#include "lprefix.hpp"


#include <limits.h>
#include <stddef.h>

#include "lua.hpp"

#include "lobject.hpp"
#include "lstate.hpp"
#include "lundump.hpp"

/*
** 'dumpSize' buffer size: each byte can store up to 7 bits. (The "+6"
** rounds up the division.)
*/
constexpr auto DIBS =    ((sizeof(size_t) * CHAR_BIT + 6) / 7);

struct DumpState
{
	lua_State *L;
	lua_Writer writer;
	void *data;
	int strip;
	int status;

	void dumpBlock (const void *b, size_t size)
	{
		if (this->status == 0 && size > 0)
		{
			lua_unlock(D->L);
			this->status = (*this->writer)(this->L, b, size, this->data);
			lua_lock(D->L);
		}
	}

	/*
	** All high-level dumps go through dumpVector; you can change it to
	** change the endianness of the result
	*/
	template<typename V, typename N>
	void dumpVector (V* v, N n)
	{
		dumpBlock(v, n * sizeof(v[0]));
	}

	template<typename V>
	void dumpVar (V* v)
	{
		dumpBlock(v, 1);
	}

	void dumpLiteral (const char* s)
	{
		dumpBlock(s, sizeof(s) - sizeof(char));
	}

	void dumpByte(int y)
	{
		lu_byte x = static_cast<lu_byte>(y);
		dumpVar(&x);
	}

	void dumpSize(size_t x)
	{
		lu_byte buff[DIBS];
		int n = 0;
		do
		{
			buff[DIBS - (++n)] = x & 0x7f; /* fill buffer in reverse order */
			x >>= 7;
		} while (x != 0);
		buff[DIBS - 1] |= 0x80; /* mark last byte */
		dumpVector(buff + DIBS - n, n);
	}


	void dumpInt(int x)
	{
		dumpSize(x);
	}


	void dumpNumber(lua_Number x)
	{
		dumpVar(&x);
	}


	void dumpInteger(lua_Integer x)
	{
		dumpVar(&x);
	}

	void dumpHeader()
	{
		dumpLiteral(LUA_SIGNATURE);
		dumpByte(LUAC_VERSION);
		dumpByte(LUAC_FORMAT);
		dumpLiteral(LUAC_DATA);
		dumpByte(sizeof(Instruction));
		dumpByte(sizeof(lua_Integer));
		dumpByte(sizeof(lua_Number));
		dumpInteger(LUAC_INT);
		dumpNumber(LUAC_NUM);
	}

	void dumpString(const TString *s)
	{
		if (s == NULL)
			dumpSize(0);
		else
		{
			size_t size = tsslen(s);
			const char *str = getstr(s);
			dumpSize(size + 1);
			dumpVector(str, size);
		}
	}

	void dumpCode(const Proto *f)
	{
		dumpInt(f->sizecode);
		dumpVector(f->code, f->sizecode);
	}

	inline void dumpFunction(const Proto *f, TString *psource);

	void dumpConstants(const Proto *f)
	{
		int i;
		int n = f->sizek;
		dumpInt(n);
		for (i = 0; i < n; i++)
		{
			const TValue *o = &f->k[i];
			int tt = ttypetag(o);
			dumpByte(tt);
			switch (tt)
			{
				case LUA_VNUMFLT:
					dumpNumber(fltvalue(o));
					break;
				case LUA_VNUMINT:
					dumpInteger(ivalue(o));
					break;
				case LUA_VSHRSTR:
				case LUA_VLNGSTR:
					dumpString(tsvalue(o));
					break;
				default:
					lua_assert(tt == LUA_VNIL || tt == LUA_VFALSE || tt == LUA_VTRUE);
			}
		}
	}

	void dumpProtos(const Proto *f)
	{
		int n = f->sizep;
		dumpInt(n);
		for (int i = 0; i < n; i++)
			dumpFunction(f->p[i], f->source);
	}

	void dumpUpvalues(const Proto *f)
	{
		int i, n = f->sizeupvalues;
		dumpInt(n);
		for (i = 0; i < n; i++)
		{
			dumpByte(f->upvalues[i].instack);
			dumpByte(f->upvalues[i].idx);
			dumpByte(f->upvalues[i].kind);
		}
	}

	void dumpDebug(const Proto *f)
	{
		int i, n;
		n = (strip) ? 0 : f->sizelineinfo;
		dumpInt(n);
		dumpVector(f->lineinfo, n);
		n = (strip) ? 0 : f->sizeabslineinfo;
		dumpInt(n);
		for (i = 0; i < n; i++)
		{
			dumpInt(f->abslineinfo[i].pc);
			dumpInt(f->abslineinfo[i].line);
		}
		n = (strip) ? 0 : f->sizelocvars;
		dumpInt(n);
		for (i = 0; i < n; i++)
		{
			dumpString(f->locvars[i].varname);
			dumpInt(f->locvars[i].startpc);
			dumpInt(f->locvars[i].endpc);
		}
		n = (strip) ? 0 : f->sizeupvalues;
		dumpInt(n);
		for (i = 0; i < n; i++)
			dumpString(f->upvalues[i].name);
	}


};

inline void DumpState::dumpFunction(const Proto *f, TString *psource)
{
	if (strip || f->source == psource)
		dumpString(nullptr); /* no debug info or same source as its parent */
	else
		dumpString(f->source);
	dumpInt(f->linedefined);
	dumpInt(f->lastlinedefined);
	dumpByte(f->numparams);
	dumpByte(f->is_vararg);
	dumpByte(f->maxstacksize);
	dumpCode(f);
	dumpConstants(f);
	dumpUpvalues(f);
	dumpProtos(f);
	dumpDebug(f);
}








/*
** dump Lua function as precompiled chunk
*/
int luaU_dump(lua_State *L, const Proto *f, lua_Writer w, void *data,
					int strip)
{
	DumpState D {
		.L = L,
		.writer = w,
		.data = data,
		.strip = strip,
		.status = 0,
	};
	// TODO: might need to do (&D)->dumpXYZ? silly
	D.dumpHeader();
	D.dumpByte(f->sizeupvalues);
	D.dumpFunction(f, NULL);
	return D.status;
}
