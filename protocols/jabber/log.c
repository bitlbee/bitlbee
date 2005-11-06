/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  Jabber
 *  Copyright (C) 1998-1999 The Jabber Team http://jabber.org/
 */

#include "jabber.h"
#include "log.h"

#ifdef DEBUG

void jdebug(char *zone, const char *msgfmt, ...)
{
    va_list ap;
    static char loghdr[LOGSIZE_HDR];
    static char logmsg[LOGSIZE_TAIL];
    static int size;

    /* XXX: We may want to check the sizes eventually */
    size = g_snprintf(loghdr, LOGSIZE_HDR, "debug/%s %s\n", zone, msgfmt);

    va_start(ap, msgfmt);
    size = vsnprintf(logmsg, LOGSIZE_TAIL, loghdr, ap);

    fprintf(stderr,"%s",logmsg);

    return;
}


#endif  /* DEBUG */
