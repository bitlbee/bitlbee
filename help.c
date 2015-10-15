/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
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
  if not, write to the Free Software Foundation, Inc., 51 Franklin St.,
  Fifth Floor, Boston, MA  02110-1301  USA
*/

#define BITLBEE_CORE
#include "bitlbee.h"
#include "help.h"
#undef read
#undef write

#define BUFSIZE 2048

help_t *help_init(help_t **help, const char *helpfile)
{
	int i, buflen = 0;
	help_t *h;
	char *s, *t;
	time_t mtime;
	struct stat stat[1];

	*help = h = g_new0(help_t, 1);

	h->fd = open(helpfile, O_RDONLY);

	if (h->fd == -1) {
		g_free(h);
		return(*help = NULL);
	}

	if (fstat(h->fd, stat) != 0) {
		g_free(h);
		return(*help = NULL);
	}
	mtime = stat->st_mtime;

	s = g_new(char, BUFSIZE + 1);
	s[BUFSIZE] = 0;

	while (((i = read(h->fd, s + buflen, BUFSIZE - buflen)) > 0) ||
	       (i == 0 && strstr(s, "\n%\n"))) {
		buflen += i;
		memset(s + buflen, 0, BUFSIZE - buflen);
		if (!(t = strstr(s, "\n%\n")) || s[0] != '?') {
			/* FIXME: Clean up */
			help_free(help);
			g_free(s);
			return NULL;
		}
		i = strchr(s, '\n') - s;

		if (h->title) {
			h = h->next = g_new0(help_t, 1);
		}
		h->title = g_new(char, i);

		strncpy(h->title, s + 1, i - 1);
		h->title[i - 1] = 0;
		h->fd = (*help)->fd;
		h->offset.file_offset = lseek(h->fd, 0, SEEK_CUR) - buflen + i + 1;
		h->length = t - s - i - 1;
		h->mtime = mtime;

		buflen -= (t + 3 - s);
		t = g_strdup(t + 3);
		g_free(s);
		s = g_renew(char, t, BUFSIZE + 1);
		s[BUFSIZE] = 0;
	}

	g_free(s);

	return(*help);
}

void help_free(help_t **help)
{
	help_t *h, *oh;
	int last_fd = -1; /* Weak de-dupe */

	if (help == NULL || *help == NULL) {
		return;
	}

	h = *help;
	while (h) {
		if (h->fd != last_fd) {
			close(h->fd);
			last_fd = h->fd;
		}
		g_free(h->title);
		h = (oh = h)->next;
		g_free(oh);
	}

	*help = NULL;
}

char *help_get(help_t **help, char *title)
{
	time_t mtime;
	struct stat stat[1];
	help_t *h;

	for (h = *help; h; h = h->next) {
		if (h->title != NULL && g_strcasecmp(h->title, title) == 0) {
			break;
		}
	}
	if (h && h->length > 0) {
		char *s = g_new(char, h->length + 1);

		s[h->length] = 0;
		if (h->fd >= 0) {
			if (fstat(h->fd, stat) != 0) {
				g_free(s);
				return NULL;
			}
			mtime = stat->st_mtime;

			if (mtime > h->mtime) {
				g_free(s);
				return NULL;
			}

			if (lseek(h->fd, h->offset.file_offset, SEEK_SET) == -1 ||
			    read(h->fd, s, h->length) != h->length) {
				g_free(s);
				return NULL;
			}
		} else {
			strncpy(s, h->offset.mem_offset, h->length);
		}
		return s;
	}

	return NULL;
}

int help_add_mem(help_t **help, const char *title, const char *content)
{
	help_t *h, *l = NULL;

	for (h = *help; h; h = h->next) {
		if (g_strcasecmp(h->title, title) == 0) {
			return 0;
		}

		l = h;
	}

	if (l) {
		h = l->next = g_new0(struct help, 1);
	} else {
		*help = h = g_new0(struct help, 1);
	}
	h->fd = -1;
	h->title = g_strdup(title);
	h->length = strlen(content);
	h->offset.mem_offset = g_strdup(content);

	return 1;
}

char *help_get_whatsnew(help_t **help, int old)
{
	GString *ret = NULL;
	help_t *h;
	int v;

	for (h = *help; h; h = h->next) {
		if (h->title != NULL && strncmp(h->title, "whatsnew", 8) == 0 &&
		    sscanf(h->title + 8, "%x", &v) == 1 && v > old) {
			char *s = help_get(&h, h->title);
			if (ret == NULL) {
				ret = g_string_new(s);
			} else {
				g_string_append_printf(ret, "\n\n%s", s);
			}
			g_free(s);
		}
	}

	return ret ? g_string_free(ret, FALSE) : NULL;
}
