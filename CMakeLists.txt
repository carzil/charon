cmake_minimum_required(VERSION 3.0.0)

project(charon VERSION 0.1)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS} -Wextra -O0 -g -fsanitize=address")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS} -Ofast")


if (CMAKE_BUILD_TYPE MATCHES DEBUG)
    add_definitions(-DCHARON_DEBUG)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
    ${CMAKE_CURRENT_BINARY_DIR}/src/include
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include/defs.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/src/include/defs.h
)

set(SOURCE_FILES
    src/http.c
    src/http_parser.c
    src/client.c
    src/chain.c
    src/handlers/http_handler.c
    src/conf.c
    src/server.c
    src/http/vhost.c
    src/timer.c
)

add_executable(charon src/main.c ${SOURCE_FILES})
