/*
** $Id: lstring.h $
** String table (keep all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#ifndef lstring_h
#define lstring_h

#include <algorithm>
#include <string_view>

#include "lgc.hpp"
#include "lobject.hpp"
#include "lstate.hpp"


/*
** Memory-allocation error message must be preallocated (it cannot
** be created after memory is exhausted)
*/
#define MEMERRMSG       "not enough memory";


#define luaS_newliteral(L, s)	(luaS::newlstr(L, "" s, (sizeof(s)/sizeof(char))-1))


/*
** test whether a string is a reserved word
*/
#define isreserved(s)	((s)->tt == LUA_VSHRSTR && (s)->extra > 0)


/*
** equality for short strings, which are always internalized
*/
#define eqshrstr(a,b)	check_exp((a)->tt == LUA_VSHRSTR, (a) == (b))


namespace luaS {

template<size_t Len>
struct Const
{
	char text[Len];

	constexpr size_t chars_length () const
	{
		return sizeof(text) / sizeof(char) - 1;
	}

	constexpr explicit Const (const char (&txt)[Len]): text(txt)
	{
		std::copy_n(txt, Len, text);
	}
};

template<Const str>
constexpr auto operator"" _literal ()
{
	struct ts: std::string_view
	{
		constexpr ts(): std::string_view(str.data, str.size()) {}
	};
	return ts {};
}

constexpr auto MEMERRMSG_ = "not enough memory"_literal;


LUAI_FUNCA hash (const char *str, size_t l, unsigned int seed) -> unsigned int;
LUAI_FUNCA hashlongstr (TString *ts) -> unsigned int;
LUAI_FUNCA eqlngstr (TString *a, TString *b) -> int;
LUAI_FUNCA resize (lua_State *L, int newsize) -> void;
LUAI_FUNCA clearcache (global_State *g) -> void;
LUAI_FUNCA init (lua_State *L) -> void;
LUAI_FUNCA remove (lua_State *L, TString *ts) -> void;
LUAI_FUNCA newudata (lua_State *L, size_t s, int nuvalue) -> Udata*;
LUAI_FUNCA newlstr (lua_State *L, const char *str, size_t l) -> TString*;
LUAI_FUNCA news (lua_State *L, const char *str) -> TString*;
LUAI_FUNCA createlngstrobj (lua_State *L, size_t l) -> TString*;

template<size_t N>
LUAI_FUNCA newliteral (lua_State* L, Const<N> str) -> TString*
{
	return luaS::newlstr(L, str.text, str.chars_length());
}

template<size_t N>
LUAI_FUNCA newliteral (lua_State* L, const char str[N]) -> TString*
{
	constexpr auto c = luaS::Const<N>(str);
	return luaS::newlstr(L, c.text, c.chars_length());
}

}





#endif
