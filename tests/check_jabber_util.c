#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>
#include <check.h>
#include <string.h>
#include <stdio.h>
#include "jabber/jabber.h"

static struct im_connection *ic;

static void check_buddy_add(int l)
{
	struct jabber_buddy *budw1, *budw2, *budw3, *budn, *bud;
	
	budw1 = jabber_buddy_add( ic, "wilmer@gaast.net/BitlBee" );
	budw1->last_msg = time( NULL ) - 100;
	budw2 = jabber_buddy_add( ic, "WILMER@gaast.net/Telepathy" );
	budw2->priority = 2;
	budw2->last_msg = time( NULL );
	budw3 = jabber_buddy_add( ic, "wilmer@GAAST.NET/bitlbee" );
	budw3->last_msg = time( NULL ) - 200;
	budw3->priority = 4;
	/* TODO(wilmer): Shouldn't this just return budw3? */
	fail_if( jabber_buddy_add( ic, "wilmer@gaast.net/Telepathy" ) != NULL );
	
	budn = jabber_buddy_add( ic, "nekkid@lamejab.net" );
	/* Shouldn't be allowed if there's already a bare JID. */
	fail_if( jabber_buddy_add( ic, "nekkid@lamejab.net/Illegal" ) );
	
	/* Case sensitivity: Case only matters after the / */
	fail_if( jabber_buddy_by_jid( ic, "wilmer@gaast.net/BitlBee", 0 ) ==
	         jabber_buddy_by_jid( ic, "wilmer@gaast.net/bitlbee", 0 ) );
	fail_if( jabber_buddy_by_jid( ic, "wilmer@gaast.net/telepathy", 0 ) );
	
	fail_unless( jabber_buddy_by_jid( ic, "wilmer@gaast.net/BitlBee", 0 ) == budw1 );
	fail_unless( jabber_buddy_by_jid( ic, "WILMER@GAAST.NET/BitlBee", GET_BUDDY_EXACT ) == budw1 );
	fail_unless( jabber_buddy_by_jid( ic, "wilmer@GAAST.NET/BitlBee", GET_BUDDY_CREAT ) == budw1 );

	fail_if( jabber_buddy_by_jid( ic, "wilmer@gaast.net", GET_BUDDY_EXACT ) );
	fail_unless( jabber_buddy_by_jid( ic, "WILMER@gaast.net", 0 ) == budw3 );

	/* Check O_FIRST and see if it's indeed the first item from the list. */
	fail_unless( ( bud = jabber_buddy_by_jid( ic, "wilmer@gaast.net", GET_BUDDY_FIRST ) ) == budw1 );
	fail_unless( bud->next == budw2 && bud->next->next == budw3 && bud->next->next->next == NULL );
	
	/* Change the resource_select setting, now we should get a different resource. */
	set_setstr( &ic->acc->set, "resource_select", "activity" );
	fail_unless( jabber_buddy_by_jid( ic, "wilmer@GAAST.NET", 0 ) == budw2 );
	
	/* Some testing of bare JID handling (which is horrible). */
	fail_if( jabber_buddy_by_jid( ic, "nekkid@lamejab.net/Illegal", 0 ) );
	fail_if( jabber_buddy_by_jid( ic, "NEKKID@LAMEJAB.NET/Illegal", GET_BUDDY_CREAT ) );
	fail_unless( jabber_buddy_by_jid( ic, "nekkid@lamejab.net", 0 ) == budn );
	fail_unless( jabber_buddy_by_jid( ic, "NEKKID@lamejab.net", GET_BUDDY_EXACT ) == budn );
	fail_unless( jabber_buddy_by_jid( ic, "nekkid@LAMEJAB.NET", GET_BUDDY_CREAT ) == budn );
	
	/* More case sensitivity testing, and see if remove works properly. */
	fail_if( jabber_buddy_remove( ic, "wilmer@gaast.net/telepathy" ) );
	fail_if( jabber_buddy_by_jid( ic, "wilmer@GAAST.NET/telepathy", GET_BUDDY_CREAT ) == budw2 );
	fail_unless( jabber_buddy_remove( ic, "wilmer@gaast.net/Telepathy" ) );
	fail_unless( jabber_buddy_remove( ic, "wilmer@gaast.net/telepathy" ) );
	fail_unless( jabber_buddy_by_jid( ic, "wilmer@gaast.net", 0 ) == budw1 );
	
	fail_if( jabber_buddy_remove( ic, "wilmer@gaast.net" ) );
	fail_unless( jabber_buddy_by_jid( ic, "wilmer@gaast.net", 0 ) == budw1 );
	
	/* Check if remove_bare() indeed gets rid of all. */
	fail_unless( jabber_buddy_remove_bare( ic, "wilmer@gaast.net" ) );
	fail_if( jabber_buddy_by_jid( ic, "wilmer@gaast.net", 0 ) );

	fail_if( jabber_buddy_remove( ic, "nekkid@lamejab.net/Illegal" ) );
	fail_unless( jabber_buddy_remove( ic, "nekkid@lamejab.net" ) );
	fail_if( jabber_buddy_by_jid( ic, "nekkid@lamejab.net", 0 ) );
}

Suite *jabber_util_suite (void)
{
	Suite *s = suite_create("jabber/util");
	TCase *tc_core = tcase_create("Buddy");
	struct jabber_data *jd;
	
	ic = g_new0( struct im_connection, 1 );
	ic->acc = g_new0( account_t, 1 );
	ic->proto_data = jd = g_new0( struct jabber_data, 1 );
	jd->buddies = g_hash_table_new( g_str_hash, g_str_equal );
	set_add( &ic->acc->set, "resource_select", "priority", NULL, ic->acc );
	
	suite_add_tcase (s, tc_core);
	tcase_add_test (tc_core, check_buddy_add);
	return s;
}
