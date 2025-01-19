cmake_minimum_required(VERSION 3.29)
project(LuaMod)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 23)

set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

include_directories(src)

add_library(
	LuaMod
	SHARED
		  src/image/stb_image.hpp
	src/lapi.cpp
	src/lapi.hpp
	src/lauxlib.cpp
	src/libs/lbaselib.cpp
	src/lcode.cpp
	src/lcode.hpp
	src/libs/lcorolib.cpp
	src/lctype.cpp
	src/lctype.hpp
	src/libs/ldblib.cpp
	src/ldebug.cpp
	src/ldebug.hpp
	src/ldo.cpp
	src/ldo.hpp
	src/dump/ldump.cpp
	src/lfunc.cpp
	src/lfunc.hpp
	src/lgc.cpp
	src/lgc.hpp
	src/libs/linit.cpp
	src/libs/liolib.cpp
	src/ljumptab.hpp
	src/llex.cpp
	src/llex.hpp
	src/llimits.hpp
	src/libs/lmathlib.cpp
	src/lmem.cpp
	src/lmem.hpp
	src/libs/loadlib.cpp
	src/lobject.cpp
	src/lobject.hpp
	src/lopcodes.cpp
	src/lopcodes.hpp
	src/lopnames.hpp
	src/libs/loslib.cpp
	src/lparser.cpp
	src/lparser.hpp
	src/lprefix.hpp
	src/lstate.cpp
	src/lstate.hpp
	src/lstring.cpp
	src/lstring.hpp
	src/libs/lstrlib.cpp
	src/ltable.cpp
	src/ltable.hpp
	src/libs/ltablib.cpp
	src/ltm.cpp
	src/ltm.hpp
	src/lua.cpp
	#src/luac.cpp
	src/dump/lundump.cpp
	src/dump/lundump.hpp
	src/libs/lutf8lib.cpp
	src/lvm.cpp
	src/lvm.hpp
	src/lzio.cpp
	src/lzio.hpp
	src/luacoyote.hpp
	src/luacoyote.cpp
		  src/libs/random/random.cpp
	src/libs/bufferlib.cpp
	src/luatemplate.hpp
		  src/image/stb_image.cpp
		  src/coyote/numberz.hpp
		  src/coyote/zlib.hpp
		  src/coyote/zlib.cpp
)
