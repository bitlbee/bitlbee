
/*
 * conn.c
 *
 * Does all this gloriously nifty connection handling stuff...
 *
 */

#include <aim.h>
#include "sock.h"

static int aim_logoff(aim_session_t *sess);

/*
 * In OSCAR, every connection has a set of SNAC groups associated
 * with it.  These are the groups that you can send over this connection
 * without being guaranteed a "Not supported" SNAC error.
 *
 * The grand theory of things says that these associations transcend
 * what libfaim calls "connection types" (conn->type).  You can probably
 * see the elegance here, but since I want to revel in it for a bit, you
 * get to hear it all spelled out.
 *
 * So let us say that you have your core BOS connection running.  One
 * of your modules has just given you a SNAC of the group 0x0004 to send
 * you.  Maybe an IM destined for some twit in Greenland.  So you start
 * at the top of your connection list, looking for a connection that
 * claims to support group 0x0004.  You find one.  Why, that neat BOS
 * connection of yours can do that.  So you send it on its way.
 *
 * Now, say, that fellow from Greenland has friends and they all want to
 * meet up with you in a lame chat room.  This has landed you a SNAC
 * in the family 0x000e and you have to admit you're a bit lost.  You've
 * searched your connection list for someone who wants to make your life
 * easy and deliver this SNAC for you, but there isn't one there.
 *
 * Here comes the good bit.  Without even letting anyone know, particularly
 * the module that decided to send this SNAC, and definitely not that twit
 * in Greenland, you send out a service request.  In this request, you have
 * marked the need for a connection supporting group 0x000e.  A few seconds
 * later, you receive a service redirect with an IP address and a cookie in
 * it.  Great, you say.  Now I have something to do.  Off you go, making
 * that connection.  One of the first things you get from this new server
 * is a message saying that indeed it does support the group you were looking
 * for.  So you continue and send rate confirmation and all that.
 *
 * Then you remember you had that SNAC to send, and now you have a means to
 * do it, and you do, and everyone is happy.  Except the Greenlander, who is
 * still stuck in the bitter cold.
 *
 * Oh, and this is useful for building the Migration SNACs, too.  In the
 * future, this may help convince me to implement rate limit mitigation
 * for real.  We'll see.
 *
 * Just to make me look better, I'll say that I've known about this great
 * scheme for quite some time now.  But I still haven't convinced myself
 * to make libfaim work that way.  It would take a fair amount of effort,
 * and probably some client API changes as well.  (Whenever I don't want
 * to do something, I just say it would change the client API.  Then I
 * instantly have a couple of supporters of not doing it.)
 *
 * Generally, addgroup is only called by the internal handling of the
 * server ready SNAC.  So if you want to do something before that, you'll
 * have to be more creative.  That is done rather early, though, so I don't
 * think you have to worry about it.  Unless you're me.  I care deeply
 * about such inane things.
 *
 */
void aim_conn_addgroup(aim_conn_t *conn, guint16 group)
{
	aim_conn_inside_t *ins = (aim_conn_inside_t *) conn->inside;
	struct snacgroup *sg;

	if (!(sg = g_malloc(sizeof(struct snacgroup)))) {
		return;
	}

	sg->group = group;

	sg->next = ins->groups;
	ins->groups = sg;

	return;
}

aim_conn_t *aim_conn_findbygroup(aim_session_t *sess, guint16 group)
{
	aim_conn_t *cur;

	for (cur = sess->connlist; cur; cur = cur->next) {
		aim_conn_inside_t *ins = (aim_conn_inside_t *) cur->inside;
		struct snacgroup *sg;

		for (sg = ins->groups; sg; sg = sg->next) {
			if (sg->group == group) {
				return cur;
			}
		}
	}

	return NULL;
}

static void connkill_snacgroups(struct snacgroup **head)
{
	struct snacgroup *sg;

	for (sg = *head; sg; ) {
		struct snacgroup *tmp;

		tmp = sg->next;
		g_free(sg);
		sg = tmp;
	}

	*head = NULL;

	return;
}

static void connkill_rates(struct rateclass **head)
{
	struct rateclass *rc;

	for (rc = *head; rc; ) {
		struct rateclass *tmp;
		struct snacpair *sp;

		tmp = rc->next;

		for (sp = rc->members; sp; ) {
			struct snacpair *tmpsp;

			tmpsp = sp->next;
			g_free(sp);
			sp = tmpsp;
		}
		g_free(rc);

		rc = tmp;
	}

	*head = NULL;

	return;
}

static void connkill_real(aim_session_t *sess, aim_conn_t **deadconn)
{

	aim_rxqueue_cleanbyconn(sess, *deadconn);
	aim_tx_cleanqueue(sess, *deadconn);

	if ((*deadconn)->fd != -1) {
		aim_conn_close(*deadconn);
	}

	/*
	 * XXX ->priv should never be touched by the library. I know
	 * it used to be, but I'm getting rid of all that.  Use
	 * ->internal instead.
	 */
	if ((*deadconn)->priv) {
		g_free((*deadconn)->priv);
	}

	/*
	 * This will free ->internal if it necessary...
	 */
	if ((*deadconn)->type == AIM_CONN_TYPE_CHAT) {
		aim_conn_kill_chat(sess, *deadconn);
	}

	if ((*deadconn)->inside) {
		aim_conn_inside_t *inside = (aim_conn_inside_t *) (*deadconn)->inside;

		connkill_snacgroups(&inside->groups);
		connkill_rates(&inside->rates);

		g_free(inside);
	}

	g_free(*deadconn);
	*deadconn = NULL;

	return;
}

/**
 * aim_connrst - Clears out connection list, killing remaining connections.
 * @sess: Session to be cleared
 *
 * Clears out the connection list and kills any connections left.
 *
 */
static void aim_connrst(aim_session_t *sess)
{

	if (sess->connlist) {
		aim_conn_t *cur = sess->connlist, *tmp;

		while (cur) {
			tmp = cur->next;
			aim_conn_close(cur);
			connkill_real(sess, &cur);
			cur = tmp;
		}
	}

	sess->connlist = NULL;

	return;
}

/**
 * aim_conn_init - Reset a connection to default values.
 * @deadconn: Connection to be reset
 *
 * Initializes and/or resets a connection structure.
 *
 */
static void aim_conn_init(aim_conn_t *deadconn)
{

	if (!deadconn) {
		return;
	}

	deadconn->fd = -1;
	deadconn->subtype = -1;
	deadconn->type = -1;
	deadconn->seqnum = 0;
	deadconn->lastactivity = 0;
	deadconn->forcedlatency = 0;
	deadconn->handlerlist = NULL;
	deadconn->priv = NULL;
	memset(deadconn->inside, 0, sizeof(aim_conn_inside_t));

	return;
}

/**
 * aim_conn_getnext - Gets a new connection structure.
 * @sess: Session
 *
 * Allocate a new empty connection structure.
 *
 */
static aim_conn_t *aim_conn_getnext(aim_session_t *sess)
{
	aim_conn_t *newconn;

	if (!(newconn = g_new0(aim_conn_t, 1))) {
		return NULL;
	}

	if (!(newconn->inside = g_new0(aim_conn_inside_t, 1))) {
		g_free(newconn);
		return NULL;
	}

	aim_conn_init(newconn);

	newconn->next = sess->connlist;
	sess->connlist = newconn;

	return newconn;
}

/**
 * aim_conn_kill - Close and free a connection.
 * @sess: Session for the connection
 * @deadconn: Connection to be freed
 *
 * Close, clear, and free a connection structure. Should never be
 * called from within libfaim.
 *
 */
void aim_conn_kill(aim_session_t *sess, aim_conn_t **deadconn)
{
	aim_conn_t *cur, **prev;

	if (!deadconn || !*deadconn) {
		return;
	}

	for (prev = &sess->connlist; (cur = *prev); ) {
		if (cur == *deadconn) {
			*prev = cur->next;
			break;
		}
		prev = &cur->next;
	}

	if (!cur) {
		return; /* oops */

	}
	connkill_real(sess, &cur);

	return;
}

/**
 * aim_conn_close - Close a connection
 * @deadconn: Connection to close
 *
 * Close (but not free) a connection.
 *
 * This leaves everything untouched except for clearing the
 * handler list and setting the fd to -1 (used to recognize
 * dead connections).  It will also remove cookies if necessary.
 *
 */
void aim_conn_close(aim_conn_t *deadconn)
{

	if (deadconn->fd >= 3) {
		closesocket(deadconn->fd);
	}
	deadconn->fd = -1;
	if (deadconn->handlerlist) {
		aim_clearhandlers(deadconn);
	}

	return;
}

/**
 * aim_getconn_type - Find a connection of a specific type
 * @sess: Session to search
 * @type: Type of connection to look for
 *
 * Searches for a connection of the specified type in the
 * specified session.  Returns the first connection of that
 * type found.
 *
 * XXX except for RENDEZVOUS, all uses of this should be removed and
 * use aim_conn_findbygroup() instead.
 */
aim_conn_t *aim_getconn_type(aim_session_t *sess, int type)
{
	aim_conn_t *cur;

	for (cur = sess->connlist; cur; cur = cur->next) {
		if ((cur->type == type) &&
		    !(cur->status & AIM_CONN_STATUS_INPROGRESS)) {
			break;
		}
	}

	return cur;
}

aim_conn_t *aim_getconn_type_all(aim_session_t *sess, int type)
{
	aim_conn_t *cur;

	for (cur = sess->connlist; cur; cur = cur->next) {
		if (cur->type == type) {
			break;
		}
	}

	return cur;
}

/**
 * aim_newconn - Open a new connection
 * @sess: Session to create connection in
 * @type: Type of connection to create
 * @dest: Host to connect to (in "host:port" syntax)
 *
 * Opens a new connection to the specified dest host of specified
 * type, using the proxy settings if available.  If @host is %NULL,
 * the connection is allocated and returned, but no connection
 * is made.
 *
 * FIXME: Return errors in a more sane way.
 *
 */
aim_conn_t *aim_newconn(aim_session_t *sess, int type, const char *dest)
{
	aim_conn_t *connstruct;
	guint16 port = AIM_LOGIN_PORT;
	char *host;
	int i;

	if (!(connstruct = aim_conn_getnext(sess))) {
		return NULL;
	}

	connstruct->sessv = (void *) sess;
	connstruct->type = type;

	if (!dest) { /* just allocate a struct */
		connstruct->fd = -1;
		connstruct->status = 0;
		return connstruct;
	}

	/*
	 * As of 23 Jul 1999, AOL now sends the port number, preceded by a
	 * colon, in the BOS redirect.  This fatally breaks all previous
	 * libfaims.  Bad, bad AOL.
	 *
	 * We put this here to catch every case.
	 *
	 */

	for (i = 0; i < (int) strlen(dest); i++) {
		if (dest[i] == ':') {
			port = atoi(&(dest[i + 1]));
			break;
		}
	}

	host = (char *) g_malloc(i + 1);
	strncpy(host, dest, i);
	host[i] = '\0';

	connstruct->fd = proxy_connect(host, port, NULL, NULL);

	g_free(host);

	return connstruct;
}

/**
 * aim_conn_setlatency - Set a forced latency value for connection
 * @conn: Conn to set latency for
 * @newval: Number of seconds to force between transmits
 *
 * Causes @newval seconds to be spent between transmits on a connection.
 *
 * This is my lame attempt at overcoming not understanding the rate
 * limiting.
 *
 * XXX: This should really be replaced with something that scales and
 * backs off like the real rate limiting does.
 *
 */
int aim_conn_setlatency(aim_conn_t *conn, int newval)
{

	if (!conn) {
		return -1;
	}

	conn->forcedlatency = newval;
	conn->lastactivity = 0; /* reset this just to make sure */

	return 0;
}

/**
 * aim_session_init - Initializes a session structure
 * @sess: Session to initialize
 * @flags: Flags to use. Any of %AIM_SESS_FLAGS %OR'd together.
 * @debuglevel: Level of debugging output (zero is least)
 *
 * Sets up the initial values for a session.
 *
 */
void aim_session_init(aim_session_t *sess, guint32 flags, int debuglevel)
{

	if (!sess) {
		return;
	}

	memset(sess, 0, sizeof(aim_session_t));
	aim_connrst(sess);
	sess->queue_outgoing = NULL;
	sess->queue_incoming = NULL;
	aim_initsnachash(sess);
	sess->msgcookies = NULL;
	sess->snacid_next = 0x00000001;

	sess->flags = 0;

	sess->modlistv = NULL;

	sess->ssi.received_data = 0;
	sess->ssi.waiting_for_ack = 0;
	sess->ssi.holding_queue = NULL;
	sess->ssi.revision = 0;
	sess->ssi.items = NULL;
	sess->ssi.timestamp = (time_t) 0;

	sess->locate.userinfo = NULL;
	sess->locate.torequest = NULL;
	sess->locate.requested = NULL;
	sess->locate.waiting_for_response = FALSE;

	sess->icq_info = NULL;
	sess->authinfo = NULL;
	sess->emailinfo = NULL;
	sess->oft_info = NULL;


	/*
	 * Default to SNAC login unless XORLOGIN is explicitly set.
	 */
	if (!(flags & AIM_SESS_FLAGS_XORLOGIN)) {
		sess->flags |= AIM_SESS_FLAGS_SNACLOGIN;
	}
	sess->flags |= flags;

	/*
	 * This must always be set.  Default to the queue-based
	 * version for back-compatibility.
	 */
	aim_tx_setenqueue(sess, AIM_TX_QUEUED, NULL);


	/*
	 * Register all the modules for this session...
	 */
	aim__registermodule(sess, misc_modfirst); /* load the catch-all first */
	aim__registermodule(sess, general_modfirst);
	aim__registermodule(sess, locate_modfirst);
	aim__registermodule(sess, buddylist_modfirst);
	aim__registermodule(sess, msg_modfirst);
	aim__registermodule(sess, admin_modfirst);
	aim__registermodule(sess, bos_modfirst);
	aim__registermodule(sess, search_modfirst);
	aim__registermodule(sess, stats_modfirst);
	aim__registermodule(sess, chatnav_modfirst);
	aim__registermodule(sess, chat_modfirst);
	/* missing 0x0f - 0x12 */
	aim__registermodule(sess, ssi_modfirst);
	/* missing 0x14 */
	aim__registermodule(sess, icq_modfirst);
	/* missing 0x16 */
	aim__registermodule(sess, auth_modfirst);

	return;
}

/**
 * aim_session_kill - Deallocate a session
 * @sess: Session to kill
 *
 */
void aim_session_kill(aim_session_t *sess)
{
	aim_cleansnacs(sess, -1);

	aim_logoff(sess);

	aim__shutdownmodules(sess);

	return;
}

/*
 * XXX this is nearly as ugly as proxyconnect().
 */
int aim_conn_completeconnect(aim_session_t *sess, aim_conn_t *conn)
{
	fd_set fds, wfds;
	struct timeval tv;
	int res, error = ETIMEDOUT;
	aim_rxcallback_t userfunc;

	if (!conn || (conn->fd == -1)) {
		return -1;
	}

	if (!(conn->status & AIM_CONN_STATUS_INPROGRESS)) {
		return -1;
	}

	FD_ZERO(&fds);
	FD_SET(conn->fd, &fds);
	FD_ZERO(&wfds);
	FD_SET(conn->fd, &wfds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if ((res = select(conn->fd + 1, &fds, &wfds, NULL, &tv)) == -1) {
		error = errno;
		aim_conn_close(conn);
		errno = error;
		return -1;
	} else if (res == 0) {
		return 0; /* hasn't really completed yet... */
	}

	if (FD_ISSET(conn->fd, &fds) || FD_ISSET(conn->fd, &wfds)) {
		socklen_t len = sizeof(error);

		if (getsockopt(conn->fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
			error = errno;
		}
	}

	if (error) {
		aim_conn_close(conn);
		errno = error;
		return -1;
	}

	sock_make_blocking(conn->fd);

	conn->status &= ~AIM_CONN_STATUS_INPROGRESS;

	if ((userfunc = aim_callhandler(sess, conn, AIM_CB_FAM_SPECIAL, AIM_CB_SPECIAL_CONNCOMPLETE))) {
		userfunc(sess, NULL, conn);
	}

	/* Flush out the queues if there was something waiting for this conn  */
	aim_tx_flushqueue(sess);

	return 0;
}

aim_session_t *aim_conn_getsess(aim_conn_t *conn)
{

	if (!conn) {
		return NULL;
	}

	return (aim_session_t *) conn->sessv;
}

/*
 * aim_logoff()
 *
 * Closes -ALL- open connections.
 *
 */
static int aim_logoff(aim_session_t *sess)
{

	aim_connrst(sess);  /* in case we want to connect again */

	return 0;

}

/*
 * aim_flap_nop()
 *
 * No-op.  WinAIM 4.x sends these _every minute_ to keep
 * the connection alive.
 */
int aim_flap_nop(aim_session_t *sess, aim_conn_t *conn)
{
	aim_frame_t *fr;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x05, 0))) {
		return -ENOMEM;
	}

	aim_tx_enqueue(sess, fr);

	return 0;
}


