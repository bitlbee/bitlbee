  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2006 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Some stuff to register, handle and save user preferences             */

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

typedef struct set
{
	void *data;
	
	char *key;
	char *value;
	char *def;	/* Default */
	
	int flags;
	
	/* Eval: Returns NULL if the value is incorrect or exactly the
	   passed value variable. When returning a corrected value,
	   set_setstr() should be able to free() the returned string! */
	char *(*eval) ( struct set *set, char *value );
	struct set *next;
} set_t;

set_t *set_add( set_t **head, char *key, char *def, void *eval, void *data );
set_t *set_find( set_t **head, char *key );
G_MODULE_EXPORT char *set_getstr( set_t **head, char *key );
G_MODULE_EXPORT int set_getint( set_t **head, char *key );
G_MODULE_EXPORT int set_getbool( set_t **head, char *key );
int set_setstr( set_t **head, char *key, char *value );
int set_setint( set_t **head, char *key, int value );
void set_del( set_t **head, char *key );

char *set_eval_int( set_t *set, char *value );
char *set_eval_bool( set_t *set, char *value );

char *set_eval_to_char( set_t *set, char *value );
char *set_eval_ops( set_t *set, char *value );
char *set_eval_charset( set_t *set, char *value );
