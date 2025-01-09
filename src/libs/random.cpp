#ifndef COYOTE_RANDOM
#define COYOTE_RANDOM

#include <cfloat>
#include <ctime>
#include "../lua.hpp"
#include "lauxlib.hpp"

/*
** {==================================================================
** Pseudo-Random Number Generator based on 'xoshiro256**'.
** ===================================================================
*/

/*
** This code uses lots of shifts. ANSI C does not allow shifts greater
** than or equal to the width of the type being shifted, so some shifts
** are written in convoluted ways to match that restriction. For
** preprocessor tests, it assumes a width of 32 bits, so the maximum
** shift there is 31 bits.
*/


/* number of binary digits in the mantissa of a float */
#define FIGS	l_floatatt(MANT_DIG)

#if FIGS > 64
/* there are only 64 random bits; use them all */
#undef FIGS
#define FIGS	64
#endif


/*
** LUA_RAND32 forces the use of 32-bit integers in the implementation
** of the PRN generator (mainly for testing).
*/
#if !defined(LUA_RAND32) && !defined(Rand64)

/* try to find an integer type with at least 64 bits */

#if ((ULONG_MAX >> 31) >> 31) >= 3

/* 'long' has at least 64 bits */
#define Rand64		unsigned long
#define SRand64		long

#elif !defined(LUA_USE_C89) && defined(LLONG_MAX)

/* there is a 'long long' type (which must have at least 64 bits) */
#define Rand64		unsigned long long
#define SRand64		long long

#elif ((LUA_MAXUNSIGNED >> 31) >> 31) >= 3

/* 'lua_Unsigned' has at least 64 bits */
#define Rand64		lua_Unsigned
#define SRand64		lua_Integer

#endif

#endif



/*
** Standard implementation, using 64-bit integers.
** If 'Rand64' has more than 64 bits, the extra bits do not interfere
** with the 64 initial bits, except in a right shift. Moreover, the
** final result has to discard the extra bits.
*/

/* avoid using extra bits when needed */
#define trim64(x)	((x) & 0xffffffffffffffffu)


/* rotate left 'x' by 'n' bits */
static Rand64 rotl(Rand64 x, int n)
{
	return (x << n) | (trim64(x) >> (64 - n));
}

static Rand64 nextrand(Rand64 *state)
{
	Rand64 state0 = state[0];
	Rand64 state1 = state[1];
	Rand64 state2 = state[2] ^ state0;
	Rand64 state3 = state[3] ^ state1;
	Rand64 res = rotl(state1 * 5, 7) * 9;
	state[0] = state0 ^ state3;
	state[1] = state1 ^ state2;
	state[2] = state2 ^ (state1 << 17);
	state[3] = rotl(state3, 45);
	return res;
}


/*
** Convert bits from a random integer into a float in the
** interval [0,1), getting the higher FIG bits from the
** random unsigned integer and converting that to a float.
** Some old Microsoft compilers cannot cast an unsigned long
** to a floating-point number, so we use a signed long as an
** intermediary. When lua_Number is float or double, the shift ensures
** that 'sx' is non negative; in that case, a good compiler will remove
** the correction.
*/

/* must throw out the extra (64 - FIGS) bits */
#define shift64_FIG	(64 - FIGS)

/* 2^(-FIGS) == 2^-1 / 2^(FIGS-1) */
#define scaleFIG	(l_mathop(0.5) / ((Rand64)1 << (FIGS - 1)))

static lua_Number I2d(Rand64 x)
{
	auto sx = (SRand64) (trim64(x) >> shift64_FIG);
	lua_Number res = static_cast<lua_Number>(sx) * scaleFIG;
	if (sx < 0)
		res += l_mathop(1.0); /* correct the two's complement if negative */
	lua_assert(0 <= res && res < 1);
	return res;
}

/* convert a 'Rand64' to a 'lua_Unsigned' */
#define I2UInt(x)	((lua_Unsigned)trim64(x))

/* convert a 'lua_Unsigned' to a 'Rand64' */
#define Int2I(x)	((Rand64)(x))




/*
** A state uses four 'Rand64' values.
*/
typedef struct
{
	Rand64 s[4];
} RanState;


/*
** Project the random integer 'ran' into the interval [0, n].
** Because 'ran' has 2^B possible values, the projection can only be
** uniform when the size of the interval is a power of 2 (exact
** division). Otherwise, to get a uniform projection into [0, n], we
** first compute 'lim', the smallest Mersenne number not smaller than
** 'n'. We then project 'ran' into the interval [0, lim].  If the result
** is inside [0, n], we are done. Otherwise, we try with another 'ran',
** until we have a result inside the interval.
*/
static lua_Unsigned project(lua_Unsigned ran, lua_Unsigned n,
									RanState *state)
{
	if ((n & (n + 1)) == 0) /* is 'n + 1' a power of 2? */
		return ran & n; /* no bias */
	lua_Unsigned lim = n;
	/* compute the smallest (2^b - 1) not smaller than 'n' */
	lim |= (lim >> 1);
	lim |= (lim >> 2);
	lim |= (lim >> 4);
	lim |= (lim >> 8);
	lim |= (lim >> 16);
#if (LUA_MAXUNSIGNED >> 31) >= 3
	lim |= (lim >> 32); /* integer type has more than 32 bits */
#endif
	lua_assert((lim & (lim + 1)) == 0 /* 'lim + 1' is a power of 2, */
		&& lim >= n /* not smaller than 'n', */
		&& (lim >> 1) < n); /* and it is the smallest one */
	while ((ran &= lim) > n) /* project 'ran' into [0..lim] */
		ran = I2UInt(nextrand(state->s)); /* not inside [0..n]? try again */
	return ran;
}


static int math_random(lua_State *L)
{
	lua_Integer low, up;
	lua_Unsigned p;
	auto *state = static_cast<RanState *>(lua_touserdata(L, lua_upvalueindex(1)));
	Rand64 rv = nextrand(state->s); /* next pseudo-random value */
	switch (lua_gettop(L))
	{
		/* check number of arguments */
		case 0: {
			/* no arguments */
			lua_pushnumber(L, I2d(rv)); /* float between 0 and 1 */
			return 1;
		}
		case 1: {
			/* only upper limit */
			low = 1;
			up = luaL_checkinteger(L, 1);
			if (up == 0)
			{
				/* single 0 as argument? */
				lua_pushinteger(L, I2UInt(rv)); /* full random integer */
				return 1;
			}
			break;
		}
		case 2: {
			/* lower and upper limits */
			low = luaL_checkinteger(L, 1);
			up = luaL_checkinteger(L, 2);
			break;
		}
		default: return luaL_error(L, "wrong number of arguments");
	}
	/* random integer in the interval [low, up] */
	luaL_argcheck(L, low <= up, 1, "interval is empty");
	/* project random integer into the interval [0, up - low] */
	p = project(I2UInt(rv), (lua_Unsigned) up - (lua_Unsigned) low, state);
	lua_pushinteger(L, p + (lua_Unsigned) low);
	return 1;
}


static void setseed(lua_State *L, Rand64 *state,
							lua_Unsigned n1, lua_Unsigned n2)
{
	int i;
	state[0] = Int2I(n1);
	state[1] = Int2I(0xff); /* avoid a zero state */
	state[2] = Int2I(n2);
	state[3] = Int2I(0);
	for (i = 0; i < 16; i++)
		nextrand(state); /* discard initial values to "spread" seed */
	lua_pushinteger(L, n1);
	lua_pushinteger(L, n2);
}


/*
** Set a "random" seed. To get some randomness, use the current time
** and the address of 'L' (in case the machine does address space layout
** randomization).
*/
static void randseed(lua_State *L, RanState *state)
{
	lua_Unsigned seed1 = (lua_Unsigned) time(NULL);
	lua_Unsigned seed2 = (lua_Unsigned) (size_t) L;
	setseed(L, state->s, seed1, seed2);
}


static int math_randomseed(lua_State *L)
{
	RanState *state = (RanState *) lua_touserdata(L, lua_upvalueindex(1));
	if (lua_isnone(L, 1))
	{
		randseed(L, state);
	}
	else
	{
		lua_Integer n1 = luaL_checkinteger(L, 1);
		lua_Integer n2 = luaL_optinteger(L, 2, 0);
		setseed(L, state->s, n1, n2);
	}
	return 2; /* return seeds */
}


static const luaL_Reg randfuncs[] = {
	{"random", math_random},
	{"randomseed", math_randomseed},
	{NULL, NULL}
};


/*
** Register the random functions and initialize their state.
*/
static void setrandfunc(lua_State *L)
{
	auto *state = lua_newuserdatauvt<RanState>(L, 0);
	randseed(L, state); /* initialize with a "random" seed */
	lua_pop(L, 2); /* remove pushed seeds */
	luaL_setfuncs(L, randfuncs, 1);
}

/* }================================================================== */

#endif
