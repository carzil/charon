cmake_minimum_required(VERSION 3.0.0)

project(charon VERSION 0.1)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Wall")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS} -Wextra -O0 -g -fsanitize=address")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS} -Ofast")
    

if (CMAKE_BUILD_TYPE MATCHES DEBUG)
    add_definitions(-DCHARON_DEBUG)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

set(SOURCE_FILES
    src/server.c
    src/http_parser.c
    src/main.c
    src/client.c
    src/chain.c
)

add_executable(charon ${SOURCE_FILES})
