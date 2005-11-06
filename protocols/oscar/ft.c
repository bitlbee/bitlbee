/*
 * File transfer (OFT) and DirectIM (ODC).
 * (OSCAR File Transfer, Oscar Direct Connect(ion?)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <aim.h>
#include <glib.h>
#include "ft.h"

#ifndef _WIN32
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/utsname.h> /* for aim_directim_initiate */
#include <arpa/inet.h> /* for inet_ntoa */
#endif

/* TODO: 
   o look for memory leaks.. there's going to be shitloads, i'm sure. 
*/

struct aim_directim_intdata {
	guint8 cookie[8];
	char sn[MAXSNLEN+1];
	char ip[22];
};

static int listenestablish(guint16 portnum);
 
/**
 * aim_handlerendconnect - call this to accept OFT connections and set up the required structures
 * @sess: the session
 * @cur: the conn the incoming connection is on
 *
 * call this when you get an outstanding read on a conn with subtype
 * AIM_CONN_SUBTYPE_RENDEZVOUS_OUT, it will clone the current
 * &aim_conn_t and tweak things as appropriate. the new conn and the
 * listener conn are both returned to the client in the
 * %AIM_CB_FAM_OFT, %AIM_CB_OFT_<CLASS>INITIATE callback.
 */
int aim_handlerendconnect(aim_session_t *sess, aim_conn_t *cur)
{ 
	int acceptfd = 0;
	struct sockaddr cliaddr;
	unsigned int clilen = sizeof(cliaddr);
	int ret = 0;
	aim_conn_t *newconn;

	if ((acceptfd = accept(cur->fd, &cliaddr, &clilen)) == -1)
		return 0; /* not an error */

	if (cliaddr.sa_family != AF_INET) { /* just in case IPv6 really is happening */
		closesocket(acceptfd);
		aim_conn_close(cur);
		return -1;
	} 

	if (!(newconn = aim_cloneconn(sess, cur))) {
		closesocket(acceptfd);
		aim_conn_close(cur);
		return -1;
	}

	newconn->type = AIM_CONN_TYPE_RENDEZVOUS;
	newconn->fd = acceptfd;

	if (newconn->subtype == AIM_CONN_SUBTYPE_OFT_DIRECTIM) { 
		struct aim_directim_intdata *priv;
		aim_rxcallback_t userfunc;

		priv = (struct aim_directim_intdata *)(newconn->internal = cur->internal);
		cur->internal = NULL;

		g_snprintf(priv->ip, sizeof(priv->ip), "%s:%u", 
				inet_ntoa(((struct sockaddr_in *)&cliaddr)->sin_addr), 
				ntohs(((struct sockaddr_in *)&cliaddr)->sin_port));

		if ((userfunc = aim_callhandler(sess, newconn, AIM_CB_FAM_OFT, AIM_CB_OFT_DIRECTIMINITIATE)))
			ret = userfunc(sess, NULL, newconn, cur);

	} else if (newconn->subtype == AIM_CONN_SUBTYPE_OFT_GETFILE) {
#if 0
		struct aim_filetransfer_priv *priv;
		aim_rxcallback_t userfunc;


		newconn->priv = cur->priv;
		cur->priv = NULL;
		priv = (struct aim_filetransfer_priv *)newconn->priv;

		g_snprintf(priv->ip, sizeof(priv->ip), "%s:%u", inet_ntoa(((struct sockaddr_in *)&cliaddr)->sin_addr), ntohs(((struct sockaddr_in *)&cliaddr)->sin_port));

		if ((userfunc = aim_callhandler(sess, newconn, AIM_CB_FAM_OFT, AIM_CB_OFT_GETFILEINITIATE)))
			ret = userfunc(sess, NULL, newconn, cur);
#endif
	} else { 
		do_error_dialog(sess->aux_data, "Got a Connection on a listener that's not Rendezvous Closing conn.", "Gaim");
		aim_conn_close(newconn);
		ret = -1;
	}

	return ret;
}

/**
 * aim_send_typing - send client-to-client typing notification over established connection
 * @sess: session to conn
 * @conn: directim connection
 * @typing: If true, notify user has started typing; if false, notify user has stopped.
 *
 * The connection must have been previously established.
 */
int aim_send_typing(aim_session_t *sess, aim_conn_t *conn, int typing)
{
	
struct aim_directim_intdata *intdata = (struct aim_directim_intdata *)conn->internal;
	aim_frame_t *fr;
	aim_bstream_t hdrbs; /* XXX this should be within aim_frame_t */

	if (!sess || !conn || (conn->type != AIM_CONN_TYPE_RENDEZVOUS)) 
		return -EINVAL; 

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_OFT, 0x01, 0)))
	       return -ENOMEM;	

	memcpy(fr->hdr.oft.magic, "ODC2", 4);
	
	fr->hdr.oft.hdr2len = 0x44;
	
	if (!(fr->hdr.oft.hdr2 = g_malloc(fr->hdr.oft.hdr2len))) { 
		aim_frame_destroy(fr);
		return -ENOMEM;
	}
	memset(fr->hdr.oft.hdr2, 0, fr->hdr.oft.hdr2len);
	
	aim_bstream_init(&hdrbs, fr->hdr.oft.hdr2, fr->hdr.oft.hdr2len);

	aimbs_put16(&hdrbs, 0x0006);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_putraw(&hdrbs, intdata->cookie, 8);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put32(&hdrbs, 0x00000000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);

	/* flags -- 0x000e for "started typing", 0x0002 for "stopped typing */
	aimbs_put16(&hdrbs, ( typing ? 0x000e : 0x0002));

	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_putraw(&hdrbs, (guint8 *)sess->sn, strlen(sess->sn));
	
	aim_bstream_setpos(&hdrbs, 52); /* bleeehh */

	aimbs_put8(&hdrbs, 0x00);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);

	/* end of hdr2 */

	aim_tx_enqueue(sess, fr);

	return 0;
} 

/**
 * aim_send_im_direct - send IM client-to-client over established connection
 * @sess: session to conn
 * @conn: directim connection
 * @msg:  null-terminated string to send. 
 * len:   The length of the message to send, including binary data.
 * 
 * Call this just like you would aim_send_im, to send a directim. You
 * _must_ have previously established the directim connection.
 */
int aim_send_im_direct(aim_session_t *sess, aim_conn_t *conn, const char *msg, int len)
{
	struct aim_directim_intdata *intdata = (struct aim_directim_intdata *)conn->internal;
	aim_frame_t *fr;
	aim_bstream_t hdrbs; /* XXX this should be within aim_frame_t */

	if (!sess || !conn || !msg || (conn->type != AIM_CONN_TYPE_RENDEZVOUS)) 
		return -EINVAL; 

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_OFT, 0x01, len)))
		return -ENOMEM;	

	memcpy(fr->hdr.oft.magic, "ODC2", 4);
	
	fr->hdr.oft.hdr2len = 0x44;
	
	if (!(fr->hdr.oft.hdr2 = g_malloc(fr->hdr.oft.hdr2len))) { 
		aim_frame_destroy(fr);
		return -ENOMEM;
	}
	memset(fr->hdr.oft.hdr2, 0, fr->hdr.oft.hdr2len);
	
	aim_bstream_init(&hdrbs, fr->hdr.oft.hdr2, fr->hdr.oft.hdr2len);
	
	aimbs_put16(&hdrbs, 0x0006);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_putraw(&hdrbs, intdata->cookie, 8);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put32(&hdrbs, len);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	
	/* flags -- 0x000e for "started typing", 0x0002 for "stopped typing, 0x0000 for message */
	aimbs_put16(&hdrbs, 0x0000);
	
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_putraw(&hdrbs, (guint8 *)sess->sn, strlen(sess->sn));
	
	aim_bstream_setpos(&hdrbs, 52); /* bleeehh */
	
	aimbs_put8(&hdrbs, 0x00);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	aimbs_put16(&hdrbs, 0x0000);
	
	/* end of hdr2 */
	
#if 0 /* XXX this is how you send buddy icon info... */	
	i += aimutil_put16(newpacket->hdr.oft.hdr2+i, 0x0008);
	i += aimutil_put16(newpacket->hdr.oft.hdr2+i, 0x000c);
	i += aimutil_put16(newpacket->hdr.oft.hdr2+i, 0x0000);
	i += aimutil_put16(newpacket->hdr.oft.hdr2+i, 0x1466);
	i += aimutil_put16(newpacket->hdr.oft.hdr2+i, 0x0001);
	i += aimutil_put16(newpacket->hdr.oft.hdr2+i, 0x2e0f);
	i += aimutil_put16(newpacket->hdr.oft.hdr2+i, 0x393e);
	i += aimutil_put16(newpacket->hdr.oft.hdr2+i, 0xcac8);
#endif
	aimbs_putraw(&fr->data, (guint8 *)msg, len);
	
	aim_tx_enqueue(sess, fr);
	
	return 0;
} 

static int getlocalip(guint8 *ip)
{
	struct hostent *hptr;
	char localhost[129];

	/* XXX if available, use getaddrinfo() */
	/* XXX allow client to specify which IP to use for multihomed boxes */

	if (gethostname(localhost, 128) < 0)
		return -1;

	if (!(hptr = gethostbyname(localhost)))
		return -1;

	memcpy(ip, hptr->h_addr_list[0], 4);

	return 0;
}

/**
 * aim_directim_intitiate - For those times when we want to open up the directim channel ourselves.
 * @sess: your session,
 * @conn: the BOS conn,
 * @priv: a dummy priv value (we'll let it get filled in later) (if you pass a %NULL, we alloc one)
 * @destsn: the SN to connect to.
 *
 */
aim_conn_t *aim_directim_initiate(aim_session_t *sess, const char *destsn)
{ 
	aim_conn_t *newconn;
	aim_msgcookie_t *cookie;
	struct aim_directim_intdata *priv;
	int listenfd;
	guint16 port = 4443;
	guint8 localip[4];
	guint8 ck[8];

	if (getlocalip(localip) == -1)
		return NULL;

	if ((listenfd = listenestablish(port)) == -1)
		return NULL;

	aim_request_directim(sess, destsn, localip, port, ck);

	cookie = (aim_msgcookie_t *)g_new0(aim_msgcookie_t,1);
	memcpy(cookie->cookie, ck, 8);
	cookie->type = AIM_COOKIETYPE_OFTIM;

	/* this one is for the cookie */
	priv = (struct aim_directim_intdata *)g_new0(struct aim_directim_intdata,1);

	memcpy(priv->cookie, ck, 8);
	strncpy(priv->sn, destsn, sizeof(priv->sn));
	cookie->data = priv;
	aim_cachecookie(sess, cookie);

	/* XXX switch to aim_cloneconn()? */
	if (!(newconn = aim_newconn(sess, AIM_CONN_TYPE_RENDEZVOUS_OUT, NULL))) {
		closesocket(listenfd);
		return NULL;
	}

	/* this one is for the conn */
	priv = (struct aim_directim_intdata *)g_new0(struct aim_directim_intdata, 1);

	memcpy(priv->cookie, ck, 8);
	strncpy(priv->sn, destsn, sizeof(priv->sn));

	newconn->fd = listenfd;
	newconn->subtype = AIM_CONN_SUBTYPE_OFT_DIRECTIM;
	newconn->internal = priv;
	newconn->lastactivity = time(NULL);

	return newconn;
}

/**
 * aim_sendfile_intitiate - For those times when we want to send the file ourselves.
 * @sess: your session,
 * @conn: the BOS conn,
 * @destsn: the SN to connect to.
 * @filename: the name of the files you want to send
 *
 */
aim_conn_t *aim_sendfile_initiate(aim_session_t *sess, const char *destsn, const char *filename, guint16 numfiles, guint32 totsize)
{ 
	aim_conn_t *newconn;
	aim_msgcookie_t *cookie;
	struct aim_directim_intdata *priv;
	int listenfd;
	guint16 port = 4443;
	guint8 localip[4];
	guint8 ck[8];

	if (getlocalip(localip) == -1)
		return NULL;

	if ((listenfd = listenestablish(port)) == -1)
		return NULL;

	aim_request_sendfile(sess, destsn, filename, numfiles, totsize, localip, port, ck);

	cookie = (aim_msgcookie_t *)g_new0(aim_msgcookie_t,1);
	memcpy(cookie->cookie, ck, 8);
	cookie->type = AIM_COOKIETYPE_OFTIM;

	/* this one is for the cookie */
	priv = (struct aim_directim_intdata *)g_new0(struct aim_directim_intdata,1);

	memcpy(priv->cookie, ck, 8);
	strncpy(priv->sn, destsn, sizeof(priv->sn));
	cookie->data = priv;
	aim_cachecookie(sess, cookie);

	/* XXX switch to aim_cloneconn()? */
	if (!(newconn = aim_newconn(sess, AIM_CONN_TYPE_RENDEZVOUS_OUT, NULL))) {
		closesocket(listenfd);
		return NULL;
	}

	/* this one is for the conn */
	priv = (struct aim_directim_intdata *)g_new0(struct aim_directim_intdata,1);

	memcpy(priv->cookie, ck, 8);
	strncpy(priv->sn, destsn, sizeof(priv->sn));

	newconn->fd = listenfd;
	newconn->subtype = AIM_CONN_SUBTYPE_OFT_SENDFILE;
	newconn->internal = priv;
	newconn->lastactivity = time(NULL);

	return newconn;
}

#if 0
/**
 * unsigned int aim_oft_listener_clean - close up old listeners
 * @sess: session to clean up in
 * @age: maximum age in seconds 
 *
 * returns number closed, -1 on error.
 */
unsigned int aim_oft_listener_clean(struct aim_session_t *sess, time_t age) 
{ 
  struct aim_conn_t *cur;
  time_t now;
  unsigned int hit = 0;
  
  if (!sess)
    return -1;
  now = time(NULL);
  faim_mutex_lock(&sess->connlistlock);
  for(cur = sess->connlist;cur; cur = cur->next)
    if (cur->type == AIM_CONN_TYPE_RENDEZVOUS_OUT) { 
      faim_mutex_lock(&cur->active);
      if (cur->lastactivity < (now - age) ) { 
	faim_mutex_unlock(&cur->active);
	aim_conn_close(cur);
	hit++;
      } else 
	faim_mutex_unlock(&cur->active);
    } 
  faim_mutex_unlock(&sess->connlistlock);
  return hit;
} 
#endif 

const char *aim_directim_getsn(aim_conn_t *conn)
{
	struct aim_directim_intdata *intdata;

	if (!conn)
	       return NULL;

	if ((conn->type != AIM_CONN_TYPE_RENDEZVOUS) || 
			(conn->subtype != AIM_CONN_SUBTYPE_OFT_DIRECTIM))
	       return NULL;

	if (!conn->internal)
		return NULL;

	intdata = (struct aim_directim_intdata *)conn->internal;

	return intdata->sn;
}

/**
 * aim_directim_connect - connect to buddy for directim
 * @sess: the session to append the conn to,
 * @sn: the SN we're connecting to
 * @addr: address to connect to
 *
 * This is a wrapper for aim_newconn.
 *
 * If addr is NULL, the socket is not created, but the connection is 
 * allocated and setup to connect.
 *
 */
aim_conn_t *aim_directim_connect(aim_session_t *sess, const char *sn, const char *addr, const guint8 *cookie)
{ 
	aim_conn_t *newconn;
	struct aim_directim_intdata *intdata;

	if (!sess || !sn)
		return NULL;

	if (!(intdata = g_new0(struct aim_directim_intdata, 1)))
		return NULL;

	memcpy(intdata->cookie, cookie, 8);
	strncpy(intdata->sn, sn, sizeof(intdata->sn));
	if (addr)
		strncpy(intdata->ip, addr, sizeof(intdata->ip));

	/* XXX verify that non-blocking connects actually work */
	if (!(newconn = aim_newconn(sess, AIM_CONN_TYPE_RENDEZVOUS, addr))) {
		g_free(intdata);
		return NULL;
	}

	if (!newconn) {
		g_free(intdata);
		return newconn;
	}

	newconn->subtype = AIM_CONN_SUBTYPE_OFT_DIRECTIM;
	newconn->internal = intdata;

	return newconn;
} 

/**
 * aim_directim_getconn - find a directim conn for buddy name
 * @sess: your session,
 * @name: the name to get,  
 *
 * returns conn for directim with name, %NULL if none found. 
 *
 */
aim_conn_t *aim_directim_getconn(aim_session_t *sess, const char *name)
{
	aim_conn_t *cur;

	if (!sess || !name || !strlen(name))
		return NULL;

	for (cur = sess->connlist; cur; cur = cur->next) {
		struct aim_directim_intdata *intdata;
		
		if ((cur->type != AIM_CONN_TYPE_RENDEZVOUS) || (cur->subtype != AIM_CONN_SUBTYPE_OFT_DIRECTIM))
			continue;

		intdata = cur->internal;

		if (aim_sncmp(intdata->sn, name) == 0)
			break;
	}

	return cur;
} 

/**
 * aim_accepttransfer - accept a file transfer request
 * @sess: the session,
 * @conn: the BOS conn for the CAP reply
 * @sn: the screenname to send it to,
 * @cookie: the cookie used
 * @ip: the ip to connect to
 * @listingfiles: number of files to share
 * @listingtotsize: total size of shared files
 * @listingsize: length of the listing file(buffer)
 * @listingchecksum: checksum of the listing
 * @rendid: capability type (%AIM_CAPS_GETFILE or %AIM_CAPS_SENDFILE)  
 *
 * Returns new connection or %NULL on error.
 *
 * XXX this should take a struct.
 */
aim_conn_t *aim_accepttransfer(aim_session_t *sess, 
						  aim_conn_t *conn, 
						  const char *sn, const guint8 *cookie, 
						  const guint8 *ip, 
						  guint16 listingfiles, 
						  guint16 listingtotsize, 
						  guint16 listingsize, 
						  guint32 listingchecksum, 
						  guint16 rendid)
{
       return NULL;	
#if 0
  struct command_tx_struct *newpacket, *newoft;
  struct aim_conn_t *newconn;
  struct aim_fileheader_t *fh;
  struct aim_filetransfer_priv *priv;
  struct aim_msgcookie_t *cachedcook;
  int curbyte, i;

  if (!sess || !conn || !sn || !cookie || !ip) {
    return NULL;
  }

  newconn = aim_newconn(sess, AIM_CONN_TYPE_RENDEZVOUS, ip);

  if (!newconn || (newconn->fd == -1)) {
    perror("aim_newconn");
    faimdprintf(sess, 2, "could not connect to %s (fd: %i)\n", ip, newconn?newconn->fd:0);
    return newconn;
  } else {
    priv = (struct aim_filetransfer_priv *)g_new0(struct aim_filetransfer_priv,1);

    memcpy(priv->cookie, cookie, 8);
    priv->state = 0;
    strncpy(priv->sn, sn, MAXSNLEN);
    strncpy(priv->ip, ip, sizeof(priv->ip));
    newconn->priv = (void *)priv;

    faimdprintf(sess, 2, "faim: connected to peer (fd = %d)\n", newconn->fd);
  }

  if (rendid == AIM_CAPS_GETFILE)  {
    newconn->subtype = AIM_CONN_SUBTYPE_OFT_GETFILE;

      faimdprintf(sess, 2, "faim: getfile request accept\n");

      if (!(newoft = aim_tx_new(sess, newconn, AIM_FRAMETYPE_OFT, 0x1108, 0))) { 
	faimdprintf(sess, 2, "faim: aim_accepttransfer: tx_new OFT failed\n");
	/* XXX: conn leak here */
	return NULL;
      } 

      newoft->lock = 1;
      memcpy(newoft->hdr.oft.magic, "OFT2", 4);
      newoft->hdr.oft.hdr2len = 0x100 - 8;

      if (!(fh = (struct aim_fileheader_t*)g_new0(struct aim_fileheader_t,1))) {
	/* XXX: conn leak here */
	perror("calloc");
	return NULL;
      }

      fh->encrypt = 0x0000;
      fh->compress = 0x0000;
      fh->totfiles = listingfiles;
      fh->filesleft = listingfiles;      /* is this right -- total parts and parts left?*/
      fh->totparts = 0x0001;
      fh->partsleft = 0x0001;
      fh->totsize = listingtotsize;
      fh->size = listingsize;      /* ls -l listing.txt */
      fh->modtime = (int)time(NULL); /* we'll go with current time for now */
      fh->checksum = listingchecksum;
      fh->rfcsum = 0x00000000;
      fh->rfsize = 0x00000000;
      fh->cretime = 0x00000000;
      fh->rfcsum = 0x00000000;
      fh->nrecvd = 0x00000000;
      fh->recvcsum = 0x00000000;
      memset(fh->idstring, 0, sizeof(fh->idstring));
      memcpy(fh->idstring, "OFT_Windows ICBMFT V1.1 32", sizeof(fh->idstring));
      fh->flags = 0x02;
      fh->lnameoffset = 0x1a;
      fh->lsizeoffset = 0x10;
      memset(fh->dummy, 0, sizeof(fh->dummy));
      memset(fh->macfileinfo, 0, sizeof(fh->macfileinfo));

      /* we need to figure out these encodings for filenames */
      fh->nencode = 0x0000;
      fh->nlanguage = 0x0000;
      memset(fh->name, 0, sizeof(fh->name));
      memcpy(fh->name, "listing.txt", sizeof(fh->name));

      if (!(newoft->hdr.oft.hdr2 = (char *)calloc(1,newoft->hdr.oft.hdr2len))) { 
	newoft->lock = 0;
	aim_frame_destroy(newoft);
	/* XXX: conn leak */
	perror("calloc (1)");
	return NULL;
      } 

      memcpy(fh->bcookie, cookie, 8);

      if (!(aim_oft_buildheader((unsigned char *)newoft->hdr.oft.hdr2, fh)))
	faimdprintf(sess, 1, "eek, bh fail!\n");

      newoft->lock = 0;
      aim_tx_enqueue(sess, newoft);
   
      if (!(cachedcook = (struct aim_msgcookie_t *)calloc(1, sizeof(struct aim_msgcookie_t)))) { 
	faimdprintf(sess, 1, "faim: accepttransfer: couldn't calloc cachedcook. yeep!\n");
	/* XXX: more cleanup, conn leak */
	perror("calloc (2)");
	return NULL;
      }

      memcpy(&(priv->fh), fh, sizeof(struct aim_fileheader_t));
      memcpy(cachedcook->cookie, cookie, 8);

      cachedcook->type = AIM_COOKIETYPE_OFTGET;
      cachedcook->data = (void *)priv;

      if (aim_cachecookie(sess, cachedcook) == -1)
	faimdprintf(sess, 1, "faim: ERROR caching message cookie\n");

      g_free(fh);     
 
      /* OSCAR CAP accept packet */
   
      if (!(newpacket = aim_tx_new(sess, conn, AIM_FRAMETYPE_OSCAR, 0x0002, 10+8+2+1+strlen(sn)+4+2+8+16))) {
	return NULL;
      }
  } else {
    return NULL;
  }
  
  newpacket->lock = 1;
  curbyte = aim_putsnac(newpacket->data, 0x0004, 0x0006, 0x0000, sess->snac_nextid);

  for (i = 0; i < 8; i++)
    curbyte += aimutil_put8(newpacket->data+curbyte, cookie[i]);

  curbyte += aimutil_put16(newpacket->data+curbyte, 0x0002);
  curbyte += aimutil_put8(newpacket->data+curbyte, strlen(sn));
  curbyte += aimutil_putstr(newpacket->data+curbyte, sn, strlen(sn));
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x0005);
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x001a);
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x0002 /* accept*/);

  for (i = 0;i < 8; i++) 
    curbyte += aimutil_put8(newpacket->data+curbyte, cookie[i]);

  curbyte += aim_putcap(newpacket->data+curbyte, 0x10, rendid);
  newpacket->lock = 0;
  aim_tx_enqueue(sess, newpacket);

  return newconn;
#endif
}

/**
 * aim_getlisting(FILE *file) -- get an aim_fileheader_t for a given FILE*
 *  @file is an opened listing file
 * 
 * returns a pointer to the filled-in fileheader_t
 *
 * Currently omits checksum. we'll fix this when AOL breaks us, i
 * guess.
 *
 */
struct aim_fileheader_t *aim_getlisting(aim_session_t *sess, FILE *file) 
{
	return NULL;
#if 0
  struct aim_fileheader_t *fh;
  u_long totsize = 0, size = 0, checksum = 0xffff0000;
  short totfiles = 0;
  char *linebuf, sizebuf[9];
  
  int linelength = 1024;

  /* XXX: if we have a line longer than 1024chars, God help us. */
  if ( (linebuf = (char *)calloc(1, linelength)) == NULL ) {
    faimdprintf(sess, 2, "linebuf calloc failed\n");
    return NULL;
  }  

  if (fseek(file, 0, SEEK_END) == -1) { /* use this for sanity check */
    perror("getlisting END1 fseek:");
    faimdprintf(sess, 2, "getlising fseek END1 error\n");
  }

  if ((size = ftell(file)) == -1) {
    perror("getlisting END1 getpos:");
    faimdprintf(sess, 2, "getlising getpos END1 error\n");
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    perror("getlesting fseek(SET):");
    faimdprintf(sess, 2, "faim: getlisting: couldn't seek to beginning of listing file\n");
  }

  memset(linebuf, 0, linelength);

  size = 0;

  while(fgets(linebuf,  linelength, file)) {
    totfiles++;
    memset(sizebuf, 0, 9);

    size += strlen(linebuf);
    
    if (strlen(linebuf) < 23) {
      faimdprintf(sess, 2, "line \"%s\" too short. skipping\n", linebuf);
      continue;
    }
    if (linebuf[strlen(linebuf)-1] != '\n') {
      faimdprintf(sess, 2, "faim: OFT: getlisting -- hit EOF or line too long!\n");
    }

    memcpy(sizebuf, linebuf+17, 8);

    totsize += strtol(sizebuf, NULL, 10);
    memset(linebuf, 0, linelength);
  }   

  if (fseek(file, 0, SEEK_SET) == -1) {
    perror("getlisting END2 fseek:");
    faimdprintf(sess, 2, "getlising fseek END2 error\n");
  }  

  g_free(linebuf);

  /* we're going to ignore checksumming the data for now -- that
   * requires walking the whole listing.txt. it should probably be
   * done at register time and cached, but, eh. */  

  if (!(fh = (struct aim_fileheader_t*)calloc(1, sizeof(struct aim_fileheader_t))))
    return NULL;

  fh->encrypt     = 0x0000;
  fh->compress    = 0x0000; 
  fh->totfiles    = totfiles;
  fh->filesleft   = totfiles; /* is this right ?*/
  fh->totparts    = 0x0001;
  fh->partsleft   = 0x0001;
  fh->totsize     = totsize;
  fh->size        = size; /* ls -l listing.txt */
  fh->modtime     = (int)time(NULL); /* we'll go with current time for now */
  fh->checksum    = checksum; /* XXX: checksum ! */
  fh->rfcsum      = 0x00000000;
  fh->rfsize      = 0x00000000;
  fh->cretime     = 0x00000000;
  fh->rfcsum      = 0x00000000;
  fh->nrecvd      = 0x00000000;
  fh->recvcsum    = 0x00000000;

  /*  memset(fh->idstring, 0, sizeof(fh->idstring)); */
  memcpy(fh->idstring, "OFT_Windows ICBMFT V1.1 32", sizeof(fh->idstring));
  memset(fh->idstring+strlen(fh->idstring), 0, sizeof(fh->idstring)-strlen(fh->idstring));

  fh->flags       = 0x02;
  fh->lnameoffset = 0x1a;
  fh->lsizeoffset = 0x10;

  /*  memset(fh->dummy, 0, sizeof(fh->dummy)); */
  memset(fh->macfileinfo, 0, sizeof(fh->macfileinfo));

  fh->nencode     = 0x0000; /* we need to figure out these encodings for filenames */
  fh->nlanguage   = 0x0000;

  /*  memset(fh->name, 0, sizeof(fh->name)); */
  memcpy(fh->name, "listing.txt", sizeof(fh->name));
  memset(fh->name+strlen(fh->name), 0, 64-strlen(fh->name));

  faimdprintf(sess, 2, "faim: OFT: listing fh name %s / %s\n", fh->name, (fh->name+(strlen(fh->name))));
  return fh;
#endif
}

/**
 * aim_listenestablish - create a listening socket on a port.
 * @portnum: the port number to bind to.  
 *
 * you need to call accept() when it's connected. returns your fd 
 *
 * XXX: give the client author the responsibility of setting up a
 * listener, then we no longer have a libfaim problem with broken
 * solaris *innocent smile* -jbm
 */
static int listenestablish(guint16 portnum)
{
#if HAVE_GETADDRINFO
	int listenfd;
	const int on = 1;
	struct addrinfo hints, *res, *ressave;
	char serv[5];

	g_snprintf(serv, sizeof(serv), "%d", portnum);
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(NULL /*any IP*/, serv, &hints, &res) != 0) {
		perror("getaddrinfo");
		return -1;
	} 
	ressave = res;
	do { 
		listenfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);    
		if (listenfd < 0)
			continue;
		setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
		if (bind(listenfd, res->ai_addr, res->ai_addrlen) == 0)
			break;
		/* success */
		closesocket(listenfd);
	} while ( (res = res->ai_next) );

	if (!res)
		return -1;

	if (listen(listenfd, 1024)!=0) { 
		perror("listen");
		return -1;
	} 

	freeaddrinfo(ressave);
	return listenfd;
#else
	int listenfd;
	const int on = 1;
	struct sockaddr_in sockin;

	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket(listenfd)");
		return -1;
	}

	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)) != 0) {
		perror("setsockopt(listenfd)");
		closesocket(listenfd);
		return -1;
	} 

	memset(&sockin, 0, sizeof(struct sockaddr_in));
	sockin.sin_family = AF_INET;
	sockin.sin_port = htons(portnum);

	if (bind(listenfd, (struct sockaddr *)&sockin, sizeof(struct sockaddr_in)) != 0) {
		perror("bind(listenfd)");
		closesocket(listenfd);
		return -1;
	}
	if (listen(listenfd, 4) != 0) {
		perror("listen(listenfd)");
		closesocket(listenfd);
		return -1;
	}
	return listenfd;
#endif
} 

static int getcommand_getfile(aim_session_t *sess, aim_conn_t *conn)
{
#if 0
	struct aim_filetransfer_priv *ft;
	aim_rxcallback_t userfunc;

	ft = conn->priv;
	if (ft->state == 2) {
		/* waiting on listing data */
		int ret = 0;
		char *listing;
		struct command_tx_struct *newoft;

		if (!(listing = g_malloc(ft->fh.size)))
			return -1;

		ft->state = 0;
		if (aim_recv(conn->fd, listing, ft->fh.size) != ft->fh.size)	
			faimdprintf(sess, 2, "OFT get: file %s was short. (0x%lx)\n", ft->fh.name, ft->fh.size);

		if (!(newoft = aim_tx_new(sess, conn, AIM_FRAMETYPE_OFT, 0x120b, 0))) {
			faimdprintf(sess, 2, "faim: aim_get_command_rendezvous: getfile listing: tx_new OFT failed\n");
			faim_mutex_unlock(&conn->active);
			g_free(listing);
			aim_conn_close(conn);
			return -1;
		}

		memcpy(newoft->hdr.oft.magic, "OFT2", 4);
		newoft->hdr.oft.hdr2len = 0x100 - 8;

		/* Protocol BS - set nrecvd to size of listing, recvcsum to listing checksum, flags to 0 */

		ft->fh.nrecvd = ft->fh.size;
		ft->fh.recvcsum = ft->fh.checksum;
		ft->fh.flags = 0;

		if (!(newoft->hdr.oft.hdr2 = (char *)calloc(1,newoft->hdr.oft.hdr2len))) {
			aim_frame_destroy(newoft);
			g_free(listing);
			return -1;
		}

		if (!(aim_oft_buildheader((unsigned char *)newoft->hdr.oft.hdr2, &(ft->fh))))
			faimdprintf(sess, 2, "eek! bh fail listing\n");

		/* send the 120b */
		aim_tx_enqueue(sess, newoft);
		if ( (userfunc = aim_callhandler(sess, conn, AIM_CB_FAM_OFT, AIM_CB_OFT_GETFILELISTING)) )
			ret = userfunc(sess, NULL, conn, ft, listing);

		g_free(listing);
		return ret;
	}

	if (ft->state == 3) { 
		/* waiting on file data */
		if ( (userfunc = aim_callhandler(sess, conn, AIM_CB_FAM_OFT, AIM_CB_OFT_GETFILERECEIVE)) ) 
			return userfunc(sess, NULL, conn, ft);
		return 0;
	}

	if (ft->state == 4) {
		if( (userfunc = aim_callhandler(sess, conn, AIM_CB_FAM_OFT, AIM_CB_OFT_GETFILESTATE4)) )
			return userfunc(sess, NULL, conn);
		aim_conn_close(conn);
		return 0;
	}	

	return 0;
#else
	return -1;
#endif
}

static void connclose_sendfile(aim_session_t *sess, aim_conn_t *conn)
{
	aim_msgcookie_t *cook;
	struct aim_filetransfer_priv *priv = (struct aim_filetransfer_priv *)conn->priv;

	cook = aim_uncachecookie(sess, (guint8 *)priv->cookie, AIM_COOKIETYPE_OFTSEND);
	aim_cookie_free(sess, cook);

	return;
}

static void connkill_sendfile(aim_session_t *sess, aim_conn_t *conn)
{
	
	g_free(conn->internal);

	return;
}

static void connclose_getfile(aim_session_t *sess, aim_conn_t *conn)
{
	aim_msgcookie_t *cook;
	struct aim_filetransfer_priv *priv = (struct aim_filetransfer_priv *)conn->priv;

	cook = aim_uncachecookie(sess, (guint8 *)priv->cookie, AIM_COOKIETYPE_OFTGET);
	aim_cookie_free(sess, cook);

	return;
}

static void connkill_getfile(aim_session_t *sess, aim_conn_t *conn)
{
	
	g_free(conn->internal);

	return;
}

static void connclose_directim(aim_session_t *sess, aim_conn_t *conn)
{
	struct aim_directim_intdata *intdata = (struct aim_directim_intdata *)conn->internal;
	aim_msgcookie_t *cook;

	cook = aim_uncachecookie(sess, intdata->cookie, AIM_COOKIETYPE_OFTIM);
	aim_cookie_free(sess, cook);

	return;
}

static void connkill_directim(aim_session_t *sess, aim_conn_t *conn)
{
	
	g_free(conn->internal);

	return;
}

void aim_conn_close_rend(aim_session_t *sess, aim_conn_t *conn)
{

	if (conn->type != AIM_CONN_TYPE_RENDEZVOUS)
		return;

	if (conn->subtype == AIM_CONN_SUBTYPE_OFT_SENDFILE)
		connclose_sendfile(sess, conn);
	else if (conn->subtype == AIM_CONN_SUBTYPE_OFT_GETFILE)
		connclose_getfile(sess, conn);
	else if (conn->subtype == AIM_CONN_SUBTYPE_OFT_DIRECTIM)
		connclose_directim(sess, conn);

	return;
}

void aim_conn_kill_rend(aim_session_t *sess, aim_conn_t *conn)
{

	if (conn->type != AIM_CONN_TYPE_RENDEZVOUS)
		return;

	if (conn->subtype == AIM_CONN_SUBTYPE_OFT_SENDFILE)
		connkill_sendfile(sess, conn);
	else if (conn->subtype == AIM_CONN_SUBTYPE_OFT_GETFILE)
		connkill_getfile(sess, conn);
	else if (conn->subtype == AIM_CONN_SUBTYPE_OFT_DIRECTIM)
		connkill_directim(sess, conn);

	return;
}

static int handlehdr_directim(aim_session_t *sess, aim_conn_t *conn, guint8 *hdr)
{
	aim_frame_t fr;
	aim_rxcallback_t userfunc;
	guint32 payloadlength;
	guint16 flags;
	char *snptr = NULL;

	fr.conn = conn;

	payloadlength = aimutil_get32(hdr+22);
	flags = aimutil_get16(hdr+32);
	snptr = (char *)hdr+38;

	if (flags == 0x000e) { 
		int ret = 0;

		if ((userfunc = aim_callhandler(sess, conn, AIM_CB_FAM_OFT, AIM_CB_OFT_DIRECTIMTYPING)))
			ret = userfunc(sess, &fr, snptr, 1);

		return ret;
	
	} else if (flags == 0x0002) {
		int ret = 0;
		
		if ((userfunc = aim_callhandler(sess, conn, AIM_CB_FAM_OFT, AIM_CB_OFT_DIRECTIMTYPING)))
			ret = userfunc(sess, &fr, snptr, 0);

		return ret;

	} else if ((flags == 0x0000) && payloadlength) { 
		char *msg, *msg2;
		int ret = 0;
		int recvd = 0;
		int i;

		if (!(msg = g_malloc(payloadlength+1)))
			return -1;
		memset(msg, 0, payloadlength+1);
		msg2 = msg;
		
		while (payloadlength - recvd) {
			if (payloadlength - recvd >= 1024)
				i = aim_recv(conn->fd, msg2, 1024);
			else 
				i = aim_recv(conn->fd, msg2, payloadlength - recvd);
			if (i <= 0) {
				g_free(msg);
				return -1;
			}
			recvd = recvd + i;
			msg2 = msg2 + i;
			if ((userfunc=aim_callhandler(sess, conn, AIM_CB_FAM_SPECIAL, AIM_CB_SPECIAL_IMAGETRANSFER)))
				userfunc(sess, &fr, snptr, (double)recvd / payloadlength);
		}
		
		if ( (userfunc = aim_callhandler(sess, conn, AIM_CB_FAM_OFT, AIM_CB_OFT_DIRECTIMINCOMING)) )
			ret = userfunc(sess, &fr, snptr, msg, payloadlength);

		g_free(msg);

		return ret;
	}

	return 0;
}

static int handlehdr_getfile_listing(aim_session_t *sess, aim_conn_t *conn, guint8 *hdr)
{
#if 0
	struct aim_filetransfer_priv *ft;
	struct aim_fileheader_t *fh;
	struct aim_msgcookie_t *cook;
	struct command_tx_struct *newoft;
	aim_rxcallback_t userfunc;

	faimdprintf(sess, 2,"faim: rend: fileget 0x1108\n");
	fh = aim_oft_getfh(hdr);

	faim_mutex_unlock(&conn->active);

	if (!(cook = aim_checkcookie(sess, fh->bcookie, AIM_COOKIETYPE_OFTGET))) {
		g_free(fh);
		return -1;
	}

	ft = cook->data;

	/* we're waaaaiiiting.. for listing.txt */
	ft->state = 2;

	memcpy(&(ft->fh), fh, sizeof(struct aim_fileheader_t));
	g_free(fh);

	if(aim_cachecookie(sess, cook) == -1) {
		faimdprintf(sess, 1, "error caching cookie\n");
		return -1;
	}     

	if (!(newoft = aim_tx_new(sess, conn, AIM_FRAMETYPE_OFT, 0x1209, 0))) {
		aim_conn_close(conn);
		return -1;
	}

	memcpy(newoft->hdr.oft.magic, "OFT2", 4);
	newoft->hdr.oft.hdr2len = 0x100 - 8;

	if (!(newoft->hdr.oft.hdr2 = (char *)calloc(1,newoft->hdr.oft.hdr2len))) {
		newoft->lock = 0;
		aim_frame_destroy(newoft);
		return -1;
	}

	if (!(aim_oft_buildheader((unsigned char *)newoft->hdr.oft.hdr2, &(ft->fh)))) {
		newoft->lock = 0;
		aim_frame_destroy(newoft);
		return -1;
	}

	newoft->lock = 0;
	aim_tx_enqueue(sess, newoft);
#endif
	return -1;
}

static int handlehdr_getfile_listing2(aim_session_t *sess, aim_conn_t *conn, guint8 *hdr)
{
#if 0
	struct aim_filetransfer_priv *ft;
	struct aim_fileheader_t *fh;
	struct aim_msgcookie_t *cook;
	int ret = 0;
	aim_rxcallback_t userfunc;
	
	fh = aim_oft_getfh(hdr);

	if (!(cook = aim_checkcookie(sess, fh->bcookie, AIM_COOKIETYPE_OFTGET)))
		faimdprintf(sess, 2, "shit, no cookie in 0x1209. (%i/%s)going to crash..\n",  AIM_COOKIETYPE_OFTGET, fh->bcookie);

	ft = cook->data;

	if (ft->fh.size != fh->size)
		faimdprintf(sess, 2, "hrm. ft->fh.size (%ld) != fh->size (%ld). um. using ft->fh.size\n", ft->fh.size, fh->size);

	if ( (userfunc = aim_callhandler(sess, conn, AIM_CB_FAM_OFT, AIM_CB_OFT_GETFILELISTINGREQ)))
		ret = userfunc(sess, NULL, conn, fh);

	faimdprintf(sess, 2, "faim: get_command_rendezvous: hit end of 1209\n");

	g_free(fh);

	return ret;
#else
	return -1;
#endif
}

static int handlehdr_getfile_listing3(aim_session_t *sess, aim_conn_t *conn, guint8 *hdr)
{
#if 0
	struct aim_filetransfer_priv *ft;
	struct aim_msgcookie_t *cook;
	struct aim_fileheader_t *fh;
	aim_rxcallback_t userfunc;

	fh = aim_oft_getfh(hdr);

	if (!(cook = aim_checkcookie(sess, fh->bcookie, AIM_COOKIETYPE_OFTGET))) {
		g_free(fh);
		return -1;
	}

	g_free(fh);

	ft = cook->data;

	if (aim_cachecookie(sess, cook) == -1)
		return -1;

	if ((userfunc = aim_callhandler(sess, conn, AIM_CB_FAM_OFT, AIM_CB_OFT_GETFILELISTINGRXCONFIRM)))
		return userfunc(sess, NULL, conn);
#endif
	return -1;
}

static int handlehdr_getfile_request(aim_session_t *sess, aim_conn_t *conn, guint8 *hdr)
{
#if 0
	struct aim_filetransfer_priv *ft;
	struct aim_msgcookie_t *cook;
	struct aim_fileheader_t *fh;
	struct command_tx_struct *newoft;
	int i = 0;
	aim_rxcallback_t userfunc;

	fh = aim_oft_getfh(hdr);

	if (!(cook = aim_checkcookie(sess, fh->bcookie, AIM_COOKIETYPE_OFTGET))) {
		g_free(fh);
		return -1;
	}

	ft = cook->data;
	memcpy(&(ft->fh), fh, sizeof(struct aim_fileheader_t));
	g_free(fh);

	aim_cachecookie(sess, cook);

	faimdprintf(sess, 2, "faim: fileget: %s seems to want %s\n", ft->sn, ft->fh.name);

	if ( (userfunc = aim_callhandler(sess, conn, AIM_CB_FAM_OFT, AIM_CB_OFT_GETFILEFILEREQ)) )
		i = userfunc(sess, NULL, conn, &(ft->fh), cook->cookie);

	if (i < 0)
		return i;

	if (!(newoft = aim_tx_new(sess, conn, AIM_FRAMETYPE_OFT, 0x0101, 0))) {
		faimdprintf(sess, 2, "faim: send_final_transfer: tx_new OFT failed\n");
		return -1;
	}

	newoft->lock = 1;
	memcpy(newoft->hdr.oft.magic, "OFT2", 4);
	newoft->hdr.oft.hdr2len = 0x100 - 8;

	if (!(newoft->hdr.oft.hdr2 = calloc(1,newoft->hdr.oft.hdr2len))) {
		aim_frame_destroy(newoft);
		return -1;
	} 

	/* protocol BS: nrecvd, recvcsum to 0, flags to 0x20. */
	ft->fh.nrecvd = 0;
	ft->fh.recvcsum = 0;
	ft->fh.flags = 0x20;

	aim_oft_buildheader((unsigned char *)newoft->hdr.oft.hdr2, &(ft->fh));

	newoft->lock = 0;
	aim_tx_enqueue(sess, newoft);

	faimdprintf(sess, 2, "faim: OFT: OFT file header enqueued.\n");

	return i;
#else
	return -1;
#endif
}

static int handlehdr_getfile_sending(aim_session_t *sess, aim_conn_t *conn, guint8 *hdr)
{
#if 0
	struct aim_fileheader_t *fh;
	struct aim_filetransfer_priv *ft;
	struct aim_msgcookie_t *cook;
	struct command_tx_struct *newoft;
	aim_rxcallback_t userfunc;

	fh = aim_oft_getfh(hdr);

	if (!(cook = aim_checkcookie(sess, fh->bcookie, AIM_COOKIETYPE_OFTGET))) {
		g_free(fh);
		return -1;
	}

	g_free(fh);

	ft = cook->data;

	ft->state = 3;

	if (aim_cachecookie(sess, cook) == -1)
		return -1;

	faimdprintf(sess, 2, "faim: fileget: %s seems to want to send %s\n", ft->sn, ft->fh.name);

	if (!(newoft = aim_tx_new(sess, conn, AIM_FRAMETYPE_OFT, 0x0202, 0))) {
		faimdprintf(sess, 2, "faim: send_final_transfer: tx_new OFT failed\n");
		return -1;
	}

	newoft->lock = 1;
	memcpy(newoft->hdr.oft.magic, "OFT2", 4);

	newoft->hdr.oft.hdr2len = 0x100 - 8;

	if (!(newoft->hdr.oft.hdr2 = calloc(1,newoft->hdr.oft.hdr2len))) {
		aim_frame_destroy(newoft);
		return -1;
	}

	aim_oft_buildheader((unsigned char *)newoft->hdr.oft.hdr2, &(ft->fh));

	newoft->lock = 0;
	aim_tx_enqueue(sess, newoft);

	faimdprintf(sess, 2, "faim: OFT: OFT 0x0202 enqueued.\n");

	if ( (userfunc = aim_callhandler(sess, conn, AIM_CB_FAM_OFT, AIM_CB_OFT_GETFILEFILEREQ)) == NULL)
		return 1;
#else
	return -1;
#endif
}

static int handlehdr_getfile_recv(aim_session_t *sess, aim_conn_t *conn, guint8 *hdr)
{
#if 0
	struct aim_fileheader_t *fh;
	struct aim_filetransfer_priv *ft;
	struct aim_msgcookie_t *cook;
	int ret = 1;
	aim_rxcallback_t userfunc;

	fh = aim_oft_getfh(hdr);

	if (!(cook = aim_checkcookie(sess, fh->bcookie, AIM_COOKIETYPE_OFTGET))) {
		g_free(fh);
		return -1;
	}

	ft = cook->data;

	faimdprintf(sess, 2, "faim: get_rend: looks like we're ready to send data.(oft 0x0202)\n");

	if ( (userfunc = aim_callhandler(sess, conn, AIM_CB_FAM_OFT, AIM_CB_OFT_GETFILEFILESEND)) )
		ret = userfunc(sess, NULL, conn, fh);

	g_free(fh);

	return ret;
#else
	return -1;
#endif
}

static int handlehdr_getfile_finish(aim_session_t *sess, aim_conn_t *conn, guint8 *hdr)
{
#if 0
	struct aim_fileheader_t *fh;
	aim_rxcallback_t userfunc;

	fh = aim_oft_getfh(hdr);

	faimdprintf(sess, 2, "faim: get_rend: looks like we're done with a transfer (oft 0x0204)\n");

	if ( (userfunc = aim_callhandler(sess, conn, AIM_CB_FAM_OFT, AIM_CB_OFT_GETFILECOMPLETE)) )
		userfunc(sess, NULL, conn, fh);

	g_free(fh);
#endif

	return -1;
}

/**
 * aim_get_command_rendezvous - OFT equivalent of aim_get_command
 * @sess: session to work on
 * @conn: conn to pull data from 
 *
 * this reads and handles data from conn->fd. currently a little rough
 * around the edges
 */
int aim_get_command_rendezvous(aim_session_t *sess, aim_conn_t *conn)
{
	guint8 hdrbuf1[6];
	guint8 *hdr = NULL;
	int hdrlen, hdrtype;
	int ret = -1;

	if (!sess || !conn)
		return -1;

	memset(hdrbuf1, 0, sizeof(hdrbuf1));

	/* I guess? I didn't understand any of that mess... */
	if (conn->subtype == AIM_CONN_SUBTYPE_OFT_GETFILE)
		return getcommand_getfile(sess, conn);

	/* XXX fix all the error cases here */
	if (aim_recv(conn->fd, hdrbuf1, 6) < 6) {

		do_error_dialog(sess->aux_data, "read error", "Gaim");

		aim_conn_close(conn);

		return -1;
	}

	hdrlen = aimutil_get16(hdrbuf1+4);
	hdrlen -= 6;

	hdr = g_malloc(hdrlen);

	if (aim_recv(conn->fd, hdr, hdrlen) < hdrlen) {
		do_error_dialog(sess->aux_data, "read error", "Gaim");
		g_free(hdr);
		aim_conn_close(conn);
		return -1;
	}

	hdrtype = aimutil_get16(hdr);

	if (hdrtype == 0x0001)
		ret = handlehdr_directim(sess, conn, hdr);
	else if (hdrtype == 0x1108) /* getfile listing.txt incoming tx->rx */
		ret = handlehdr_getfile_listing(sess, conn, hdr);
	else if (hdrtype == 0x1209) /* get file listing ack rx->tx */
		ret = handlehdr_getfile_listing2(sess, conn, hdr);
	else if (hdrtype == 0x120b) /* get file listing rx confirm */
		ret = handlehdr_getfile_listing3(sess, conn, hdr);
	else if (hdrtype == 0x120c) /* getfile request */
		ret = handlehdr_getfile_request(sess, conn, hdr);
	else if (hdrtype == 0x0101) /* getfile sending data */
		ret = handlehdr_getfile_sending(sess, conn, hdr);
	else if (hdrtype == 0x0202) /* getfile recv data */
		ret = handlehdr_getfile_recv(sess, conn, hdr);
	else if (hdrtype == 0x0204) /* getfile finished */
		ret = handlehdr_getfile_finish(sess, conn, hdr);
	else {
		ret = -1;
	}
	
	g_free(hdr);

	if (ret == -1)
		aim_conn_close(conn);

	return ret;
}

#if 0
/**
 * aim_oft_getfh - extracts an &aim_fileheader_t from buffer hdr.
 * @hdr: buffer to extract header from  
 *
 * returns pointer to new struct on success; %NULL on error.  
 *
 */
static struct aim_fileheader_t *aim_oft_getfh(unsigned char *hdr) 
{
  struct aim_fileheader_t *fh;
  int i, j;
  if (!(fh = calloc(1, sizeof(struct aim_fileheader_t))))
    return NULL;
  
  /* [0] and [1] are the type. we can ignore those here. */
  i = 2;
  for(j = 0; j < 8; j++, i++)
    fh->bcookie[j] = hdr[i];
  fh->encrypt = aimutil_get16(hdr+i);
  i += 2;
  fh->compress = aimutil_get16(hdr+i);
  i += 2;
  fh->totfiles = aimutil_get16(hdr+i);
  i += 2;
  fh->filesleft = aimutil_get16(hdr+i);
  i += 2;
  fh->totparts = aimutil_get16(hdr+i);
  i += 2;
  fh->partsleft = aimutil_get16(hdr+i);
  i += 2;
  fh->totsize = aimutil_get32(hdr+i);
  i += 4;
  fh->size = aimutil_get32(hdr+i);
  i += 4;
  fh->modtime = aimutil_get32(hdr+i);
  i += 4;
  fh->checksum = aimutil_get32(hdr+i);
  i += 4;
  fh->rfrcsum = aimutil_get32(hdr+i);
  i += 4;
  fh->rfsize = aimutil_get32(hdr+i);
  i += 4;
  fh->cretime = aimutil_get32(hdr+i);
  i += 4;
  fh->rfcsum = aimutil_get32(hdr+i);
  i += 4;
  fh->nrecvd = aimutil_get32(hdr+i);
  i += 4;
  fh->recvcsum = aimutil_get32(hdr+i);
  i += 4;
  memcpy(fh->idstring, hdr+i, 32);
  i += 32;
  fh->flags = aimutil_get8(hdr+i);
  i += 1;
  fh->lnameoffset = aimutil_get8(hdr+i);
  i += 1;
  fh->lsizeoffset = aimutil_get8(hdr+i);
  i += 1;
  memcpy(fh->dummy, hdr+i, 69);
  i += 69;
  memcpy(fh->macfileinfo, hdr+i, 16);
  i += 16;
  fh->nencode = aimutil_get16(hdr+i);
  i += 2;
  fh->nlanguage = aimutil_get16(hdr+i);
  i += 2;
  memcpy(fh->name, hdr+i, 64);
  i += 64;
  return fh;
} 
#endif

/**
 * aim_oft_checksum - calculate oft checksum of buffer
 * @buffer: buffer of data to checksum
 * @bufsize: size of buffer
 * @checksum: pointer to integer to place result in (pointer!) 
 *
 *
 * Note that checksum is a pointer. Checksum should be filled with
 * 0xFFFF0000 for each new file; you can have this checksum chunks of
 * files in series if you just call it repeatedly in a for(; ; ) loop
 * and don't reset the checksum between each call. And you thought we
 * didn't care about you and your pathetic client's meomry footprint
 * ;^) 
 *
 *
 * Also, it's been said that this is incorrect as currently
 * written. You were warned.
 */
guint32 aim_oft_checksum(aim_session_t *sess, const char *buffer, int bufsize, guint32 *checksum)
{
	return 0xdeadbeef;
#if 0
  guint16 check0, check1;
  int i;

  check0 = ((*checksum & 0xFF000000) >> 16);
  check1 = ((*checksum & 0x00ff0000) >> 16);
  for(i = 0; i < bufsize; i++) {
    if (i % 2) { /* use check1 -- second byte */
      if ( (short)buffer[i] > check1 ) { /* wrapping */
	check1 += 0x100;  /* this is a cheap way to wrap */

	/* if we're wrapping, decrement the other one */
	/* XXX: check this corner case */
	if (check0 == 0) 
	  check0 = 0x00ff;
	else 
	  check0--;
      } 
      check1 -= buffer[i];
    } else { /* use check0 -- first byte  */
      if ( (short)buffer[i] > check0 ) { /* wrapping */
	check0 += 0x100;       /* this is a cheap way to wrap */
  
	/* if we're wrapping, decrement the other one */
	/* XXX: check this corner case */
	if (check1 == 0) 
	  check1 = 0x00ff;
	else 
	  check1--;
      } 
      check0 -= buffer[i];
    } 
  }

  if (check0 > 0xff || check1 > 0xff)  { 
    /* they shouldn't be able to do this. error! */
    faimdprintf(sess, 2, "check0 or check1 is too high: 0x%04x, 0x%04x\n", check0, check1);
    return -1;
  } 

  /* grab just the lowest byte; this should be clean, but just in
     case */
  check0 &= 0xff;
  check1 &= 0xff;

  *checksum = ((check0 * 0x1000000) + (check1 * 0x10000));
  return *checksum;
#endif
} 

#if 0
/**
 * aim_oft_buildheader - fills a buffer with network-order fh data
 * @dest: buffer to fill -- pre-alloced
 * @fh: fh to get data from  
 *
 * returns length written; -1 on error.
 * DOES NOT DO BOUNDS CHECKING!
 *
 */
static int oft_buildheader(unsigned char *dest, struct aim_fileheader_t *fh) 
{ 
  int i, curbyte;
  if (!dest || !fh)
    return -1;
  curbyte = 0;
  for(i = 0; i < 8; i++) 
    curbyte += aimutil_put8(dest+curbyte, fh->bcookie[i]);
  curbyte += aimutil_put16(dest+curbyte, fh->encrypt);
  curbyte += aimutil_put16(dest+curbyte, fh->compress);
  curbyte += aimutil_put16(dest+curbyte, fh->totfiles);
  curbyte += aimutil_put16(dest+curbyte, fh->filesleft);
  curbyte += aimutil_put16(dest+curbyte, fh->totparts);
  curbyte += aimutil_put16(dest+curbyte, fh->partsleft);
  curbyte += aimutil_put32(dest+curbyte, fh->totsize);
  curbyte += aimutil_put32(dest+curbyte, fh->size);
  curbyte += aimutil_put32(dest+curbyte, fh->modtime);
  curbyte += aimutil_put32(dest+curbyte, fh->checksum);
  curbyte += aimutil_put32(dest+curbyte, fh->rfrcsum);
  curbyte += aimutil_put32(dest+curbyte, fh->rfsize);
  curbyte += aimutil_put32(dest+curbyte, fh->cretime);
  curbyte += aimutil_put32(dest+curbyte, fh->rfcsum);
  curbyte += aimutil_put32(dest+curbyte, fh->nrecvd);
  curbyte += aimutil_put32(dest+curbyte, fh->recvcsum);
  memcpy(dest+curbyte, fh->idstring, 32);
  curbyte += 32;
  curbyte += aimutil_put8(dest+curbyte, fh->flags);
  curbyte += aimutil_put8(dest+curbyte, fh->lnameoffset);
  curbyte += aimutil_put8(dest+curbyte, fh->lsizeoffset);
  memcpy(dest+curbyte, fh->dummy, 69);
  curbyte += 69;
  memcpy(dest+curbyte, fh->macfileinfo, 16);
  curbyte += 16;
  curbyte += aimutil_put16(dest+curbyte, fh->nencode);
  curbyte += aimutil_put16(dest+curbyte, fh->nlanguage);
  memset(dest+curbyte, 0x00, 64);
  memcpy(dest+curbyte, fh->name, 64);

  /* XXX: Filenames longer than 64B  */
  curbyte += 64;
  return curbyte;
}
#endif

/**
 * aim_getfile_intitiate - Request an OFT getfile session
 * @sess: your session,
 * @conn: the BOS conn,
 * @destsn is the SN to connect to.
 * 
 * returns a new &aim_conn_t on success, %NULL on error
 */
aim_conn_t *aim_getfile_initiate(aim_session_t *sess, aim_conn_t *conn, const char *destsn)
{ 
	return NULL;
#if 0
  struct command_tx_struct *newpacket;
  struct aim_conn_t *newconn;
  struct aim_filetransfer_priv *priv;
  struct aim_msgcookie_t *cookie;
  int curbyte, i, listenfd;
  short port = 4443;
  struct hostent *hptr;
  struct utsname myname;
  char cap[16];
  char d[4];
 
  /* Open our socket */

  if ( (listenfd = aim_listenestablish(port)) == -1)
    return NULL;

  /* get our local IP */

  if (uname(&myname) < 0)
    return NULL;
  if ( (hptr = gethostbyname(myname.nodename)) == NULL)
    return NULL;
  memcpy(&d, hptr->h_addr_list[0], 4);

  aim_putcap(cap, 16, AIM_CAPS_GETFILE);

  /* create the OSCAR packet */

  if (!(newpacket = aim_tx_new(sess, conn, AIM_FRAMETYPE_OSCAR, 0x0002, 10+8+2+1+strlen(destsn)+4+4+0x42)))
    return NULL;
  newpacket->lock = 1;

  /* lock struct */
  curbyte = 0;
  curbyte += aim_putsnac(newpacket->data+curbyte, 0x0004, 0x0006, 0x0000, sess->snac_nextid);

  /* XXX: check the cookie before commiting to using it */

  /* Generate a random message cookie
   * This cookie needs to be alphanumeric and NULL-terminated to be TOC-compatible. */
  for (i=0; i<7; i++) 
    curbyte += aimutil_put8(newpacket->data+curbyte, 0x30 + ((u_char) random() % 10));

  curbyte += aimutil_put8(newpacket->data+curbyte, 0x00);

  /* grab all the data for cookie caching. */
 
  if (!(cookie = (struct aim_msgcookie_t *)calloc(1, sizeof(struct aim_msgcookie_t))))
    return NULL;
  memcpy(cookie->cookie, newpacket->data+curbyte-8, 8);
  cookie->type = AIM_COOKIETYPE_OFTGET;

  if (!(priv = (struct aim_filetransfer_priv *)calloc(1, sizeof(struct aim_filetransfer_priv))))
    return NULL;
  memcpy(priv->cookie, cookie, 8);
  memcpy(priv->sn, destsn, sizeof(priv->sn));
  memcpy(priv->fh.name, "listing.txt", strlen("listing.txt"));
  priv->state = 1;

  cookie->data = priv;

  aim_cachecookie(sess, cookie);

  /* Channel ID */
  curbyte += aimutil_put16(newpacket->data+curbyte,0x0002);

  /* Destination SN (prepended with byte length) */
  curbyte += aimutil_put8(newpacket->data+curbyte,strlen(destsn));
  curbyte += aimutil_putstr(newpacket->data+curbyte, destsn, strlen(destsn));
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x0003);
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x0000);

  /* enTLV start */
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x0005);
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x0042);

  /* Flag data / ICBM Parameters? */
  curbyte += aimutil_put8(newpacket->data+curbyte, 0x00);
  curbyte += aimutil_put8(newpacket->data+curbyte, 0x00);

  /* Cookie */
  curbyte += aimutil_putstr(newpacket->data+curbyte, (char *)cookie, 8);

  /* Capability String */
  curbyte += aimutil_putstr(newpacket->data+curbyte, (char *)cap, 0x10);

  /* 000a/0002 : 0001 */
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x000a);
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x0002);
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x0001);

  /* 0003/0004: IP address */
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x0003);
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x0004);
  for(i = 0; i < 4; i++)
    curbyte += aimutil_put8(newpacket->data+curbyte, d[i]);

  /* already in network byte order  */
 
  /* 0005/0002: Port */
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x0005);
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x0002);
  curbyte += aimutil_put16(newpacket->data+curbyte, port);

  /* 000f/0000: ?? */
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x000f);
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x0000);

  /* 2711/000c: ?? */
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x2711);
  curbyte += aimutil_put16(newpacket->data+curbyte, 0x000c);
  curbyte += aimutil_put32(newpacket->data+curbyte, 0x00120001);

  for(i = 0; i < 0x000c - 4; i++)
    curbyte += aimutil_put8(newpacket->data+curbyte, 0x00);

  newpacket->commandlen = curbyte;
  newpacket->lock = 0;
  aim_tx_enqueue(sess, newpacket);

  /* allocate and set up our connection */

  i = fcntl(listenfd, F_GETFL, 0);
  fcntl(listenfd, F_SETFL, i | O_NONBLOCK);
  newconn = aim_newconn(sess, AIM_CONN_TYPE_RENDEZVOUS_OUT, NULL);

  if (!newconn){ 
    perror("aim_newconn");
    return NULL;
  }

  newconn->fd = listenfd;
  newconn->subtype = AIM_CONN_SUBTYPE_OFT_GETFILE;
  newconn->priv = priv;

  return newconn;
#endif
}
 
/**
 * aim_oft_getfile_request - request a particular file over an established getfile connection
 * @sess: your session
 * @conn: the established OFT getfile connection
 * @name: filename to request
 * @size: size of the file 
 *
 *
 * returns -1 on error, 0 on successful enqueuing
 */
int aim_oft_getfile_request(aim_session_t *sess, aim_conn_t *conn, const char *name, int size)
{
	return -EINVAL;
#if 0
  struct command_tx_struct *newoft;
  struct aim_filetransfer_priv *ft;
  if (!sess || !conn || !conn->priv || !name)
    return -1;

  if (!(newoft = aim_tx_new(sess, conn, AIM_FRAMETYPE_OFT, 0x120c, 0))) {
    faimdprintf(sess, 2, "faim: aim_accepttransfer: tx_new OFT failed\n");
    return -1;
  }

  newoft->lock = 1;

  memcpy(newoft->hdr.oft.magic, "OFT2", 4);
  newoft->hdr.oft.hdr2len = 0x100 - 8;

  ft = (struct aim_filetransfer_priv *)conn->priv;
  ft->fh.filesleft = 1;
  ft->fh.totfiles = 1;
  ft->fh.totparts = 1;
  ft->fh.partsleft = 1;
  ft->fh.totsize = size;
  ft->fh.size = size;
  ft->fh.checksum = 0;
  memcpy(ft->fh.name, name, strlen(name));
  memset(ft->fh.name+strlen(name), 0, 1);

  if (!(newoft->hdr.oft.hdr2 = (unsigned char *)calloc(1,newoft->hdr.oft.hdr2len))) {
    newoft->lock = 0;
    aim_frame_destroy(newoft);
    return -1;
  }

  if (!(aim_oft_buildheader(newoft->hdr.oft.hdr2, &(ft->fh)))) {
    newoft->lock = 0;
    aim_frame_destroy(newoft);
    return -1;
  }

  newoft->lock = 0;

  aim_tx_enqueue(sess, newoft);
  return 0;
#endif
}
 
/**
 * aim_oft_getfile_ack - acknowledge a getfile download as complete
 * @sess: your session
 * @conn: the getfile conn to send the ack over 
 *
 * Call this function after you have read all the data in a particular
 * filetransfer. Returns -1 on error, 0 on apparent success
 *
 */
int aim_oft_getfile_ack(aim_session_t *sess, aim_conn_t *conn) 
{
	return -EINVAL;
#if 0
  struct command_tx_struct *newoft;
  struct aim_filetransfer_priv *ft;

  if (!sess || !conn || !conn->priv)
    return -1;

  if (!(newoft = aim_tx_new(sess, conn, AIM_FRAMETYPE_OFT, 0x0202, 0))) {
    faimdprintf(sess, 2, "faim: aim_accepttransfer: tx_new OFT failed\n");
    return -1;
  } 

  newoft->lock = 1;

  memcpy(newoft->hdr.oft.magic, "OFT2", 4);
  newoft->hdr.oft.hdr2len = 0x100-8;

 if (!(newoft->hdr.oft.hdr2 = (char *)calloc(1,newoft->hdr.oft.hdr2len))) { 
   newoft->lock = 0;
   aim_frame_destroy(newoft);
   return -1;
 }

 ft = (struct aim_filetransfer_priv *)conn->priv;

 if (!(aim_oft_buildheader((unsigned char *)newoft->hdr.oft.hdr2, &(ft->fh)))) {
   newoft->lock = 0;
   aim_frame_destroy(newoft);
   return -1;
 }

 newoft->lock = 0;
 aim_tx_enqueue(sess, newoft);
 return 0;
#endif
}
 
/**
 * aim_oft_getfile_end - end a getfile.
 * @sess: your session
 * @conn: the getfile connection 
 *
 * call this before you close the getfile connection if you're on the
 * receiving/requesting end.
 */
int aim_oft_getfile_end(aim_session_t *sess, aim_conn_t *conn)
{
	return -EINVAL;
#if 0
  struct command_tx_struct *newoft;
  struct aim_filetransfer_priv *ft;
  
  if (!sess || !conn || !conn->priv)
    return -1;

  if (!(newoft = aim_tx_new(sess, conn, AIM_FRAMETYPE_OFT, 0x0204, 0))) {
    faimdprintf(sess, 2, "faim: aim_accepttransfer: tx_new OFT failed\n");
    return -1;
  }
  
  newoft->lock = 1;
  
  memcpy(newoft->hdr.oft.magic, "OFT2", 4);
  newoft->hdr.oft.hdr2len = 0x100 - 8;
  
  if (!(newoft->hdr.oft.hdr2 = (char *)calloc(1,newoft->hdr.oft.hdr2len))) {
    newoft->lock = 0;
    aim_frame_destroy(newoft);
    return -1;
  }
  
  ft = (struct aim_filetransfer_priv *)conn->priv;
  ft->state = 4; /* no longer wanting data */
  ft->fh.nrecvd = ft->fh.size;
  ft->fh.recvcsum = ft->fh.checksum;
  ft->fh.flags = 0x21;
  
  if (!(aim_oft_buildheader((unsigned char *)newoft->hdr.oft.hdr2, &(ft->fh)))) {
    newoft->lock = 0;
    aim_frame_destroy(newoft);
    return -1;
  }
  
  newoft->lock = 0;
  aim_tx_enqueue(sess, newoft);
  
  return 0;
#endif /* 0 */
}

