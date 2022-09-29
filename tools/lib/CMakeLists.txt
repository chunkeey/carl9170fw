cmake_minimum_required(VERSION 3.0)

project(libcarlfw)

set(carlfw_src carlfw.c)

add_library( carlfw STATIC ${carlfw_src})
