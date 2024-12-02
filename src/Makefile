# Makefile for building Lua
# See ../doc/readme.html for installation and customization instructions.

# == CHANGE THE SETTINGS BELOW TO SUIT YOUR ENVIRONMENT =======================

# Your platform. See PLATS for possible values.
PLAT= guess

CC= gcc -std=gnu99
CFLAGS= -O2 -Wall -Wextra -DLUA_COMPAT_5_3 $(SYSCFLAGS) $(MYCFLAGS)
LDFLAGS= $(SYSLDFLAGS) $(MYLDFLAGS)
LIBS= -lm $(SYSLIBS) $(MYLIBS)

AR= ar rcu
RANLIB= ranlib
RM= rm -f
UNAME= uname

SYSCFLAGS=
SYSLDFLAGS=
SYSLIBS=

MYCFLAGS=
MYLDFLAGS=
MYLIBS=
MYOBJS=

# Special flags for compiler modules; -Os reduces code size.
CMCFLAGS= 

# == END OF USER SETTINGS -- NO NEED TO CHANGE ANYTHING BELOW THIS LINE =======

PLATS= guess aix bsd c89 freebsd generic ios linux linux-readline macosx mingw posix solaris

LUA_A=	liblua.a
CORE_O=	lapi.o lcode.o lctype.o ldebug.o ldo.o ldump.o lfunc.o lgc.o llex.o lmem.o lobject.o lopcodes.o lparser.o lstate.o lstring.o ltable.o ltm.o lundump.o lvm.o lzio.o
LIB_O=	lauxlib.o lbaselib.o lcorolib.o ldblib.o liolib.o lmathlib.o loadlib.o loslib.o lstrlib.o ltablib.o lutf8lib.o linit.o
BASE_O= $(CORE_O) $(LIB_O) $(MYOBJS)

LUA_T=	lua
LUA_O=	lua.o

LUAC_T=	luac
LUAC_O=	luac.o

ALL_O= $(BASE_O) $(LUA_O) $(LUAC_O)
ALL_T= $(LUA_A) $(LUA_T) $(LUAC_T)
ALL_A= $(LUA_A)

# Targets start here.
default: $(PLAT)

all:	$(ALL_T)

o:	$(ALL_O)

a:	$(ALL_A)

$(LUA_A): $(BASE_O)
	$(AR) $@ $(BASE_O)
	$(RANLIB) $@

$(LUA_T): $(LUA_O) $(LUA_A)
	$(CC) -o $@ $(LDFLAGS) $(LUA_O) $(LUA_A) $(LIBS)

$(LUAC_T): $(LUAC_O) $(LUA_A)
	$(CC) -o $@ $(LDFLAGS) $(LUAC_O) $(LUA_A) $(LIBS)

test:
	./$(LUA_T) -v

clean:
	$(RM) $(ALL_T) $(ALL_O)

depend:
	@$(CC) $(CFLAGS) -MM l*.c

echo:
	@echo "PLAT= $(PLAT)"
	@echo "CC= $(CC)"
	@echo "CFLAGS= $(CFLAGS)"
	@echo "LDFLAGS= $(LDFLAGS)"
	@echo "LIBS= $(LIBS)"
	@echo "AR= $(AR)"
	@echo "RANLIB= $(RANLIB)"
	@echo "RM= $(RM)"
	@echo "UNAME= $(UNAME)"

# Convenience targets for popular platforms.
ALL= all

help:
	@echo "Do 'make PLATFORM' where PLATFORM is one of these:"
	@echo "   $(PLATS)"
	@echo "See doc/readme.html for complete instructions."

guess:
	@echo Guessing `$(UNAME)`
	@$(MAKE) `$(UNAME)`

AIX aix:
	$(MAKE) $(ALL) CC="xlc" CFLAGS="-O2 -DLUA_USE_POSIX -DLUA_USE_DLOPEN" SYSLIBS="-ldl" SYSLDFLAGS="-brtl -bexpall"

bsd:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_POSIX -DLUA_USE_DLOPEN" SYSLIBS="-Wl,-E"

c89:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_C89" CC="gcc -std=c89"
	@echo ''
	@echo '*** C89 does not guarantee 64-bit integers for Lua.'
	@echo '*** Make sure to compile all external Lua libraries'
	@echo '*** with LUA_USE_C89 to ensure consistency'
	@echo ''

FreeBSD NetBSD OpenBSD freebsd:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_LINUX -DLUA_USE_READLINE -I/usr/include/edit" SYSLIBS="-Wl,-E -ledit" CC="cc"

generic: $(ALL)

ios:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_IOS"

Linux linux:	linux-noreadline

linux-noreadline:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_LINUX" SYSLIBS="-Wl,-E -ldl"

linux-readline:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_LINUX -DLUA_USE_READLINE" SYSLIBS="-Wl,-E -ldl -lreadline"

Darwin macos macosx:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_MACOSX -DLUA_USE_READLINE" SYSLIBS="-lreadline"

mingw:
	$(MAKE) "LUA_A=lua54.dll" "LUA_T=lua.exe" \
	"AR=$(CC) -shared -o" "RANLIB=strip --strip-unneeded" \
	"SYSCFLAGS=-DLUA_BUILD_AS_DLL" "SYSLIBS=" "SYSLDFLAGS=-s" lua.exe
	$(MAKE) "LUAC_T=luac.exe" luac.exe

posix:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_POSIX"

SunOS solaris:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_POSIX -DLUA_USE_DLOPEN -D_REENTRANT" SYSLIBS="-ldl"

# Targets that do not create files (not all makes understand .PHONY).
.PHONY: all $(PLATS) help test clean default o a depend echo

# Compiler modules may use special flags.
llex.o:
	$(CC) $(CFLAGS) $(CMCFLAGS) -c llex.c

lparser.o:
	$(CC) $(CFLAGS) $(CMCFLAGS) -c lparser.c

lcode.o:
	$(CC) $(CFLAGS) $(CMCFLAGS) -c lcode.c

# DO NOT DELETE

lapi.o: lapi.cpp lprefix.hpp lua.hpp luaconf.hpp lapi.hpp llimits.hpp lstate.hpp \
 lobject.hpp ltm.hpp lzio.hpp lmem.hpp ldebug.hpp ldo.hpp lfunc.hpp lgc.hpp lstring.hpp \
 ltable.hpp lundump.hpp lvm.hpp
lauxlib.o: lauxlib.cpp lprefix.hpp lua.hpp luaconf.hpp lauxlib.hpp
lbaselib.o: libs/lbaselib.cpp lprefix.hpp lua.hpp luaconf.hpp lauxlib.hpp lualib.h
lcode.o: lcode.cpp lprefix.hpp lua.hpp luaconf.hpp lcode.hpp llex.hpp lobject.hpp \
 llimits.hpp lzio.hpp lmem.hpp lopcodes.hpp lparser.hpp ldebug.hpp lstate.hpp ltm.hpp \
 ldo.hpp lgc.hpp lstring.hpp ltable.hpp lvm.hpp
lcorolib.o: libs/lcorolib.cpp lprefix.hpp lua.hpp luaconf.hpp lauxlib.hpp lualib.h
lctype.o: lctype.cpp lprefix.hpp lctype.hpp lua.hpp luaconf.hpp llimits.hpp
ldblib.o: libs/ldblib.cpp lprefix.hpp lua.hpp luaconf.hpp lauxlib.hpp lualib.h
ldebug.o: ldebug.cpp lprefix.hpp lua.hpp luaconf.hpp lapi.hpp llimits.hpp lstate.hpp \
 lobject.hpp ltm.hpp lzio.hpp lmem.hpp lcode.hpp llex.hpp lopcodes.hpp lparser.hpp \
 ldebug.hpp ldo.hpp lfunc.hpp lstring.hpp lgc.hpp ltable.hpp lvm.hpp
ldo.o: ldo.cpp lprefix.hpp lua.hpp luaconf.hpp lapi.hpp llimits.hpp lstate.hpp \
 lobject.hpp ltm.hpp lzio.hpp lmem.hpp ldebug.hpp ldo.hpp lfunc.hpp lgc.hpp lopcodes.hpp \
 lparser.hpp lstring.hpp ltable.hpp lundump.hpp lvm.hpp
ldump.o: ldump.cpp lprefix.hpp lua.hpp luaconf.hpp lobject.hpp llimits.hpp lstate.hpp \
 ltm.hpp lzio.hpp lmem.hpp lundump.hpp
lfunc.o: lfunc.cpp lprefix.hpp lua.hpp luaconf.hpp ldebug.hpp lstate.hpp lobject.hpp \
 llimits.hpp ltm.hpp lzio.hpp lmem.hpp ldo.hpp lfunc.hpp lgc.hpp
lgc.o: lgc.cpp lprefix.hpp lua.hpp luaconf.hpp ldebug.hpp lstate.hpp lobject.hpp \
 llimits.hpp ltm.hpp lzio.hpp lmem.hpp ldo.hpp lfunc.hpp lgc.hpp lstring.hpp ltable.hpp
linit.o: libs/linit.cpp lprefix.hpp lua.hpp luaconf.hpp lualib.h lauxlib.hpp
liolib.o: libs/liolib.cpp lprefix.hpp lua.hpp luaconf.hpp lauxlib.hpp lualib.h
llex.o: llex.cpp lprefix.hpp lua.hpp luaconf.hpp lctype.hpp llimits.hpp ldebug.hpp \
 lstate.hpp lobject.hpp ltm.hpp lzio.hpp lmem.hpp ldo.hpp lgc.hpp llex.hpp lparser.hpp \
 lstring.hpp ltable.hpp
lmathlib.o: libs/lmathlib.cpp lprefix.hpp lua.hpp luaconf.hpp lauxlib.hpp lualib.h
lmem.o: lmem.cpp lprefix.hpp lua.hpp luaconf.hpp ldebug.hpp lstate.hpp lobject.hpp \
 llimits.hpp ltm.hpp lzio.hpp lmem.hpp ldo.hpp lgc.hpp
loadlib.o: libs/loadlib.cpp lprefix.hpp lua.hpp luaconf.hpp lauxlib.hpp lualib.h
lobject.o: lobject.cpp lprefix.hpp lua.hpp luaconf.hpp lctype.hpp llimits.hpp \
 ldebug.hpp lstate.hpp lobject.hpp ltm.hpp lzio.hpp lmem.hpp ldo.hpp lstring.hpp lgc.hpp \
 lvm.hpp
lopcodes.o: lopcodes.cpp lprefix.hpp lopcodes.hpp llimits.hpp lua.hpp luaconf.hpp
loslib.o: libs/loslib.cpp lprefix.hpp lua.hpp luaconf.hpp lauxlib.hpp lualib.h
lparser.o: lparser.cpp lprefix.hpp lua.hpp luaconf.hpp lcode.hpp llex.hpp lobject.hpp \
 llimits.hpp lzio.hpp lmem.hpp lopcodes.hpp lparser.hpp ldebug.hpp lstate.hpp ltm.hpp \
 ldo.hpp lfunc.hpp lstring.hpp lgc.hpp ltable.hpp
lstate.o: lstate.cpp lprefix.hpp lua.hpp luaconf.hpp lapi.hpp llimits.hpp lstate.hpp \
 lobject.hpp ltm.hpp lzio.hpp lmem.hpp ldebug.hpp ldo.hpp lfunc.hpp lgc.hpp llex.hpp \
 lstring.hpp ltable.hpp
lstring.o: lstring.cpp lprefix.hpp lua.hpp luaconf.hpp ldebug.hpp lstate.hpp \
 lobject.hpp llimits.hpp ltm.hpp lzio.hpp lmem.hpp ldo.hpp lstring.hpp lgc.hpp
lstrlib.o: libs/lstrlib.cpp lprefix.hpp lua.hpp luaconf.hpp lauxlib.hpp lualib.h
ltable.o: ltable.cpp lprefix.hpp lua.hpp luaconf.hpp ldebug.hpp lstate.hpp lobject.hpp \
 llimits.hpp ltm.hpp lzio.hpp lmem.hpp ldo.hpp lgc.hpp lstring.hpp ltable.hpp lvm.hpp
ltablib.o: libs/ltablib.cpp lprefix.hpp lua.hpp luaconf.hpp lauxlib.hpp lualib.h
ltm.o: ltm.cpp lprefix.hpp lua.hpp luaconf.hpp ldebug.hpp lstate.hpp lobject.hpp \
 llimits.hpp ltm.hpp lzio.hpp lmem.hpp ldo.hpp lgc.hpp lstring.hpp ltable.hpp lvm.hpp
lua.o: lua.c lprefix.hpp lua.hpp luaconf.hpp lauxlib.hpp lualib.h
luac.o: luac.cpp lprefix.hpp lua.hpp luaconf.hpp lauxlib.hpp ldebug.hpp lstate.hpp \
 lobject.hpp llimits.hpp ltm.hpp lzio.hpp lmem.hpp lopcodes.hpp lopnames.hpp lundump.hpp
lundump.o: lundump.cpp lprefix.hpp lua.hpp luaconf.hpp ldebug.hpp lstate.hpp \
 lobject.hpp llimits.hpp ltm.hpp lzio.hpp lmem.hpp ldo.hpp lfunc.hpp lstring.hpp lgc.hpp \
 lundump.hpp
lutf8lib.o: libs/lutf8lib.cpp lprefix.hpp lua.hpp luaconf.hpp lauxlib.hpp lualib.h
lvm.o: lvm.cpp lprefix.hpp lua.hpp luaconf.hpp ldebug.hpp lstate.hpp lobject.hpp \
 llimits.hpp ltm.hpp lzio.hpp lmem.hpp ldo.hpp lfunc.hpp lgc.hpp lopcodes.hpp lstring.hpp \
 ltable.hpp lvm.hpp ljumptab.hpp
lzio.o: lzio.cpp lprefix.hpp lua.hpp luaconf.hpp llimits.hpp lmem.hpp lstate.hpp \
 lobject.hpp ltm.hpp lzio.hpp

# (end of Makefile)
