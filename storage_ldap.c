  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Storage backend that uses a LDAP database */

/* Copyright (C) 2006 Jelmer Vernooij <jelmer@samba.org> */

/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License with
  the Debian GNU/Linux distribution in /usr/share/common-licenses/GPL;
  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
  Suite 330, Boston, MA  02111-1307  USA
*/

#define BITLBEE_CORE
#include "bitlbee.h"
#include <ldap.h>

#define BB_LDAP_HOST "localhost"
#define BB_LDAP_BASE ""

static char *nick_dn(const char *nick)
{
	return g_strdup_printf("bitlBeeNick=%s%s%s", nick, BB_LDAP_BASE?",":"", BB_LDAP_BASE?BB_LDAP_BASE:"");
}

static storage_status_t nick_connect(const char *nick, const char *password, LDAP **ld)
{
	char *mydn;
	int ret;
	storage_status_t status;
	*ld = ldap_init(BB_LDAP_HOST, LDAP_PORT);

	if (!ld) {
		log_message( LOGLVL_WARNING, "Unable to connect to LDAP server at %s", BB_LDAP_HOST );
		return STORAGE_OTHER_ERROR;
	}

	mydn = nick_dn(nick);

	ret = ldap_simple_bind_s(*ld, mydn, password);

	switch (ret) {
	 case LDAP_SUCCESS: status = STORAGE_OK; break;
	 case LDAP_INVALID_CREDENTIALS: status = STORAGE_INVALID_PASSWORD; break;
	 default: 
		log_message( LOGLVL_WARNING, "Unable to authenticate %s: %s", mydn, ldap_err2string(ret) );
		status = STORAGE_OTHER_ERROR;
		break;
	}

	g_free(mydn);

	return status;
}

static storage_status_t sldap_load ( const char *my_nick, const char* password, irc_t *irc )
{
	LDAPMessage *res, *msg;
	LDAP *ld;
	int ret, i;
	storage_status_t status;
	char *mydn; 

	status = nick_connect(my_nick, password, &ld);
	if (status != STORAGE_OK)
		return status;

	mydn = nick_dn(my_nick);

	ret = ldap_search_s(ld, mydn, LDAP_SCOPE_BASE, "(objectClass=*)", NULL, 0, &res);

	if (ret != LDAP_SUCCESS) {
		log_message( LOGLVL_WARNING, "Unable to search for %s: %s", mydn, ldap_err2string(ret) );
		ldap_unbind_s(ld);
		return STORAGE_OTHER_ERROR;
	}

	g_free(mydn);

	for (msg = ldap_first_entry(ld, res); msg; msg = ldap_next_entry(ld, msg)) {
	}

	/* FIXME: Store in irc_t */

	ldap_unbind_s(ld);
	
	return STORAGE_OK;
}

static storage_status_t sldap_check_pass( const char *nick, const char *password )
{
	LDAP *ld;
	storage_status_t status;

	status = nick_connect(nick, password, &ld);

	ldap_unbind_s(ld);

	return status;
}

static storage_status_t sldap_remove( const char *nick, const char *password )
{
	storage_status_t status;
	LDAP *ld;
	char *mydn;
	int ret;
	
	status = nick_connect(nick, password, &ld);

	if (status != STORAGE_OK)
		return status;

	mydn = nick_dn(nick);
	
	ret = ldap_delete(ld, mydn);

	if (ret != LDAP_SUCCESS) {
		log_message( LOGLVL_WARNING, "Error removing %s: %s", mydn, ldap_err2string(ret) );
		ldap_unbind_s(ld);
		return STORAGE_OTHER_ERROR;
	}

	ldap_unbind_s(ld);

	g_free(mydn);
	return STORAGE_OK;
}

static storage_status_t sldap_save( irc_t *irc, int overwrite )
{
	LDAP *ld;
	char *mydn;
	storage_status_t status;
	LDAPMessage *msg;

	status = nick_connect(irc->nick, irc->password, &ld);
	if (status != STORAGE_OK)
		return status;

	mydn = nick_dn(irc->nick);

	/* FIXME: Make this a bit more atomic? What if we crash after 
	 * removing the old account but before adding the new one ? */
	if (overwrite) 
		sldap_remove(irc->nick, irc->password);

	g_free(mydn);

	ldap_unbind_s(ld);
	
	return STORAGE_OK;
}



storage_t storage_ldap = {
	.name = "ldap",
	.check_pass = sldap_check_pass,
	.remove = sldap_remove,
	.load = sldap_load,
	.save = sldap_save
};
