#include <check.h>

#include "http.h"
#include "http/parser.h"

typedef struct {
    http_parser_t parser;
    buffer_t buf;
    char* request;
    size_t request_size;

} http_parser_test_env_t;

http_parser_test_env_t* setup(char* request, size_t sz)
{
    http_parser_test_env_t* env = malloc(sizeof(http_parser_test_env_t));
    env->buf.start = request;
    env->buf.end = request + sz;
    http_parser_init(&env->parser);
    return env;
}

void teardown(http_parser_test_env_t* env)
{
    http_parser_destroy(&env->parser);
    free(env);
}

// START_TEST(simple_test)
// {
//     char request[] = "GET / HTTP/1.1\r\n\r\n";

//     http_parser_test_env_t* env = setup(request, sizeof(request));

//     ck_assert(http_parser_feed(&env->parser, &env->buf, &env->req) == CHARON_OK);
//     ck_assert(env->req.method == HTTP_GET);
//     ck_assert(!string_cmpl(&env->req.uri.path, "/"));
//     charon_debug("%.*s", (int)string_size(&env->req.uri.path), env->req.uri.path.start);

//     teardown(env);
// }
// END_TEST

START_TEST(server_request_test)
{
    char request[] = "HTTP/1.1 200 Ok\r\nServer: charon\r\n\r\n";
    http_parser_test_env_t* env = setup(request, sizeof(request));
    http_status_t status = 0;
    string_t status_message;
    http_version_t version;
    http_header_t header;

    env->parser.state = st_spaces_http_version;
    ck_assert(http_parse_status_line(&env->parser, &env->buf, &status, &status_message, &version) == HTTP_PARSER_DONE);

    charon_debug("status: %d", status);
    charon_debug("status_message: %.*s", (int)string_size(&status_message), status_message.start);
    charon_debug("http_version: major=%d, minor=%d", version.major, version.minor);

    while (http_parse_header(&env->parser, &env->buf, &header) == HTTP_PARSER_OK) {
        charon_debug("parsed header name='%.*s' value='%.*s'",
                (int)string_size(&header.name), header.name.start,
                (int)string_size(&header.value), header.value.start
        );
    }

    teardown(env);
}
END_TEST

Suite* http_parser_suite()
{
    Suite* s;
    TCase* tc_core;
    s = suite_create("HttpParser");
    tc_core = tcase_create("CommonTests");
    // tcase_add_test(tc_core, simple_test);
    tcase_add_test(tc_core, server_request_test);
    suite_add_tcase(s, tc_core);
    return s;
}

int main()
{
    int failed = 0;
    Suite* suite;
    SRunner* sr;

    suite = http_parser_suite();
    sr = srunner_create(suite);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? 0 : 1;
}
