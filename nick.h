  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Some stuff to fetch, save and handle nicknames for your buddies      */

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

void nick_set( account_t *acc, const char *handle, const char *nick );
char *nick_get( account_t *acc, const char *handle );
void nick_dedupe( account_t *acc, const char *handle, char nick[MAX_NICK_LENGTH+1] );
int nick_saved( account_t *acc, const char *handle );
void nick_del( account_t *acc, const char *handle );
void nick_strip( char *nick );

int nick_ok( const char *nick );
int nick_lc( char *nick );
int nick_uc( char *nick );
int nick_cmp( const char *a, const char *b );
char *nick_dup( const char *nick );
