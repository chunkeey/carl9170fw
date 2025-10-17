cmake_minimum_required(VERSION 3.10)

project(tools)

if (CONFIG_CARL9170FW_MAKE_RELEASE)
	set(CMAKE_BUILD_TYPE Release)
endif (CONFIG_CARL9170FW_MAKE_RELEASE)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/extra)

include("../config.cmake")

include_directories (../include/linux ../include/shared ../include lib include)
add_subdirectory(lib)
add_subdirectory(src)
