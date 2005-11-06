/*
 * libyahoo2: yahoo_util.c
 *
 * Copyright (C) 2002-2004, Philip S Tellis <philip.tellis AT gmx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if STDC_HEADERS
# include <string.h>
#else
# if !HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif
char *strchr (), *strrchr ();
# if !HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy ((s), (d), (n))
#  define memmove(d, s, n) bcopy ((s), (d), (n))
# endif
#endif

#include "yahoo_util.h"

char * y_string_append(char * string, char * append)
{
	int size = strlen(string) + strlen(append) + 1;
	char * new_string = y_renew(char, string, size);

	if(new_string == NULL) {
		new_string = y_new(char, size);
		strcpy(new_string, string);
		FREE(string);
	}

	strcat(new_string, append);

	return new_string;
}

char * y_str_to_utf8(const char *in)
{
	unsigned int n, i = 0;
	char *result = NULL;

	if(in == NULL || *in == '\0')
		return "";
	
	result = y_new(char, strlen(in) * 2 + 1);

	/* convert a string to UTF-8 Format */
	for (n = 0; n < strlen(in); n++) {
		unsigned char c = (unsigned char)in[n];

		if (c < 128) {
			result[i++] = (char) c;
		} else {
			result[i++] = (char) ((c >> 6) | 192);
			result[i++] = (char) ((c & 63) | 128);
		}
	}
	result[i] = '\0';
	return result;
}

char * y_utf8_to_str(const char *in)
{
	int i = 0;
	unsigned int n;
	char *result = NULL;

	if(in == NULL || *in == '\0')
		return "";
	
	result = y_new(char, strlen(in) + 1);

	/* convert a string from UTF-8 Format */
	for (n = 0; n < strlen(in); n++) {
		unsigned char c = in[n];

		if (c < 128) {
			result[i++] = (char) c;
		} else {
			result[i++] = (c << 6) | (in[++n] & 63);
		}
	}
	result[i] = '\0';
	return result;
}

#if !HAVE_GLIB

void y_strfreev(char ** vector)
{
	char **v;
	for(v = vector; *v; v++) {
		FREE(*v);
	}
	FREE(vector);
}

char ** y_strsplit(char * str, char * sep, int nelem)
{
	char ** vector;
	char *s, *p;
	int i=0;
	int l = strlen(sep);
	if(nelem < 0) {
		char * s;
		nelem=0;
		for(s=strstr(str, sep); s; s=strstr(s+l, sep),nelem++)
			;
		if(strcmp(str+strlen(str)-l, sep))
			nelem++;
	}

	vector = y_new(char *, nelem + 1);

	for(p=str, s=strstr(p,sep); i<nelem && s; p=s+l, s=strstr(p,sep), i++) {
		int len = s-p;
		vector[i] = y_new(char, len+1);
		strncpy(vector[i], p, len);
		vector[i][len] = '\0';
	}

	if(i<nelem) /* str didn't end with sep */
		vector[i++] = strdup(p);
			
	vector[i] = NULL;

	return vector;
}

void * y_memdup(const void * addr, int n)
{
	void * new_chunk = malloc(n);
	if(new_chunk)
		memcpy(new_chunk, addr, n);
	return new_chunk;
}

#endif
