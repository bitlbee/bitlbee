/*
 * libyahoo2: yahoo_util.h
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

#ifndef __YAHOO_UTIL_H__
#define __YAHOO_UTIL_H__

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if HAVE_GLIB
# include <glib.h>

# define FREE(x)	if(x) {g_free(x); x=NULL;}

# define y_new		g_new
# define y_new0		g_new0
# define y_renew	g_renew

# define y_memdup	g_memdup
# define y_strsplit	g_strsplit
# define y_strfreev	g_strfreev
# ifndef strdup
#  define strdup	g_strdup
# endif
# ifndef strncasecmp
#  define strncasecmp	g_strncasecmp
#  define strcasecmp	g_strcasecmp
# endif

# define snprintf	g_snprintf
# define vsnprintf	g_vsnprintf

#else

# include <stdlib.h>
# include <stdarg.h>

# define FREE(x)		if(x) {free(x); x=NULL;}

# define y_new(type, n)		(type *)malloc(sizeof(type) * (n))
# define y_new0(type, n)	(type *)calloc((n), sizeof(type))
# define y_renew(type, mem, n)	(type *)realloc(mem, n)

void * y_memdup(const void * addr, int n);
char ** y_strsplit(char * str, char * sep, int nelem);
void y_strfreev(char ** vector);

int strncasecmp(const char * s1, const char * s2, size_t n);
int strcasecmp(const char * s1, const char * s2);

char * strdup(const char *s);

int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);

#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef MIN
#define MIN(x,y) ((x)<(y)?(x):(y))
#endif

#ifndef MAX
#define MAX(x,y) ((x)>(y)?(x):(y))
#endif

/* 
 * The following three functions return newly allocated memory.
 * You must free it yourself
 */
char * y_string_append(char * str, char * append);
char * y_str_to_utf8(const char * in);
char * y_utf8_to_str(const char * in);

#endif

