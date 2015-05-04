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
  if not, write to the Free Software Foundation, Inc., 51 Franklin St.,
  Fifth Floor, Boston, MA  02110-1301  USA
*/

void nick_set_raw(account_t *acc, const char *handle, const char *nick);
void nick_set(bee_user_t *bu, const char *nick);
char *nick_get(bee_user_t *bu);
char *nick_gen(bee_user_t *bu);
void underscore_dedupe(char nick[MAX_NICK_LENGTH + 1]);
void nick_dedupe(bee_user_t * bu, char nick[MAX_NICK_LENGTH + 1]);
int nick_saved(bee_user_t *bu);
void nick_del(bee_user_t *bu);

void nick_strip(irc_t *irc, char *nick);
gboolean nick_ok(irc_t *irc, const char *nick);
int nick_lc(irc_t *irc, char *nick);
int nick_uc(irc_t *irc, char *nick);
int nick_cmp(irc_t *irc, const char *a, const char *b);
char *nick_dup(const char *nick);
