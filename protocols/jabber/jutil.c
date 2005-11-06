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
#include "nogaim.h"

/* util for making presence packets */
xmlnode jutil_presnew(int type, char *to, char *status)
{
    xmlnode pres;

    pres = xmlnode_new_tag("presence");
    switch(type)
    {
    case JPACKET__SUBSCRIBE:
        xmlnode_put_attrib(pres,"type","subscribe");
        break;
    case JPACKET__UNSUBSCRIBE:
        xmlnode_put_attrib(pres,"type","unsubscribe");
        break;
    case JPACKET__SUBSCRIBED:
        xmlnode_put_attrib(pres,"type","subscribed");
        break;
    case JPACKET__UNSUBSCRIBED:
        xmlnode_put_attrib(pres,"type","unsubscribed");
        break;
    case JPACKET__PROBE:
        xmlnode_put_attrib(pres,"type","probe");
        break;
    case JPACKET__UNAVAILABLE:
        xmlnode_put_attrib(pres,"type","unavailable");
        break;
    case JPACKET__INVISIBLE:
        xmlnode_put_attrib(pres,"type","invisible");
        break;
    }
    if(to != NULL)
        xmlnode_put_attrib(pres,"to",to);
    if(status != NULL)
        xmlnode_insert_cdata(xmlnode_insert_tag(pres,"status"),status,strlen(status));

    return pres;
}

/* util for making IQ packets */
xmlnode jutil_iqnew(int type, char *ns)
{
    xmlnode iq;

    iq = xmlnode_new_tag("iq");
    switch(type)
    {
    case JPACKET__GET:
        xmlnode_put_attrib(iq,"type","get");
        break;
    case JPACKET__SET:
        xmlnode_put_attrib(iq,"type","set");
        break;
    case JPACKET__RESULT:
        xmlnode_put_attrib(iq,"type","result");
        break;
    case JPACKET__ERROR:
        xmlnode_put_attrib(iq,"type","error");
        break;
    }
    xmlnode_put_attrib(xmlnode_insert_tag(iq,"query"),"xmlns",ns);

    return iq;
}

/* util for making stream packets */
xmlnode jutil_header(char* xmlns, char* server)
{
     xmlnode result;
     if ((xmlns == NULL)||(server == NULL))
	  return NULL;
     result = xmlnode_new_tag("stream:stream");
     xmlnode_put_attrib(result, "xmlns:stream", "http://etherx.jabber.org/streams");
     xmlnode_put_attrib(result, "xmlns", xmlns);
     xmlnode_put_attrib(result, "to", server);

     return result;
}
