  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
  \********************************************************************/

/* MSN module - All the SOAPy XML stuff.
   Some manager at Microsoft apparently thought MSNP wasn't XMLy enough so
   someone stepped up and changed that. This is the result. Kilobytes and
   more kilobytes of XML vomit to transfer tiny bits of informaiton. */

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
  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
  Suite 330, Boston, MA  02111-1307  USA
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
"User-Agent: BitlBee " BITLBEE_VERSION "\r\n" \
"Content-Type: text/xml; charset=utf-8\r\n" \
"%s" \
"Content-Length: %zd\r\n" \
"Cache-Control: no-cache\r\n" \
"\r\n" \
"%s"


#define SOAP_PASSPORT_SSO_URL "https://login.live.com/RST.srf"
#define SOAP_PASSPORT_SSO_URL_MSN "https://msnia.login.live.com/pp550/RST.srf"

#define SOAP_PASSPORT_SSO_PAYLOAD \
"<Envelope xmlns=\"http://schemas.xmlsoap.org/soap/envelope/\" " \
   "xmlns:wsse=\"http://schemas.xmlsoap.org/ws/2003/06/secext\" " \
   "xmlns:saml=\"urn:oasis:names:tc:SAML:1.0:assertion\" " \
   "xmlns:wsp=\"http://schemas.xmlsoap.org/ws/2002/12/policy\" " \
   "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\" " \
   "xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/03/addressing\" " \
   "xmlns:wssc=\"http://schemas.xmlsoap.org/ws/2004/04/sc\" " \
   "xmlns:wst=\"http://schemas.xmlsoap.org/ws/2004/04/trust\">" \
   "<Header>" \
       "<ps:AuthInfo " \
           "xmlns:ps=\"http://schemas.microsoft.com/Passport/SoapServices/PPCRL\" " \
           "Id=\"PPAuthInfo\">" \
           "<ps:HostingApp>{7108E71A-9926-4FCB-BCC9-9A9D3F32E423}</ps:HostingApp>" \
           "<ps:BinaryVersion>4</ps:BinaryVersion>" \
           "<ps:UIVersion>1</ps:UIVersion>" \
           "<ps:Cookies></ps:Cookies>" \
           "<ps:RequestParams>AQAAAAIAAABsYwQAAAAxMDMz</ps:RequestParams>" \
       "</ps:AuthInfo>" \
       "<wsse:Security>" \
           "<wsse:UsernameToken Id=\"user\">" \
               "<wsse:Username>%s</wsse:Username>" \
               "<wsse:Password>%s</wsse:Password>" \
           "</wsse:UsernameToken>" \
       "</wsse:Security>" \
   "</Header>" \
   "<Body>" \
       "<ps:RequestMultipleSecurityTokens " \
           "xmlns:ps=\"http://schemas.microsoft.com/Passport/SoapServices/PPCRL\" " \
           "Id=\"RSTS\">" \
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
                       "<wsa:Address>messengerclear.live.com</wsa:Address>" \
                   "</wsa:EndpointReference>" \
               "</wsp:AppliesTo>" \
               "<wsse:PolicyReference URI=\"%s\"></wsse:PolicyReference>" \
           "</wst:RequestSecurityToken>" \
           "<wst:RequestSecurityToken Id=\"RST2\">" \
               "<wst:RequestType>http://schemas.xmlsoap.org/ws/2004/04/security/trust/Issue</wst:RequestType>" \
               "<wsp:AppliesTo>" \
                   "<wsa:EndpointReference>" \
                       "<wsa:Address>contacts.msn.com</wsa:Address>" \
                   "</wsa:EndpointReference>" \
               "</wsp:AppliesTo>" \
               "<wsse:PolicyReference xmlns=\"http://schemas.xmlsoap.org/ws/2003/06/secext\" URI=\"MBI\"></wsse:PolicyReference>" \
           "</wst:RequestSecurityToken>" \
           "<wst:RequestSecurityToken Id=\"RST3\">" \
               "<wst:RequestType>http://schemas.xmlsoap.org/ws/2004/04/security/trust/Issue</wst:RequestType>" \
               "<wsp:AppliesTo>" \
                   "<wsa:EndpointReference>" \
                       "<wsa:Address>messengersecure.live.com</wsa:Address>" \
                   "</wsa:EndpointReference>" \
               "</wsp:AppliesTo>" \
               "<wsse:PolicyReference xmlns=\"http://schemas.xmlsoap.org/ws/2003/06/secext\" URI=\"MBI_SSL\"></wsse:PolicyReference>" \
           "</wst:RequestSecurityToken>" \
       "</ps:RequestMultipleSecurityTokens>" \
   "</Body>" \
"</Envelope>"

int msn_soap_passport_sso_request( struct im_connection *ic, const char *policy, const char *nonce );


#define SOAP_OIM_SEND_URL "https://ows.messenger.msn.com/OimWS/oim.asmx"
#define SOAP_OIM_SEND_ACTION "http://messenger.live.com/ws/2006/09/oim/Store2"

#define SOAP_OIM_SEND_PAYLOAD \
"<?xml version=\"1.0\" encoding=\"utf-8\"?>" \
"<soap:Envelope xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">" \
"<soap:Header>" \
  "<From memberName=\"%s\" friendlyName=\"=?utf-8?B?%s?=\" xml:lang=\"nl-nl\" proxy=\"MSNMSGR\" xmlns=\"http://messenger.msn.com/ws/2004/09/oim/\" msnpVer=\"%s\" buildVer=\"%s\"/>" \
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
    "X-OIM-Run-Id: {F9A6C9DD-0D94-4E85-9CC6-F9D118CC1CAF}\r\n" \
    "X-OIM-Sequence-Num: %d\r\n" \
    "\r\n" \
    "%s" \
  "</Content>" \
"</soap:Body>" \
"</soap:Envelope>"

int msn_soap_oim_send( struct im_connection *ic, const char *to, const char *msg );
int msn_soap_oim_send_queue( struct im_connection *ic, GSList **msgq );


#define SOAP_MEMLIST_URL "http://contacts.msn.com/abservice/SharingService.asmx"
#define SOAP_MEMLIST_ACTION "http://www.msn.com/webservices/AddressBook/FindMembership"

#define SOAP_MEMLIST_PAYLOAD \
"<?xml version=\"1.0\" encoding=\"utf-8\"?>" \
"<soap:Envelope xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">" \
  "<soap:Header xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">" \
    "<ABApplicationHeader xmlns=\"http://www.msn.com/webservices/AddressBook\">" \
      "<ApplicationId xmlns=\"http://www.msn.com/webservices/AddressBook\">CFE80F9D-180F-4399-82AB-413F33A1FA11</ApplicationId>" \
      "<IsMigration xmlns=\"http://www.msn.com/webservices/AddressBook\">false</IsMigration>" \
      "<PartnerScenario xmlns=\"http://www.msn.com/webservices/AddressBook\">Initial</PartnerScenario>" \
    "</ABApplicationHeader>" \
    "<ABAuthHeader xmlns=\"http://www.msn.com/webservices/AddressBook\">" \
      "<ManagedGroupRequest xmlns=\"http://www.msn.com/webservices/AddressBook\">false</ManagedGroupRequest>" \
      "<TicketToken>%s</TicketToken>" \
    "</ABAuthHeader>" \
  "</soap:Header>" \
  "<soap:Body xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">" \
    "<FindMembership xmlns=\"http://www.msn.com/webservices/AddressBook\"><serviceFilter xmlns=\"http://www.msn.com/webservices/AddressBook\"><Types xmlns=\"http://www.msn.com/webservices/AddressBook\"><ServiceType xmlns=\"http://www.msn.com/webservices/AddressBook\">Messenger</ServiceType><ServiceType xmlns=\"http://www.msn.com/webservices/AddressBook\">Invitation</ServiceType><ServiceType xmlns=\"http://www.msn.com/webservices/AddressBook\">SocialNetwork</ServiceType><ServiceType xmlns=\"http://www.msn.com/webservices/AddressBook\">Space</ServiceType><ServiceType xmlns=\"http://www.msn.com/webservices/AddressBook\">Profile</ServiceType></Types></serviceFilter>" \
    "</FindMembership>" \
  "</soap:Body>" \
"</soap:Envelope>"

int msn_soap_memlist_request( struct im_connection *ic );


#define SOAP_ADDRESSBOOK_URL "http://contacts.msn.com/abservice/abservice.asmx"
#define SOAP_ADDRESSBOOK_ACTION "http://www.msn.com/webservices/AddressBook/ABFindAll"

#define SOAP_ADDRESSBOOK_PAYLOAD \
"<?xml version=\"1.0\" encoding=\"utf-8\"?>" \
"<soap:Envelope xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" xmlns:soapenc=\"http://schemas.xmlsoap.org/soap/encoding/\">" \
  "<soap:Header>" \
    "<ABApplicationHeader xmlns=\"http://www.msn.com/webservices/AddressBook\">" \
      "<ApplicationId>CFE80F9D-180F-4399-82AB-413F33A1FA11</ApplicationId>" \
      "<IsMigration>false</IsMigration>" \
      "<PartnerScenario>Initial</PartnerScenario>" \
    "</ABApplicationHeader>" \
    "<ABAuthHeader xmlns=\"http://www.msn.com/webservices/AddressBook\">" \
      "<ManagedGroupRequest>false</ManagedGroupRequest>" \
      "<TicketToken>%s</TicketToken>" \
    "</ABAuthHeader>" \
  "</soap:Header>" \
  "<soap:Body>" \
    "<ABFindAll xmlns=\"http://www.msn.com/webservices/AddressBook\">" \
      "<abId>00000000-0000-0000-0000-000000000000</abId>" \
      "<abView>Full</abView>" \
      "<deltasOnly>false</deltasOnly>" \
      "<lastChange>0001-01-01T00:00:00.0000000-08:00</lastChange>" \
    "</ABFindAll>" \
  "</soap:Body>" \
"</soap:Envelope>"

int msn_soap_addressbook_request( struct im_connection *ic );


#endif /* __SOAP_H__ */
