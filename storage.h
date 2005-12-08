  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Layer for retrieving and storing buddy information */

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

#ifndef __STORAGE_H__
#define __STORAGE_H__

typedef struct {
	const char *name;
	
	/* May be set to NULL if not required */
	void (*init) (void);

	int (*load) (const char *nick, const char *password, irc_t * irc);
	int (*exists) (const char *nick);
	int (*save) (irc_t *irc);
	int (*remove) (const char *nick);
	int (*check_pass) (const char *nick, const char *pass);

	/* May be NULL if not supported by backend */
	int (*rename) (const char *onick, const char *nnick, const char *password);
} storage_t;

void register_storage_backend(storage_t *);
storage_t *storage_init(const char *name);

#endif /* __STORAGE_H__ */
