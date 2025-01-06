cmake_minimum_required(VERSION 3.29)
project(LuaMod)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 23)

include_directories(src)

add_executable(LuaMod
        src/lapi.cpp
        src/lapi.hpp
        src/lauxlib.cpp
        src/lbaselib.cpp
        src/lcode.cpp
        src/lcode.hpp
        src/lcorolib.cpp
        src/lctype.cpp
        src/lctype.hpp
        src/ldblib.cpp
        src/ldebug.cpp
        src/ldebug.hpp
        src/ldo.cpp
        src/ldo.hpp
        src/ldump.cpp
        src/lfunc.cpp
        src/lfunc.hpp
        src/lgc.cpp
        src/lgc.hpp
        src/linit.cpp
        src/liolib.cpp
        src/ljumptab.hpp
        src/llex.cpp
        src/llex.hpp
        src/llimits.hpp
        src/lmathlib.cpp
        src/lmem.cpp
        src/lmem.hpp
        src/loadlib.cpp
        src/lobject.cpp
        src/lobject.hpp
        src/lopcodes.cpp
        src/lopcodes.hpp
        src/lopnames.hpp
        src/loslib.cpp
        src/lparser.cpp
        src/lparser.hpp
        src/lprefix.hpp
        src/lstate.cpp
        src/lstate.hpp
        src/lstring.cpp
        src/lstring.hpp
        src/lstrlib.cpp
        src/ltable.cpp
        src/ltable.hpp
        src/ltablib.cpp
        src/ltm.cpp
        src/ltm.hpp
        src/lua.cpp
        src/luac.cpp
        src/lundump.cpp
        src/lundump.hpp
        src/lutf8lib.cpp
        src/lvm.cpp
        src/lvm.hpp
        src/lzio.cpp
        src/lzio.hpp
        src/luacoyote.hpp
        src/luacoyote.cpp

)
