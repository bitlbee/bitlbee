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

#ifndef __SET_H__
#define __SET_H__

struct set;

/* This used to be specific to irc_t structures, but it's more generic now
   (so it can also be used for account_t structs). It's pretty simple, but
   so far pretty useful.
   
   In short, it just keeps a linked list of settings/variables and it also
   remembers a default value for every setting. And to prevent the user
   from setting invalid values, you can write an evaluator function for
   every setting, which can check a new value and block it by returning
   NULL, or replace it by returning a new value. See struct set.eval. */

typedef char *(*set_eval) ( struct set *set, char *value );

typedef struct set
{
	void *data;     /* Here you can save a pointer to the
	                   object this settings belongs to. */
	
	char *key;
	char *value;
	char *def;      /* Default value. If the set_setstr() function
	                   notices a new value is exactly the same as
	                   the default, value gets set to NULL. So when
	                   you read a setting, don't forget about this! */
	
	int flags;      /* See account.h, for example. set.c doesn't use
	                   this (yet?). */
	
	/* Eval: Returns NULL if the value is incorrect or exactly the
	   passed value variable. When returning a corrected value,
	   set_setstr() should be able to free() the returned string! */
	set_eval eval;
	struct set *next;
} set_t;

/* Should be pretty clear. */
set_t *set_add( set_t **head, char *key, char *def, set_eval eval, void *data );

/* Returns the raw set_t. Might be useful sometimes. */
set_t *set_find( set_t **head, char *key );

/* Returns a pointer to the string value of this setting. Don't modify the
   returned string, and don't free() it! */
G_MODULE_EXPORT char *set_getstr( set_t **head, char *key );

/* Get an integer. Right now this also converts true/false/on/off/etc to
   numbers, but this is for historical reasons, please use set_getbool()
   for booleans instead. */
G_MODULE_EXPORT int set_getint( set_t **head, char *key );
G_MODULE_EXPORT int set_getbool( set_t **head, char *key );

/* set_setstr() strdup()s the given value, so after using this function
   you can free() it, if you want. */
int set_setstr( set_t **head, char *key, char *value );
int set_setint( set_t **head, char *key, int value );
void set_del( set_t **head, char *key );

/* Two very useful generic evaluators. */
char *set_eval_int( set_t *set, char *value );
char *set_eval_bool( set_t *set, char *value );

/* Some not very generic evaluators that really shouldn't be here... */
char *set_eval_to_char( set_t *set, char *value );
char *set_eval_ops( set_t *set, char *value );
char *set_eval_charset( set_t *set, char *value );

#endif /* __SET_H__ */
