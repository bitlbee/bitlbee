/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2008 Wilmer van der Gaast and others                *
  \********************************************************************/

/* INI file reading code						*/

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
#define BITLBEE_CORE
#include "bitlbee.h"

ini_t *ini_open(char *file)
{
	int fd;
	ini_t *ini = NULL;
	struct stat fi;

	if ((fd = open(file, O_RDONLY)) != -1 &&
	    fstat(fd, &fi) == 0 &&
	    fi.st_size <= 16384 &&
	    (ini = g_malloc(sizeof(ini_t) + fi.st_size + 1)) &&
	    read(fd, ini->file, fi.st_size) == fi.st_size) {
		memset(ini, 0, sizeof(ini_t));
		ini->size = fi.st_size;
		ini->file[ini->size] = 0;
		ini->cur = ini->file;
		ini->c_section = "";

		close(fd);

		return ini;
	}

	if (fd >= 0) {
		close(fd);
	}

	ini_close(ini);

	return NULL;
}

/* Strips leading and trailing whitespace and returns a pointer to the first
   non-ws character of the given string. */
static char *ini_strip_whitespace(char *in)
{
	char *e;

	while (g_ascii_isspace(*in)) {
		in++;
	}

	e = in + strlen(in) - 1;
	while (e > in && g_ascii_isspace(*e)) {
		e--;
	}
	e[1] = 0;

	return in;
}

int ini_read(ini_t *file)
{
	char *s;

	while (file->cur && file->cur < file->file + file->size) {
		char *e, *next;

		file->line++;

		/* Find the end of line */
		if ((e = strchr(file->cur, '\n')) != NULL) {
			*e = 0;
			next = e + 1;
		} else {
			/* No more lines. */
			e = file->cur + strlen(file->cur);
			next = NULL;
		}

		/* Comment? */
		if ((s = strchr(file->cur, '#')) != NULL) {
			*s = 0;
		}

		file->cur = ini_strip_whitespace(file->cur);

		if (*file->cur == '[') {
			file->cur++;
			if ((s = strchr(file->cur, ']')) != NULL) {
				*s = 0;
				file->c_section = file->cur;
			}
		} else if ((s = strchr(file->cur, '=')) != NULL) {
			*s = 0;
			file->key = ini_strip_whitespace(file->cur);
			file->value = ini_strip_whitespace(s + 1);

			if ((s = strchr(file->key, '.')) != NULL) {
				*s = 0;
				file->section = file->key;
				file->key = s + 1;
			} else {
				file->section = file->c_section;
			}

			file->cur = next;
			return 1;
		}
		/* else: noise/comment/etc, let's just ignore it. */

		file->cur = next;
	}

	return 0;
}

void ini_close(ini_t *file)
{
	g_free(file);
}
