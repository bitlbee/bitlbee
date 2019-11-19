#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>
#include <check.h>
#include <string.h>
#include "set.h"
#include "testsuite.h"

START_TEST(test_set_add)
{
    void *data = "data";
    set_t *s = NULL, *t;
    t = set_add(&s, "name", "default", NULL, data);
    fail_unless(s == t);
    fail_unless(t->data == data);
    fail_unless(strcmp(t->def, "default") == 0);
}
END_TEST

START_TEST(test_set_add_existing)
{
    void *data = "data";
    set_t *s = NULL, *t;
    t = set_add(&s, "name", "default", NULL, data);
    t = set_add(&s, "name", "newdefault", NULL, data);
    fail_unless(s == t);
    fail_unless(strcmp(t->def, "newdefault") == 0);
}
END_TEST

START_TEST(test_set_find_unknown)
{
    set_t * s = NULL;
    fail_unless(set_find(&s, "foo") == NULL);
}
END_TEST

START_TEST(test_set_find)
{
    void *data = "data";
    set_t *s = NULL, *t;
    t = set_add(&s, "name", "default", NULL, data);
    fail_unless(s == t);
    fail_unless(set_find(&s, "name") == t);
}
END_TEST

START_TEST(test_set_get_str_default)
{
    void *data = "data";
    set_t *s = NULL, *t;
    t = set_add(&s, "name", "default", NULL, data);
    fail_unless(s == t);
    fail_unless(strcmp(set_getstr(&s, "name"), "default") == 0);
}
END_TEST

START_TEST(test_set_get_bool_default)
{
    void *data = "data";
    set_t *s = NULL, *t;
    t = set_add(&s, "name", "true", NULL, data);
    fail_unless(s == t);
    fail_unless(set_getbool(&s, "name"));
}
END_TEST

START_TEST(test_set_get_bool_integer)
{
    void *data = "data";
    set_t *s = NULL, *t;
    t = set_add(&s, "name", "3", NULL, data);
    fail_unless(s == t);
    fail_unless(set_getbool(&s, "name") == 3);
}
END_TEST

START_TEST(test_set_get_bool_unknown)
{
    set_t * s = NULL;
    fail_unless(set_getbool(&s, "name") == 0);
}
END_TEST

START_TEST(test_set_get_str_value)
{
    void *data = "data";
    set_t *s = NULL;
    set_add(&s, "name", "default", NULL, data);
    set_setstr(&s, "name", "foo");
    fail_unless(strcmp(set_getstr(&s, "name"), "foo") == 0);
}
END_TEST

START_TEST(test_set_get_str_unknown)
{
    set_t * s = NULL;
    fail_unless(set_getstr(&s, "name") == NULL);
}
END_TEST

START_TEST(test_setint)
{
    void *data = "data";
    set_t *s = NULL;
    set_add(&s, "name", "10", NULL, data);
    set_setint(&s, "name", 3);
    fail_unless(set_getint(&s, "name") == 3);
}
END_TEST

START_TEST(test_setstr)
{
    void *data = "data";
    set_t *s = NULL;
    set_add(&s, "name", "foo", NULL, data);
    set_setstr(&s, "name", "bloe");
    fail_unless(strcmp(set_getstr(&s, "name"), "bloe") == 0);
}
END_TEST

START_TEST(test_set_get_int_unknown)
{
    set_t * s = NULL;
    fail_unless(set_getint(&s, "foo") == 0);
}
END_TEST

Suite *set_suite(void)
{
	Suite *s = suite_create("Set");
	TCase *tc_core = tcase_create("Core");

	suite_add_tcase(s, tc_core);
	tcase_add_test(tc_core, test_set_add);
	tcase_add_test(tc_core, test_set_add_existing);
	tcase_add_test(tc_core, test_set_find_unknown);
	tcase_add_test(tc_core, test_set_find);
	tcase_add_test(tc_core, test_set_get_str_default);
	tcase_add_test(tc_core, test_set_get_str_value);
	tcase_add_test(tc_core, test_set_get_str_unknown);
	tcase_add_test(tc_core, test_set_get_bool_default);
	tcase_add_test(tc_core, test_set_get_bool_integer);
	tcase_add_test(tc_core, test_set_get_bool_unknown);
	tcase_add_test(tc_core, test_set_get_int_unknown);
	tcase_add_test(tc_core, test_setint);
	tcase_add_test(tc_core, test_setstr);
	return s;
}
