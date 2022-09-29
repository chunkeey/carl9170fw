cmake_minimum_required(VERSION 3.0)

project(tools)

add_custom_target(wol ALL COMMAND gcc wol.c -o wol)

set(tools fwinfo miniboot checksum)

foreach(tool ${tools})
	add_executable( ${tool} ${tool}.c )
	add_definitions("-Wall -Wextra -Wshadow")
	target_link_libraries( ${tool} carlfw )
endforeach()
