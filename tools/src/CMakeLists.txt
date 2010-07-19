cmake_minimum_required(VERSION 2.8)

project(tools)

set(tools fwinfo miniboot checksum eeprom_fix)

foreach(tool ${tools})
	add_executable( ${tool} ${tool}.c )
	add_definitions("-std=gnu99 -Wall -Wextra -Wshadow")
	target_link_libraries( ${tool} carlfw )
endforeach()
