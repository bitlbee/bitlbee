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

typedef enum {
	STORAGE_OK = 0,
	STORAGE_NO_SUCH_USER,
	STORAGE_INVALID_PASSWORD,
	STORAGE_ALREADY_EXISTS,
	STORAGE_OTHER_ERROR /* Error that isn't caused by user input, such as 
	                       a database that is unreachable. log() will be 
	                       used for the exact error message */
} storage_status_t;

typedef struct {
	const char *name;
	
	/* May be set to NULL if not required */
	void (*init) (void);

	storage_status_t (*check_pass) (const char *nick, const char *password);

	storage_status_t (*load) (irc_t *irc, const char *password);
	storage_status_t (*save) (irc_t *irc, int overwrite);
	storage_status_t (*remove) (const char *nick, const char *password);

	/* May be NULL if not supported by backend */
	storage_status_t (*rename) (const char *onick, const char *nnick, const char *password);
} storage_t;

storage_status_t storage_check_pass (const char *nick, const char *password);

storage_status_t storage_load (irc_t * irc, const char *password);
storage_status_t storage_save (irc_t *irc, char *password, int overwrite);
storage_status_t storage_remove (const char *nick, const char *password);

/* storage_status_t storage_rename (const char *onick, const char *nnick, const char *password); */

void register_storage_backend(storage_t *);
G_GNUC_MALLOC GList *storage_init(const char *primary, char **migrate);

#endif /* __STORAGE_H__ */
