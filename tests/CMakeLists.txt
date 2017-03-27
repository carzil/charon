find_package(Check REQUIRED)

include_directories(
    ${CHECK_INCLUDE_LIBS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
    ${CMAKE_CURRENT_BINARY_DIR}/src/include
)

function(charon_test)
    set(oneValueArgs NAME)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(ETEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN} )
    add_executable(${ETEST_NAME} ${ETEST_SOURCES})
    target_link_libraries(${ETEST_NAME} ${CHECK_LIBRARIES})
    add_test(NAME "charon_${ETEST_NAME}" COMMAND ${ETEST_NAME})
endfunction()

charon_test(NAME http_parser_test SOURCES http_parser_test.c ${CMAKE_SOURCE_DIR}/src/http_parser.c ${CMAKE_SOURCE_DIR}/src/chain.c)
charon_test(NAME vector_test SOURCES vector_test.c)
