/* soap.h
 *
 * SOAP-related functions. Some manager at Microsoft apparently thought
 * MSNP wasn't XMLy enough so someone stepped up and changed that. This
 * is the result.
 *
 * Copyright (C) 2010 Wilmer van der Gaast <wilmer@gaast.net>
 *
 * This program is free software; you can redistribute it and/or modify             
 * it under the terms of the GNU General Public License version 2                   
 * as published by the Free Software Foundation                                     
 *                                                                                   
 * This program is distributed in the hope that is will be useful,                  
 * bit WITHOU ANY WARRANTY; without even the implied warranty of                   
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                    
 * GNU General Public License for more details.                                     
 *                                                                                   
 * You should have received a copy of the GNU General Public License                
 * along with this program; if not, write to the Free Software                      
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA          
 */

/* Thanks to http://msnpiki.msnfanatic.com/ for lots of info on this! */

#ifndef __SOAP_H__
#define __SOAP_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
#include "nogaim.h"


#define SOAP_HTTP_REQUEST \
"POST %s HTTP/1.0\r\n" \
"Host: %s\r\n" \
"Accept: */*\r\n" \
"SOAPAction: \"%s\"\r\n" \
"User-Agent: BitlBee " BITLBEE_VERSION "\r\n" \
"Content-Type: text/xml; charset=utf-8\r\n" \
"Content-Length: %d\r\n" \
"Cache-Control: no-cache\r\n" \
"\r\n" \
"%s"


#define SOAP_OIM_SEND_URL "https://ows.messenger.msn.com/OimWS/oim.asmx"
#define SOAP_OIM_ACTION_URL "http://messenger.msn.com/ws/2004/09/oim/Store"

#define SOAP_OIM_SEND_PAYLOAD \
"<?xml version=\"1.0\" encoding=\"utf-8\"?>" \
"<soap:Envelope xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">" \
"<soap:Header>" \
  "<From memberName=\"%s\" friendlyName=\"=?utf-8?B?%s?=\" xml:lang=\"nl-nl\" proxy=\"MSNMSGR\" xmlns=\"http://messenger.msn.com/ws/2004/09/oim/\" msnpVer=\"MSNP13\" buildVer=\"8.0.0328\"/>" \
  "<To memberName=\"%s\" xmlns=\"http://messenger.msn.com/ws/2004/09/oim/\"/>" \
  "<Ticket passport=\"%s\" appid=\"%s\" lockkey=\"%s\" xmlns=\"http://messenger.msn.com/ws/2004/09/oim/\"/>" \
  "<Sequence xmlns=\"http://schemas.xmlsoap.org/ws/2003/03/rm\">" \
    "<Identifier xmlns=\"http://schemas.xmlsoap.org/ws/2002/07/utility\">http://messenger.msn.com</Identifier>" \
    "<MessageNumber>%d</MessageNumber>" \
  "</Sequence>" \
"</soap:Header>" \
"<soap:Body>" \
  "<MessageType xmlns=\"http://messenger.msn.com/ws/2004/09/oim/\">text</MessageType>" \
  "<Content xmlns=\"http://messenger.msn.com/ws/2004/09/oim/\">" \
    "MIME-Version: 1.0\r\n" \
    "Content-Type: text/plain; charset=UTF-8\r\n" \
    "Content-Transfer-Encoding: base64\r\n" \
    "X-OIM-Message-Type: OfflineMessage\r\n" \
    "X-OIM-Run-Id: {89527393-8723-4F4F-8005-287532973298}\r\n" \
    "X-OIM-Sequence-Num: %d\r\n" \
    "\r\n" \
    "%s" \
  "</Content>" \
"</soap:Body>" \
"</soap:Envelope>"

int msn_soap_oim_send( struct im_connection *ic, const char *to, const char *msg );
int msn_soap_oim_send_queue( struct im_connection *ic, GSList **msgq );

#endif /* __SOAP_H__ */
