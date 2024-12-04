if(GCC_TOOLCHAIN)
    #first choice -DGCC_TOOLCHAIN GCC_HOME
    set(GCC_TOOLCHAIN "${GCC_TOOLCHAIN}")
elseif (DEFINED ENV{GCC_TOOLCHAIN})
    #last choice env GCC_TOOLCHAIN
    set(GCC_TOOLCHAIN "$ENV{GCC_TOOLCHAIN}")
else ()
    message (FATAL_ERROR "clang must set -DGCC_TOOLCHAIN like '/usr/local/gcc-10.2.0'")
endif ()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --gcc-toolchain=${GCC_TOOLCHAIN}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --gcc-toolchain=${GCC_TOOLCHAIN}")

message(STATUS "Add --gcc-toolchain=${GCC_TOOLCHAIN}")