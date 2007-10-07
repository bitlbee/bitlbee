#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>
#include <check.h>
#include <string.h>
#include <stdio.h>
#include "arc.h"

char *password = "TotT";

char *clear_tests[] =
{
	"Wie dit leest is gek :-)",
	"ItllBeBitlBee",
	"One more boring password",
	NULL
};

static void check_codec(int l)
{
	int i;
	
	for( i = 0; clear_tests[i]; i++ )
	{
  		tcase_fn_start (clear_tests[i], __FILE__, __LINE__);
		unsigned char *crypted;
		char *decrypted;
		int len;
		
		len = arc_encode( clear_tests[i], 0, &crypted, password );
		len = arc_decode( crypted, len, &decrypted, password );
		
		fail_if( strcmp( clear_tests[i], decrypted ) != 0,
		         "%s didn't decrypt back properly", clear_tests[i] );
		
		g_free( crypted );
		g_free( decrypted );
	}
}

struct
{
	const unsigned char crypted[24];
	int len;
	char *decrypted;
} decrypt_tests[] = {
	{
		{
			0xc3, 0x0d, 0x43, 0xc3, 0xee, 0x80, 0xe2, 0x8c, 0x0b, 0x29, 0x32, 0x7e,
			0x38, 0x05, 0x82, 0x10, 0x21, 0x1c, 0x4a, 0x00, 0x2c
		}, 21, "Debugging sucks"
	},
	{
		{
			0xb0, 0x00, 0x57, 0x0d, 0x0d, 0x0d, 0x70, 0xe1, 0xc0, 0x00, 0xa4, 0x25,
			0x7d, 0xbe, 0x03, 0xcc, 0x24, 0xd1, 0x0c
		}, 19, "Testing rocks"
	},
	{
		{
			0xb6, 0x92, 0x59, 0xe4, 0xf9, 0xc1, 0x7a, 0xf6, 0xf3, 0x18, 0xea, 0x28,
			0x73, 0x6d, 0xb3, 0x0a, 0x6f, 0x0a, 0x2b, 0x43, 0x57, 0xe9, 0x3e, 0x63
		}, 24, "OSCAR is creepy..."
	}
};

static void check_decod(int l)
{
	int i;
	
	for( i = 0; clear_tests[i]; i++ )
	{
  		tcase_fn_start (decrypt_tests[i].decrypted, __FILE__, __LINE__);
		char *decrypted;
		int len;
		
		len = arc_decode( decrypt_tests[i].crypted, decrypt_tests[i].len,
		                  &decrypted, password );
		
		fail_if( strcmp( decrypt_tests[i].decrypted, decrypted ) != 0,
		         "%s didn't decrypt properly", clear_tests[i] );
		
		g_free( decrypted );
	}
}

Suite *arc_suite (void)
{
	Suite *s = suite_create("ArcFour");
	TCase *tc_core = tcase_create("Core");
	suite_add_tcase (s, tc_core);
	tcase_add_test (tc_core, check_codec);
	tcase_add_test (tc_core, check_decod);
	return s;
}
