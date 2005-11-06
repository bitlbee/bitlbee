/* --------------------------------------------------------------------------
 *
 * License
 *
 * The contents of this file are subject to the Jabber Open Source License
 * Version 1.0 (the "JOSL").  You may not copy or use this file, in either
 * source code or executable form, except in compliance with the JOSL. You
 * may obtain a copy of the JOSL at http://www.jabber.org/ or at
 * http://www.opensource.org/.  
 *
 * Software distributed under the JOSL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied.  See the JOSL
 * for the specific language governing rights and limitations under the
 * JOSL.
 *
 * Copyrights
 * 
 * Portions created by or assigned to Jabber.com, Inc. are 
 * Copyright (c) 1999-2002 Jabber.com, Inc.  All Rights Reserved.  Contact
 * information for Jabber.com, Inc. is available at http://www.jabber.com/.
 *
 * Portions Copyright (c) 1998-1999 Jeremie Miller.
 * 
 * Acknowledgements
 * 
 * Special thanks to the Jabber Open Source Contributors for their
 * suggestions and support of Jabber.
 * 
 * Alternatively, the contents of this file may be used under the terms of the
 * GNU General Public License Version 2 or later (the "GPL"), in which case
 * the provisions of the GPL are applicable instead of those above.  If you
 * wish to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the JOSL,
 * indicate your decision by deleting the provisions above and replace them
 * with the notice and other provisions required by the GPL.  If you do not
 * delete the provisions above, a recipient may use your version of this file
 * under either the JOSL or the GPL. 
 * 
 * 
 * --------------------------------------------------------------------------*/

#include "jabber.h"
#include <glib.h>

static jid jid_safe(jid id)
{
    char *str;

    if(strlen(id->server) == 0 || strlen(id->server) > 255)
        return NULL;

    /* lowercase the hostname, make sure it's valid characters */
    for(str = id->server; *str != '\0'; str++)
    {
        *str = tolower(*str);
        if(!(isalnum(*str) || *str == '.' || *str == '-' || *str == '_')) return NULL;
    }

    /* cut off the user */
    if(id->user != NULL && strlen(id->user) > 64)
        id->user[64] = '\0';

    /* check for low and invalid ascii characters in the username */
    if(id->user != NULL)
        for(str = id->user; *str != '\0'; str++)
            if(*str <= 32 || *str == ':' || *str == '@' || *str == '<' || *str == '>' || *str == '\'' || *str == '"' || *str == '&') return NULL;

    return id;
}

jid jid_new(pool p, char *idstr)
{
    char *server, *resource, *type, *str;
    jid id;

    if(p == NULL || idstr == NULL || strlen(idstr) == 0)
        return NULL;

    /* user@server/resource */

    str = pstrdup(p, idstr);

    id = pmalloco(p,sizeof(struct jid_struct));
    id->p = p;

    resource = strstr(str,"/");
    if(resource != NULL)
    {
        *resource = '\0';
        ++resource;
        if(strlen(resource) > 0)
            id->resource = resource;
    }else{
        resource = str + strlen(str); /* point to end */
    }

    type = strstr(str,":");
    if(type != NULL && type < resource)
    {
        *type = '\0';
        ++type;
        str = type; /* ignore the type: prefix */
    }

    server = strstr(str,"@");
    if(server == NULL || server > resource)
    { /* if there's no @, it's just the server address */
        id->server = str;
    }else{
        *server = '\0';
        ++server;
        id->server = server;
        if(strlen(str) > 0)
            id->user = str;
    }

    return jid_safe(id);
}

char *jid_full(jid id)
{
    spool s;

    if(id == NULL)
        return NULL;

    /* use cached copy */
    if(id->full != NULL)
        return id->full;

    s = spool_new(id->p);

    if(id->user != NULL)
        spooler(s, id->user,"@",s);

    spool_add(s, id->server);

    if(id->resource != NULL)
        spooler(s, "/",id->resource,s);

    id->full = spool_print(s);
    return id->full;
}

/* local utils */
static int _jid_nullstrcmp(char *a, char *b)
{
    if(a == NULL && b == NULL) return 0;
    if(a == NULL || b == NULL) return -1;
    return strcmp(a,b);
}
static int _jid_nullstrcasecmp(char *a, char *b)
{
    if(a == NULL && b == NULL) return 0;
    if(a == NULL || b == NULL) return -1;
    return g_strcasecmp(a,b);
}

/* suggested by Anders Qvist <quest@valdez.netg.se> */
int jid_cmpx(jid a, jid b, int parts)
{
    if(a == NULL || b == NULL)
        return -1;

    if(parts & JID_RESOURCE && _jid_nullstrcmp(a->resource, b->resource) != 0) return -1;
    if(parts & JID_USER && _jid_nullstrcasecmp(a->user, b->user) != 0) return -1;
    if(parts & JID_SERVER && _jid_nullstrcmp(a->server, b->server) != 0) return -1;

    return 0;
}
