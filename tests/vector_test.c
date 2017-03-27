#include <check.h>

#include "utils/vector.h"

START_TEST(int_test)
{
    VECTOR_DEFINE(v, int);
    vector_init(&v);
    for (int i = 0; i < 10; i++) {
        vector_push(&v, &i, int);
    }
    for (int i = 0; i < 10; i++) {
        ck_assert_int_eq(v[i], i);
    }
    vector_destroy(&v);
}
END_TEST

START_TEST(struct_test)
{
    struct my_data {
        int x;
        float y;
    };

    VECTOR_DEFINE(v, struct my_data);
    vector_init(&v);
    for (int i = 0; i < 10; i++) {
        struct my_data data = { i, 2.0f * i / 3 };
        vector_push(&v, &data, struct my_data);
    }
    for (int i = 0; i < 10; i++) {
        ck_assert_int_eq(v[i].x, i);
        ck_assert_float_eq(v[i].y, 2.0f * i / 3);
    }
    vector_destroy(&v);
}
END_TEST

START_TEST(set_int_test)
{
    VECTOR_DEFINE(v, int);
    vector_init(&v);
    for (int i = 0; i < 10; i++) {
        vector_set(&v, i, &i, int);
    }
    for (int i = 0; i < 10; i++) {
        ck_assert_int_eq(v[i], i);
    }
    vector_destroy(&v);
}
END_TEST

START_TEST(set_struct_test)
{
    struct my_data {
        int x;
        float y;
    };

    VECTOR_DEFINE(v, struct my_data);
    vector_init(&v);
    for (int i = 0; i < 10; i++) {
        struct my_data data = { i, 2.0f * i / 3 };
        vector_push(&v, &data, struct my_data);
    }
    for (int i = 0; i < 10; i++) {
        ck_assert_int_eq(v[i].x, i);
        ck_assert_float_eq(v[i].y, 2.0f * i / 3);
    }
    vector_destroy(&v);
}
END_TEST

Suite* http_parser_suite()
{
    Suite* s;
    TCase* tc_core;
    s = suite_create("Vector");
    tc_core = tcase_create("CommonTests");
    tcase_add_test(tc_core, int_test);
    tcase_add_test(tc_core, struct_test);
    tcase_add_test(tc_core, set_int_test);
    tcase_add_test(tc_core, set_struct_test);
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
