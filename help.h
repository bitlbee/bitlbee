  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Help file control                                                    */

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

#ifndef _HELP_H
#define _HELP_H

typedef union
{
	off_t file_offset;
	char *mem_offset;
} help_off_t;

typedef struct help
{
	int fd;
	time_t mtime;
	char *title;
	help_off_t offset;
	int length;
	struct help *next;
} help_t;

G_GNUC_MALLOC help_t *help_init( help_t **help, const char *helpfile );
void help_free( help_t **help );
char *help_get( help_t **help, char *title );

#endif
