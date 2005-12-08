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

void setpassnc (irc_t *irc, const char *pass); /* USE WITH CAUTION! */
char *passchange (irc_t *irc, void *set, const char *value);
int setpass (irc_t *irc, const char *pass, const char* md5sum);
char *hashpass (irc_t *irc);
char *obfucrypt (irc_t *irc, char *line);
char *deobfucrypt (irc_t *irc, char *line);
