cmake_minimum_required(VERSION 2.8.4)

project(config)

#set(CMAKE_VERBOSE_MAKEFILE ON)

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../include/generated")

BISON_TARGET(parser parser.y parser.tab.c COMPILE_FLAGS "-l -b parser -p yy -t")
FLEX_TARGET(lexer lexer.l lexer.lex.c COMPILE_FLAGS "-Pyy -L")

SET(parser_deps ${FLEX_lexer_OUTPUTS})
SET_SOURCE_FILES_PROPERTIES(${BISON_parser_OUTPUTS}
      PROPERTIES OBJECT_DEPENDS "${parser_deps}")

set(conf_src conf.c util.c symbol.c confdata.c expr.c preprocess.c ${BISON_parser_OUTPUTS} ${FLEX_lexer_OUTPUTS})

add_executable(conf ${conf_src})
