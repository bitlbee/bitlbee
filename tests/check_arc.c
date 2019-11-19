#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>
#include <check.h>
#include <string.h>
#include <stdio.h>
#include "arc.h"

char *password = "ArcVier";

char *clear_tests[] =
{
	"Wie dit leest is gek :-)",
	"ItllBeBitlBee",
	"One more boring password",
	"Hoi hoi",
	NULL
};

START_TEST(check_codec)
{
	int i;

	for (i = 0; clear_tests[i]; i++) {
		tcase_fn_start(clear_tests[i], __FILE__, __LINE__);
		unsigned char *crypted;
		char *decrypted;
		int len;

		len = arc_encode(clear_tests[i], 0, &crypted, password, 12);
		len = arc_decode(crypted, len, &decrypted, password);

		fail_if(strcmp(clear_tests[i], decrypted) != 0,
		        "%s didn't decrypt back properly", clear_tests[i]);

		g_free(crypted);
		g_free(decrypted);
	}
}
END_TEST

struct {
	unsigned char crypted[30];
	int len;
	char *decrypted;
} decrypt_tests[] = {
	/* One block with padding. */
	{
		{
			0x3f, 0x79, 0xb0, 0xf5, 0x91, 0x56, 0xd2, 0x1b, 0xd1, 0x4b, 0x67, 0xac,
			0xb1, 0x31, 0xc9, 0xdb, 0xf9, 0xaa
		}, 18, "short pass"
	},

	/* Two blocks with padding. */
	{
		{
			0xf9, 0xa6, 0xec, 0x5d, 0xc7, 0x06, 0xb8, 0x6b, 0x63, 0x9f, 0x2d, 0xb5,
			0x7d, 0xaa, 0x32, 0xbb, 0xd8, 0x08, 0xfd, 0x81, 0x2e, 0xca, 0xb4, 0xd7,
			0x2f, 0x36, 0x9c, 0xac, 0xa0, 0xbc
		}, 30, "longer password"
	},

	/* This string is exactly two "blocks" long, to make sure unpadded strings also decrypt
	   properly. */
	{
		{
			0x95, 0x4d, 0xcf, 0x4d, 0x5e, 0x6c, 0xcf, 0xef, 0xb9, 0x80, 0x00, 0xef,
			0x25, 0xe9, 0x17, 0xf6, 0x29, 0x6a, 0x82, 0x79, 0x1c, 0xca, 0x68, 0xb5,
			0x4e, 0xd0, 0xc1, 0x41, 0x8e, 0xe6
		}, 30, "OSCAR is really creepy.."
	},
	{ "", 0, NULL }
};

START_TEST(check_decod)
{
	int i;

	for (i = 0; decrypt_tests[i].len; i++) {
		tcase_fn_start(decrypt_tests[i].decrypted, __FILE__, __LINE__);
		char *decrypted;
		int len;

		len = arc_decode(decrypt_tests[i].crypted, decrypt_tests[i].len,
		                 &decrypted, password);

		fail_if(len == -1,
		        "`%s' didn't decrypt properly", decrypt_tests[i].decrypted);
		fail_if(strcmp(decrypt_tests[i].decrypted, decrypted) != 0,
		        "`%s' didn't decrypt properly", decrypt_tests[i].decrypted);

		g_free(decrypted);
	}
}
END_TEST

Suite *arc_suite(void)
{
	Suite *s = suite_create("ArcFour");
	TCase *tc_core = tcase_create("Core");

	suite_add_tcase(s, tc_core);
	tcase_add_test(tc_core, check_codec);
	tcase_add_test(tc_core, check_decod);
	return s;
}
