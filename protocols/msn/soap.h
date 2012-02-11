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


int msn_soapq_flush( struct im_connection *ic, gboolean resend );


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
#define SOAP_PASSPORT_SSO_URL_MSN "https://msnia.login.live.com/pp900/RST.srf"
#define MAX_PASSPORT_PWLEN 16

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
           "<wst:RequestSecurityToken Id=\"RST4\">" \
               "<wst:RequestType>http://schemas.xmlsoap.org/ws/2004/04/security/trust/Issue</wst:RequestType>" \
               "<wsp:AppliesTo>" \
                   "<wsa:EndpointReference>" \
                       "<wsa:Address>storage.msn.com</wsa:Address>" \
                   "</wsa:EndpointReference>" \
               "</wsp:AppliesTo>" \
               "<wsse:PolicyReference xmlns=\"http://schemas.xmlsoap.org/ws/2003/06/secext\" URI=\"MBI_SSL\"></wsse:PolicyReference>" \
           "</wst:RequestSecurityToken>" \
       "</ps:RequestMultipleSecurityTokens>" \
   "</Body>" \
"</Envelope>"

int msn_soap_passport_sso_request( struct im_connection *ic, const char *nonce );


#define SOAP_ABSERVICE_PAYLOAD \
"<?xml version=\"1.0\" encoding=\"utf-8\"?>" \
"<soap:Envelope xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">" \
  "<soap:Header xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">" \
    "<ABApplicationHeader xmlns=\"http://www.msn.com/webservices/AddressBook\">" \
      "<ApplicationId xmlns=\"http://www.msn.com/webservices/AddressBook\">CFE80F9D-180F-4399-82AB-413F33A1FA11</ApplicationId>" \
      "<IsMigration xmlns=\"http://www.msn.com/webservices/AddressBook\">false</IsMigration>" \
      "<PartnerScenario xmlns=\"http://www.msn.com/webservices/AddressBook\">%s</PartnerScenario>" \
    "</ABApplicationHeader>" \
    "<ABAuthHeader xmlns=\"http://www.msn.com/webservices/AddressBook\">" \
      "<ManagedGroupRequest xmlns=\"http://www.msn.com/webservices/AddressBook\">false</ManagedGroupRequest>" \
      "<TicketToken>%s</TicketToken>" \
    "</ABAuthHeader>" \
  "</soap:Header>" \
  "<soap:Body xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">" \
    "%%s" \
  "</soap:Body>" \
"</soap:Envelope>"

#define SOAP_MEMLIST_URL "http://contacts.msn.com/abservice/SharingService.asmx"
#define SOAP_MEMLIST_ACTION "http://www.msn.com/webservices/AddressBook/FindMembership"

#define SOAP_MEMLIST_PAYLOAD \
    "<FindMembership xmlns=\"http://www.msn.com/webservices/AddressBook\"><serviceFilter xmlns=\"http://www.msn.com/webservices/AddressBook\"><Types xmlns=\"http://www.msn.com/webservices/AddressBook\"><ServiceType xmlns=\"http://www.msn.com/webservices/AddressBook\">Messenger</ServiceType><ServiceType xmlns=\"http://www.msn.com/webservices/AddressBook\">Invitation</ServiceType><ServiceType xmlns=\"http://www.msn.com/webservices/AddressBook\">SocialNetwork</ServiceType><ServiceType xmlns=\"http://www.msn.com/webservices/AddressBook\">Space</ServiceType><ServiceType xmlns=\"http://www.msn.com/webservices/AddressBook\">Profile</ServiceType></Types></serviceFilter>" \
    "</FindMembership>"

#define SOAP_MEMLIST_ADD_ACTION "http://www.msn.com/webservices/AddressBook/AddMember"
#define SOAP_MEMLIST_DEL_ACTION "http://www.msn.com/webservices/AddressBook/DeleteMember"

#define SOAP_MEMLIST_EDIT_PAYLOAD \
  "<%sMember xmlns=\"http://www.msn.com/webservices/AddressBook\">" \
   "<serviceHandle>" \
    "<Id>0</Id>" \
    "<Type>Messenger</Type>" \
    "<ForeignId></ForeignId>" \
   "</serviceHandle>" \
   "<memberships>" \
    "<Membership>" \
     "<MemberRole>%s</MemberRole>" \
     "<Members>" \
      "<Member xsi:type=\"PassportMember\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">" \
       "<Type>Passport</Type>" \
       "<State>Accepted</State>" \
       "<PassportName>%s</PassportName>" \
      "</Member>" \
     "</Members>" \
    "</Membership>" \
   "</memberships>" \
  "</%sMember>"

int msn_soap_memlist_request( struct im_connection *ic );
int msn_soap_memlist_edit( struct im_connection *ic, const char *handle, gboolean add, int list );


#define SOAP_ADDRESSBOOK_URL "http://contacts.msn.com/abservice/abservice.asmx"
#define SOAP_ADDRESSBOOK_ACTION "http://www.msn.com/webservices/AddressBook/ABFindAll"

#define SOAP_ADDRESSBOOK_PAYLOAD \
    "<ABFindAll xmlns=\"http://www.msn.com/webservices/AddressBook\">" \
      "<abId>00000000-0000-0000-0000-000000000000</abId>" \
      "<abView>Full</abView>" \
      "<deltasOnly>false</deltasOnly>" \
      "<lastChange>0001-01-01T00:00:00.0000000-08:00</lastChange>" \
    "</ABFindAll>"

#define SOAP_AB_NAMECHANGE_ACTION "http://www.msn.com/webservices/AddressBook/ABContactUpdate"

#define SOAP_AB_NAMECHANGE_PAYLOAD \
        "<ABContactUpdate xmlns=\"http://www.msn.com/webservices/AddressBook\">" \
            "<abId>00000000-0000-0000-0000-000000000000</abId>" \
            "<contacts>" \
                "<Contact xmlns=\"http://www.msn.com/webservices/AddressBook\">" \
                    "<contactInfo>" \
                        "<contactType>Me</contactType>" \
                        "<displayName>%s</displayName>" \
                    "</contactInfo>" \
                    "<propertiesChanged>DisplayName</propertiesChanged>" \
                "</Contact>" \
            "</contacts>" \
        "</ABContactUpdate>"

#define SOAP_AB_CONTACT_ADD_ACTION "http://www.msn.com/webservices/AddressBook/ABContactAdd"

#define SOAP_AB_CONTACT_ADD_PAYLOAD \
        "<ABContactAdd xmlns=\"http://www.msn.com/webservices/AddressBook\">" \
            "<abId>00000000-0000-0000-0000-000000000000</abId>" \
            "<contacts>" \
                "<Contact xmlns=\"http://www.msn.com/webservices/AddressBook\">" \
                    "<contactInfo>" \
                        "<contactType>LivePending</contactType>" \
                        "<passportName>%s</passportName>" \
                        "<isMessengerUser>true</isMessengerUser>" \
                        "<MessengerMemberInfo>" \
                            "<DisplayName>%s</DisplayName>" \
                        "</MessengerMemberInfo>" \
                    "</contactInfo>" \
                "</Contact>" \
            "</contacts>" \
            "<options>" \
                "<EnableAllowListManagement>true</EnableAllowListManagement>" \
            "</options>" \
        "</ABContactAdd>"

#define SOAP_AB_CONTACT_DEL_ACTION "http://www.msn.com/webservices/AddressBook/ABContactDelete"

#define SOAP_AB_CONTACT_DEL_PAYLOAD \
        "<ABContactDelete xmlns=\"http://www.msn.com/webservices/AddressBook\">" \
            "<abId>00000000-0000-0000-0000-000000000000</abId>" \
            "<contacts>" \
                "<Contact xmlns=\"http://www.msn.com/webservices/AddressBook\">" \
                    "<contactId>%s</contactId>" \
                "</Contact>" \
            "</contacts>" \
        "</ABContactDelete>"

int msn_soap_addressbook_request( struct im_connection *ic );
int msn_soap_addressbook_set_display_name( struct im_connection *ic, const char *new );
int msn_soap_ab_contact_add( struct im_connection *ic, bee_user_t *bu );
int msn_soap_ab_contact_del( struct im_connection *ic, bee_user_t *bu );


#define SOAP_STORAGE_URL "https://storage.msn.com/storageservice/SchematizedStore.asmx"
#define SOAP_PROFILE_GET_ACTION "http://www.msn.com/webservices/storage/w10/GetProfile"
#define SOAP_PROFILE_SET_DN_ACTION "http://www.msn.com/webservices/storage/w10/UpdateProfile"

#define SOAP_PROFILE_GET_PAYLOAD \
"<?xml version=\"1.0\" encoding=\"utf-8\"?>" \
"<soap:Envelope xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">" \
  "<soap:Header xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">" \
    "<StorageApplicationHeader xmlns=\"http://www.msn.com/webservices/storage/w10\">" \
      "<ApplicationID>Messenger Client 9.0</ApplicationID>" \
      "<Scenario>Initial</Scenario>" \
    "</StorageApplicationHeader>" \
    "<StorageUserHeader xmlns=\"http://www.msn.com/webservices/storage/w10\">" \
      "<Puid>0</Puid>" \
      "<TicketToken>%s</TicketToken>" \
    "</StorageUserHeader>" \
  "</soap:Header>" \
  "<soap:Body xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">" \
    "<GetProfile xmlns=\"http://www.msn.com/webservices/storage/w10\">" \
      "<profileHandle>" \
        "<Alias>" \
          "<Name>%s</Name>" \
          "<NameSpace>MyCidStuff</NameSpace>" \
        "</Alias>" \
        "<RelationshipName>MyProfile</RelationshipName>" \
      "</profileHandle>" \
      "<profileAttributes>" \
        "<ResourceID>true</ResourceID>" \
        "<DateModified>true</DateModified>" \
        "<ExpressionProfileAttributes>" \
          "<ResourceID>true</ResourceID>" \
          "<DateModified>true</DateModified>" \
          "<DisplayName>true</DisplayName>" \
          "<DisplayNameLastModified>true</DisplayNameLastModified>" \
          "<PersonalStatus>true</PersonalStatus>" \
          "<PersonalStatusLastModified>true</PersonalStatusLastModified>" \
          "<StaticUserTilePublicURL>true</StaticUserTilePublicURL>" \
          "<Photo>true</Photo>" \
          "<Flags>true</Flags>" \
        "</ExpressionProfileAttributes>" \
      "</profileAttributes>" \
    "</GetProfile>" \
  "</soap:Body>" \
"</soap:Envelope>"

#define SOAP_PROFILE_SET_DN_PAYLOAD \
"<?xml version=\"1.0\" encoding=\"utf-8\"?>" \
"<soap:Envelope xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">" \
  "<soap:Header xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">" \
    "<StorageApplicationHeader xmlns=\"http://www.msn.com/webservices/storage/w10\">" \
      "<ApplicationID>Messenger Client 9.0</ApplicationID>" \
      "<Scenario>Initial</Scenario>" \
    "</StorageApplicationHeader>" \
    "<StorageUserHeader xmlns=\"http://www.msn.com/webservices/storage/w10\">" \
      "<Puid>0</Puid>" \
      "<TicketToken>%s</TicketToken>" \
    "</StorageUserHeader>" \
  "</soap:Header>" \
  "<soap:Body xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">" \
    "<UpdateProfile xmlns=\"http://www.msn.com/webservices/storage/w10\">" \
      "<profile>" \
        "<ResourceID>%s</ResourceID>" \
        "<ExpressionProfile>" \
          "<FreeText>Update</FreeText>" \
          "<DisplayName>%s</DisplayName>" \
          "<Flags>0</Flags>" \
        "</ExpressionProfile>" \
      "</profile>" \
    "</UpdateProfile>" \
  "</soap:Body>" \
"</soap:Envelope>"

int msn_soap_profile_get( struct im_connection *ic, const char *cid );
int msn_soap_profile_set_dn( struct im_connection *ic, const char *dn );

#endif /* __SOAP_H__ */
