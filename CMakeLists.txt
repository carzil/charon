cmake_minimum_required(VERSION 3.0.0)

project(charon VERSION 0.1)

option(CHARON_BUILD_TESTS "Build tests for charon" OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS} -Wextra -O0 -g -fsanitize=address")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS} -Ofast")

add_definitions(-DCHARON_DEBUG)
if (CMAKE_BUILD_TYPE MATCHES DEBUG)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

execute_process(
    COMMAND git log -1 --format=%h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/defs.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/defs.h
)

set(SOURCE_FILES
    src/http.c
    src/http/parser.c
    src/http/handler.c
    src/http/upstream.c
    src/http/vhost.c
    src/http/body.c

    src/connection.c
    src/chain.c
    src/conf.c
    src/worker.c
    src/timer.c
)

add_executable(charon src/main.c ${SOURCE_FILES})

install(TARGETS charon
    RUNTIME DESTINATION bin
)

install(FILES conf/main.conf
    DESTINATION /etc/charon
)

install(FILES charon.service
    DESTINATION /usr/lib/systemd/system
)

if(CHARON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
