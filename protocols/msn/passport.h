/* passport.h
 *
 * Functions to login to Microsoft Passport service for Messenger
 * Copyright (C) 2004-2008 Wilmer van der Gaast <wilmer@gaast.net>
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

/* Thanks to http://msnpiki.msnfanatic.com/index.php/MSNP13:SOAPTweener
   for the specs! */

#ifndef __PASSPORT_H__
#define __PASSPORT_H__

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

struct msn_auth_data
{
	char *url;
	int ttl;
	
	char *username;
	char *password;
	char *cookie;
	
	/* The end result, the only thing we'll really be interested in
	   once finished. */
	char *token;
	char *error; /* Yeah, or that... */
	
	void (*callback)( struct msn_auth_data *mad );
	gpointer data;
};

#define SOAP_AUTHENTICATION_URL "https://loginnet.passport.com/RST.srf"

#define SOAP_AUTHENTICATION_REQUEST \
"POST %s HTTP/1.0\r\n" \
"Accept: text/*\r\n" \
"User-Agent: BitlBee " BITLBEE_VERSION "\r\n" \
"Host: %s\r\n" \
"Content-Length: %d\r\n" \
"Cache-Control: no-cache\r\n" \
"\r\n" \
"%s"

#define SOAP_AUTHENTICATION_PAYLOAD \
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>" \
"<Envelope xmlns=\"http://schemas.xmlsoap.org/soap/envelope/\" xmlns:wsse=\"http://schemas.xmlsoap.org/ws/2003/06/secext\" xmlns:saml=\"urn:oasis:names:tc:SAML:1.0:assertion\" xmlns:wsp=\"http://schemas.xmlsoap.org/ws/2002/12/policy\" xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\" xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/03/addressing\" xmlns:wssc=\"http://schemas.xmlsoap.org/ws/2004/04/sc\" xmlns:wst=\"http://schemas.xmlsoap.org/ws/2004/04/trust\">" \
  "<Header>" \
    "<ps:AuthInfo xmlns:ps=\"http://schemas.microsoft.com/Passport/SoapServices/PPCRL\" Id=\"PPAuthInfo\">" \
      "<ps:HostingApp>{7108E71A-9926-4FCB-BCC9-9A9D3F32E423}</ps:HostingApp>" \
      "<ps:BinaryVersion>4</ps:BinaryVersion>" \
      "<ps:UIVersion>1</ps:UIVersion>" \
      "<ps:Cookies></ps:Cookies>" \
      "<ps:RequestParams>AQAAAAIAAABsYwQAAAAzMDg0</ps:RequestParams>" \
    "</ps:AuthInfo>" \
    "<wsse:Security>" \
       "<wsse:UsernameToken Id=\"user\">" \
         "<wsse:Username>%s</wsse:Username>" \
         "<wsse:Password>%s</wsse:Password>" \
       "</wsse:UsernameToken>" \
    "</wsse:Security>" \
  "</Header>" \
  "<Body>" \
    "<ps:RequestMultipleSecurityTokens xmlns:ps=\"http://schemas.microsoft.com/Passport/SoapServices/PPCRL\" Id=\"RSTS\">" \
      "<wst:RequestSecurityToken Id=\"RST0\">" \
        "<wst:RequestType>http://schemas.xmlsoap.org/ws/2004/04/security/trust/Issue</wst:RequestType>" \
        "<wsp:AppliesTo>" \
          "<wsa:EndpointReference>" \
            "<wsa:Address>http://Passport.NET/tb</wsa:Address>" \
          "</wsa:EndpointReference>" \
        "</wsp:AppliesTo>" \
      "</wst:RequestSecurityToken>" \
      "<wst:RequestSecurityToken Id=\"RST1\">" \
       "<wst:RequestType>http://schemas.xmlsoap.org/ws/2004/04/security/trust/Issue</wst:RequestType>" \
        "<wsp:AppliesTo>" \
          "<wsa:EndpointReference>" \
            "<wsa:Address>messenger.msn.com</wsa:Address>" \
          "</wsa:EndpointReference>" \
        "</wsp:AppliesTo>" \
        "<wsse:PolicyReference URI=\"?%s\"></wsse:PolicyReference>" \
      "</wst:RequestSecurityToken>" \
    "</ps:RequestMultipleSecurityTokens>" \
  "</Body>" \
"</Envelope>"

int passport_get_token( gpointer func, gpointer data, char *username, char *password, char *cookie );

#endif /* __PASSPORT_H__ */
