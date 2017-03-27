#include <check.h>

#include "http_parser.h"

START_TEST(simple_test)
{
    char request[] = "GET / HTTP/1.1\r\n\r\n";

    http_parser_t parser;
    http_request_t req;
    buffer_t buf;
    buf.start = request;
    buf.end = request + sizeof(request);
    http_parser_init(&parser);
    http_parser_feed(&parser, &buf, &req);
    ck_assert(req.method == HTTP_GET);
    ck_assert(!string_cmpl(&req.uri.path, "/"));
    charon_debug("%.*s", (int)string_size(&req.uri.path), req.uri.path.start);
    http_parser_destroy(&parser);
    http_request_destroy(&req);
}
END_TEST

Suite* http_parser_suite()
{
    Suite* s;
    TCase* tc_core;
    s = suite_create("HttpParser");
    tc_core = tcase_create("CommonTests");
    tcase_add_test(tc_core, simple_test);
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
