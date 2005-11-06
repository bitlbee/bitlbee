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
static jpacket jpacket_reset(jpacket p);

jpacket jpacket_new(xmlnode x)
{
    jpacket p;

    if(x == NULL)
        return NULL;

    p = pmalloc(xmlnode_pool(x),sizeof(_jpacket));
    p->x = x;

    return jpacket_reset(p);
}

static jpacket jpacket_reset(jpacket p)
{
    char *val;
    xmlnode x;

    x = p->x;
    memset(p,0,sizeof(_jpacket));
    p->x = x;
    p->p = xmlnode_pool(x);

    if(strncmp(xmlnode_get_name(x),"message",7) == 0)
    {
        p->type = JPACKET_MESSAGE;
    }else if(strncmp(xmlnode_get_name(x),"presence",8) == 0)
    {
        p->type = JPACKET_PRESENCE;
        val = xmlnode_get_attrib(x, "type");
        if(val == NULL)
            p->subtype = JPACKET__AVAILABLE;
        else if(strcmp(val,"unavailable") == 0)
            p->subtype = JPACKET__UNAVAILABLE;
        else if(strcmp(val,"probe") == 0)
            p->subtype = JPACKET__PROBE;
        else if(strcmp(val,"error") == 0)
            p->subtype = JPACKET__ERROR;
        else if(strcmp(val,"invisible") == 0)
            p->subtype = JPACKET__INVISIBLE;
        else if(*val == 's' || *val == 'u')
            p->type = JPACKET_S10N;
        else if(strcmp(val,"available") == 0)
        { /* someone is using type='available' which is frowned upon */
            xmlnode_hide_attrib(x,"type");
            p->subtype = JPACKET__AVAILABLE;
        }else
            p->type = JPACKET_UNKNOWN;
    }else if(strncmp(xmlnode_get_name(x),"iq",2) == 0)
    {
        p->type = JPACKET_IQ;
        p->iq = xmlnode_get_tag(x,"?xmlns");
        p->iqns = xmlnode_get_attrib(p->iq,"xmlns");
    }

    /* set up the jids if any, flag packet as unknown if they are unparseable */
    val = xmlnode_get_attrib(x,"to");
    if(val != NULL)
        if((p->to = jid_new(p->p, val)) == NULL)
            p->type = JPACKET_UNKNOWN;
    val = xmlnode_get_attrib(x,"from");
    if(val != NULL)
        if((p->from = jid_new(p->p, val)) == NULL)
            p->type = JPACKET_UNKNOWN;

    return p;
}


int jpacket_subtype(jpacket p)
{
    char *type;
    int ret = p->subtype;

    if(ret != JPACKET__UNKNOWN)
        return ret;

    ret = JPACKET__NONE; /* default, when no type attrib is specified */
    type = xmlnode_get_attrib(p->x, "type");
    if(j_strcmp(type,"error") == 0)
        ret = JPACKET__ERROR;
    else
        switch(p->type)
        {
        case JPACKET_MESSAGE:
            if(j_strcmp(type,"chat") == 0)
                ret = JPACKET__CHAT;
            else if(j_strcmp(type,"groupchat") == 0)
                ret = JPACKET__GROUPCHAT;
            else if(j_strcmp(type,"headline") == 0)
                ret = JPACKET__HEADLINE;
            break;
        case JPACKET_S10N:
            if(j_strcmp(type,"subscribe") == 0)
                ret = JPACKET__SUBSCRIBE;
            else if(j_strcmp(type,"subscribed") == 0)
                ret = JPACKET__SUBSCRIBED;
            else if(j_strcmp(type,"unsubscribe") == 0)
                ret = JPACKET__UNSUBSCRIBE;
            else if(j_strcmp(type,"unsubscribed") == 0)
                ret = JPACKET__UNSUBSCRIBED;
            break;
        case JPACKET_IQ:
            if(j_strcmp(type,"get") == 0)
                ret = JPACKET__GET;
            else if(j_strcmp(type,"set") == 0)
                ret = JPACKET__SET;
            else if(j_strcmp(type,"result") == 0)
                ret = JPACKET__RESULT;
            break;
        }

    p->subtype = ret;
    return ret;
}
