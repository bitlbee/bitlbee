  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Sjoerd Hemminga and others                     *
  \********************************************************************/

/* A little bit of encryption for the users' passwords                  */

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

/* [WvG] This file can also be compiled into a stand-alone program
   which can encode/decode BitlBee account files. The main() will be
   included if CRYPTING_MAIN is defined. Or just do "make decode" and
   the programs will be built. */

#include <bitlbee.h>
#include "md5.h"
#include "crypting.h"

/*\
 * [SH] Do _not_ call this if it's not entirely sure that it will not cause
 * harm to another users file, since this does not check the password for
 * correctness.
\*/

int checkpass (const char *pass, const char *md5sum)
{
	md5_state_t md5state;
	md5_byte_t digest[16];
	int i, j;
	char digits[3];
	
	md5_init (&md5state);
	md5_append (&md5state, (unsigned char *)pass, strlen (pass));
	md5_finish (&md5state, digest);
	
	for (i = 0, j = 0; i < 16; i++, j += 2) {
		/* Check password for correctness */
		g_snprintf (digits, sizeof (digits), "%02x\n", digest[i]);
		
		if (digits[0] != md5sum[j]) return (-1);
		if (digits[1] != md5sum[j + 1]) return (-1);
	}

	return( 0 );
}


char *hashpass (const char *password)
{
	md5_state_t md5state;
	md5_byte_t digest[16];
	int i;
	char digits[3];
	char *rv;
	
	if (password == NULL) return (NULL);
	
	rv = g_new0 (char, 33);
	
	md5_init (&md5state);
	md5_append (&md5state, (const unsigned char *)password, strlen (password));
	md5_finish (&md5state, digest);
	
	for (i = 0; i < 16; i++) {
		/* Build a hash of the pass */
		g_snprintf (digits, sizeof (digits), "%02x", digest[i]);
		strcat (rv, digits);
	}
	
	return (rv);
}

char *obfucrypt (char *line, const char *password) 
{
	int i, j;
	char *rv;
	
	if (password == NULL) return (NULL);
	
	rv = g_new0 (char, strlen (line) + 1);
	
	i = j = 0;
	while (*line) {
		/* Encrypt/obfuscate the line, using the password */
		if (*(signed char*)line < 0) *line = - (*line);
		
		rv[j] = *line + password[i]; /* Overflow intended */
		
		line++;
		if (!password[++i]) i = 0;
		j++;
	}
	
	return (rv);
}

char *deobfucrypt (char *line, const char *password) 
{
	int i, j;
	char *rv;
	
	if (password == NULL) return (NULL);
	
	rv = g_new0 (char, strlen (line) + 1);
	
	i = j = 0;
	while (*line) {
		/* Decrypt/deobfuscate the line, using the pass */
		rv[j] = *line - password[i]; /* Overflow intended */
		
		line++;
		if (!password[++i]) i = 0;
		j++;
	}
	
	return (rv);
}
