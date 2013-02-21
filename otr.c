  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
  \********************************************************************/

/*
  OTR support (cf. http://www.cypherpunks.ca/otr/)
  
  (c) 2008-2011 Sven Moritz Hallberg <pesco@khjk.org>
  (c) 2008 funded by stonedcoder.org
    
  files used to store OTR data:
    <configdir>/<nick>.otr_keys
    <configdir>/<nick>.otr_fprints
    
  top-level todos: (search for TODO for more ;-))
    integrate otr_load/otr_save with existing storage backends
    per-account policy settings
    per-user policy settings
*/

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

#include "bitlbee.h"
#include "irc.h"
#include "otr.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>


/** OTR interface routines for the OtrlMessageAppOps struct: **/

OtrlPolicy op_policy(void *opdata, ConnContext *context);

void op_create_privkey(void *opdata, const char *accountname, const char *protocol);

int op_is_logged_in(void *opdata, const char *accountname, const char *protocol,
	const char *recipient);

void op_inject_message(void *opdata, const char *accountname, const char *protocol,
	const char *recipient, const char *message);

int op_display_otr_message(void *opdata, const char *accountname, const char *protocol,
	const char *username, const char *msg);

void op_new_fingerprint(void *opdata, OtrlUserState us, const char *accountname,
	const char *protocol, const char *username, unsigned char fingerprint[20]);

void op_write_fingerprints(void *opdata);

void op_gone_secure(void *opdata, ConnContext *context);

void op_gone_insecure(void *opdata, ConnContext *context);

void op_still_secure(void *opdata, ConnContext *context, int is_reply);

void op_log_message(void *opdata, const char *message);

int op_max_message_size(void *opdata, ConnContext *context);

const char *op_account_name(void *opdata, const char *account, const char *protocol);


/** otr sub-command handlers: **/

static void cmd_otr(irc_t *irc, char **args);
void cmd_otr_connect(irc_t *irc, char **args);
void cmd_otr_disconnect(irc_t *irc, char **args);
void cmd_otr_reconnect(irc_t *irc, char **args);
void cmd_otr_smp(irc_t *irc, char **args);
void cmd_otr_smpq(irc_t *irc, char **args);
void cmd_otr_trust(irc_t *irc, char **args);
void cmd_otr_info(irc_t *irc, char **args);
void cmd_otr_keygen(irc_t *irc, char **args);
void cmd_otr_forget(irc_t *irc, char **args);

const command_t otr_commands[] = {
	{ "connect",     1, &cmd_otr_connect,    0 },
	{ "disconnect",  1, &cmd_otr_disconnect, 0 },
	{ "reconnect",   1, &cmd_otr_reconnect,  0 },
	{ "smp",         2, &cmd_otr_smp,        0 },
	{ "smpq",        3, &cmd_otr_smpq,       0 },
	{ "trust",       6, &cmd_otr_trust,      0 },
	{ "info",        0, &cmd_otr_info,       0 },
	{ "keygen",      1, &cmd_otr_keygen,     0 },
	{ "forget",      2, &cmd_otr_forget,     0 },
	{ NULL }
};

typedef struct {
	void *fst;
	void *snd;
} pair_t;	

static OtrlMessageAppOps otr_ops;   /* collects interface functions required by OTR */


/** misc. helpers/subroutines: **/

/* check whether we are already generating a key for a given account */
int keygen_in_progress(irc_t *irc, const char *handle, const char *protocol);

/* start background process to generate a (new) key for a given account */
void otr_keygen(irc_t *irc, const char *handle, const char *protocol);

/* main function for the forked keygen slave */
void keygen_child_main(OtrlUserState us, int infd, int outfd);

/* mainloop handler for when a keygen finishes */
gboolean keygen_finish_handler(gpointer data, gint fd, b_input_condition cond);

/* copy the contents of file a to file b, overwriting it if it exists */
void copyfile(const char *a, const char *b);

/* read one line of input from a stream, excluding trailing newline */
void myfgets(char *s, int size, FILE *stream);

/* some yes/no handlers */
void yes_keygen(void *data);
void yes_forget_fingerprint(void *data);
void yes_forget_context(void *data);
void yes_forget_key(void *data);

/* helper to make sure accountname and protocol match the incoming "opdata" */
struct im_connection *check_imc(void *opdata, const char *accountname,
	const char *protocol);

/* determine the nick for a given handle/protocol pair
   returns "handle/protocol" if not found */
const char *peernick(irc_t *irc, const char *handle, const char *protocol);

/* turn a hexadecimal digit into its numerical value */
int hexval(char a);

/* determine the irc_user_t for a given handle/protocol pair
   returns NULL if not found */
irc_user_t *peeruser(irc_t *irc, const char *handle, const char *protocol);

/* handle SMP TLVs from a received message */
void otr_handle_smp(struct im_connection *ic, const char *handle, OtrlTLV *tlvs);

/* combined handler for the 'otr smp' and 'otr smpq' commands */
void otr_smp_or_smpq(irc_t *irc, const char *nick, const char *question,
		const char *secret);

/* update flags within the irc_user structure to reflect OTR status of context */
void otr_update_uflags(ConnContext *context, irc_user_t *u);

/* update op/voice flag of given user according to encryption state and settings
   returns 0 if neither op_buddies nor voice_buddies is set to "encrypted",
   i.e. msgstate should be announced seperately */
int otr_update_modeflags(irc_t *irc, irc_user_t *u);

/* show general info about the OTR subsystem; called by 'otr info' */
void show_general_otr_info(irc_t *irc);

/* show info about a given OTR context */
void show_otr_context_info(irc_t *irc, ConnContext *ctx);

/* show the list of fingerprints associated with a given context */
void show_fingerprints(irc_t *irc, ConnContext *ctx);

/* find a fingerprint by prefix (given as any number of hex strings) */
Fingerprint *match_fingerprint(irc_t *irc, ConnContext *ctx, const char **args);

/* find a private key by fingerprint prefix (given as any number of hex strings) */
OtrlPrivKey *match_privkey(irc_t *irc, const char **args);

/* check whether a string is safe to use in a path component */
int strsane(const char *s);

/* functions to be called for certain events */
static const struct irc_plugin otr_plugin;


/*** routines declared in otr.h: ***/

#ifdef OTR_BI
#define init_plugin otr_init
#endif

void init_plugin(void)
{
	OTRL_INIT;
	
	/* fill global OtrlMessageAppOps */
	otr_ops.policy = &op_policy;
	otr_ops.create_privkey = &op_create_privkey;
	otr_ops.is_logged_in = &op_is_logged_in;
	otr_ops.inject_message = &op_inject_message;
	otr_ops.notify = NULL;
	otr_ops.display_otr_message = &op_display_otr_message;
	otr_ops.update_context_list = NULL;
	otr_ops.protocol_name = NULL;
	otr_ops.protocol_name_free = NULL;
	otr_ops.new_fingerprint = &op_new_fingerprint;
	otr_ops.write_fingerprints = &op_write_fingerprints;
	otr_ops.gone_secure = &op_gone_secure;
	otr_ops.gone_insecure = &op_gone_insecure;
	otr_ops.still_secure = &op_still_secure;
	otr_ops.log_message = &op_log_message;
	otr_ops.max_message_size = &op_max_message_size;
	otr_ops.account_name = &op_account_name;
	otr_ops.account_name_free = NULL;
	
	root_command_add( "otr", 1, cmd_otr, 0 );
	register_irc_plugin( &otr_plugin );
}

gboolean otr_irc_new(irc_t *irc)
{
	set_t *s;
	GSList *l;
	
	irc->otr = g_new0(otr_t, 1);
	irc->otr->us = otrl_userstate_create();
	
	s = set_add( &irc->b->set, "otr_color_encrypted", "true", set_eval_bool, irc );
	
	s = set_add( &irc->b->set, "otr_policy", "opportunistic", set_eval_list, irc );
	l = g_slist_prepend( NULL, "never" );
	l = g_slist_prepend( l, "opportunistic" );
	l = g_slist_prepend( l, "manual" );
	l = g_slist_prepend( l, "always" );
	s->eval_data = l;

	s = set_add( &irc->b->set, "otr_does_html", "true", set_eval_bool, irc );
	
	return TRUE;
}

void otr_irc_free(irc_t *irc)
{
	otr_t *otr = irc->otr;
	otrl_userstate_free(otr->us);
	if(otr->keygen) {
		kill(otr->keygen, SIGTERM);
		waitpid(otr->keygen, NULL, 0);
		/* TODO: remove stale keygen tempfiles */
	}
	if(otr->to)
		fclose(otr->to);
	if(otr->from)
		fclose(otr->from);
	while(otr->todo) {
		kg_t *p = otr->todo;
		otr->todo = p->next;
		g_free(p);
	}
	g_free(otr);
}

void otr_load(irc_t *irc)
{
	char s[512];
	account_t *a;
	gcry_error_t e;
	gcry_error_t enoent = gcry_error_from_errno(ENOENT);
	int kg=0;

	if(strsane(irc->user->nick)) {
		g_snprintf(s, 511, "%s%s.otr_keys", global.conf->configdir, irc->user->nick);
		e = otrl_privkey_read(irc->otr->us, s);
		if(e && e!=enoent) {
			irc_rootmsg(irc, "otr load: %s: %s", s, gcry_strerror(e));
		}
		g_snprintf(s, 511, "%s%s.otr_fprints", global.conf->configdir, irc->user->nick);
		e = otrl_privkey_read_fingerprints(irc->otr->us, s, NULL, NULL);
		if(e && e!=enoent) {
			irc_rootmsg(irc, "otr load: %s: %s", s, gcry_strerror(e));
		}
	}
	
	/* check for otr keys on all accounts */
	for(a=irc->b->accounts; a; a=a->next) {
		kg = otr_check_for_key(a) || kg;
	}
	if(kg) {
		irc_rootmsg(irc, "Notice: "
			"The accounts above do not have OTR encryption keys associated with them, yet. "
			"These keys are now being generated in the background. "
			"You will be notified as they are completed. "
			"It is not necessary to wait; "
			"BitlBee can be used normally during key generation. "
			"You may safely ignore this message if you don't know what OTR is. ;)");
	}
}

void otr_save(irc_t *irc)
{
	char s[512];
	gcry_error_t e;

	if(strsane(irc->user->nick)) {
		g_snprintf(s, 511, "%s%s.otr_fprints", global.conf->configdir, irc->user->nick);
		e = otrl_privkey_write_fingerprints(irc->otr->us, s);
		if(e) {
			irc_rootmsg(irc, "otr save: %s: %s", s, gcry_strerror(e));
		}
		chmod(s, 0600);
	}
}

void otr_remove(const char *nick)
{
	char s[512];
	
	if(strsane(nick)) {
		g_snprintf(s, 511, "%s%s.otr_keys", global.conf->configdir, nick);
		unlink(s);
		g_snprintf(s, 511, "%s%s.otr_fprints", global.conf->configdir, nick);
		unlink(s);
	}
}

void otr_rename(const char *onick, const char *nnick)
{
	char s[512], t[512];
	
	if(strsane(nnick) && strsane(onick)) {
		g_snprintf(s, 511, "%s%s.otr_keys", global.conf->configdir, onick);
		g_snprintf(t, 511, "%s%s.otr_keys", global.conf->configdir, nnick);
		rename(s,t);
		g_snprintf(s, 511, "%s%s.otr_fprints", global.conf->configdir, onick);
		g_snprintf(t, 511, "%s%s.otr_fprints", global.conf->configdir, nnick);
		rename(s,t);
	}
}

int otr_check_for_key(account_t *a)
{
	irc_t *irc = a->bee->ui_data;
	OtrlPrivKey *k;
	
	/* don't do OTR on certain (not classic IM) protocols, e.g. twitter */
	if(a->prpl->options & OPT_NOOTR) {
		return 0;
	}
	
	k = otrl_privkey_find(irc->otr->us, a->user, a->prpl->name);
	if(k) {
		irc_rootmsg(irc, "otr: %s/%s ready", a->user, a->prpl->name);
		return 0;
	} if(keygen_in_progress(irc, a->user, a->prpl->name)) {
		irc_rootmsg(irc, "otr: keygen for %s/%s already in progress", a->user, a->prpl->name);
		return 0;
	} else {
		irc_rootmsg(irc, "otr: starting background keygen for %s/%s", a->user, a->prpl->name);
		otr_keygen(irc, a->user, a->prpl->name);
		return 1;
	}
}

char *otr_filter_msg_in(irc_user_t *iu, char *msg, int flags)
{
	int ignore_msg;
	char *newmsg = NULL;
	OtrlTLV *tlvs = NULL;
	irc_t *irc = iu->irc;
	struct im_connection *ic = iu->bu->ic;
	
	/* don't do OTR on certain (not classic IM) protocols, e.g. twitter */
	if(ic->acc->prpl->options & OPT_NOOTR) {
		return msg;
	}
	
	ignore_msg = otrl_message_receiving(irc->otr->us, &otr_ops, ic,
		ic->acc->user, ic->acc->prpl->name, iu->bu->handle, msg, &newmsg,
		&tlvs, NULL, NULL);

	otr_handle_smp(ic, iu->bu->handle, tlvs);
	
	if(ignore_msg) {
		/* this was an internal OTR protocol message */
		return NULL;
	} else if(!newmsg) {
		/* this was a non-OTR message */
		return msg;
	} else {
		/* OTR has processed this message */
		ConnContext *context = otrl_context_find(irc->otr->us, iu->bu->handle,
			ic->acc->user, ic->acc->prpl->name, 0, NULL, NULL, NULL);

		/* we're done with the original msg, which will be caller-freed. */
		/* NB: must not change the newmsg pointer, since we free it. */
		msg = newmsg;

		if(context && context->msgstate == OTRL_MSGSTATE_ENCRYPTED) {
			/* HTML decoding */
			/* perform any necessary stripping that the top level would miss */
			if(set_getbool(&ic->bee->set, "otr_does_html") &&
			   !(ic->flags & OPT_DOES_HTML) &&
			   set_getbool(&ic->bee->set, "strip_html")) {
				strip_html(msg);
			}

			/* coloring */
			if(set_getbool(&ic->bee->set, "otr_color_encrypted")) {
				int color;                /* color according to f'print trust */
				char *pre="", *sep="";    /* optional parts */
				const char *trust = context->active_fingerprint->trust;

				if(trust && trust[0] != '\0')
					color=3;   /* green */
				else
					color=5;   /* red */

				/* in a query window, keep "/me " uncolored at the beginning */
				if(g_strncasecmp(msg, "/me ", 4) == 0
				   && irc_user_msgdest(iu) == irc->user->nick) {
					msg += 4;  /* skip */
					pre = "/me ";
				}

				/* comma in first place could mess with the color code */
				if(msg[0] == ',') {
				    /* insert a space between color spec and message */
				    sep = " ";
				}

				msg = g_strdup_printf("%s\x03%.2d%s%s\x0F", pre,
					color, sep, msg);
			}
		}

		if(msg == newmsg) {
			msg = g_strdup(newmsg);
		}
		otrl_message_free(newmsg);
		return msg;
	}
}

char *otr_filter_msg_out(irc_user_t *iu, char *msg, int flags)
{	
	int st;
	char *otrmsg = NULL;
	char *emsg = msg;           /* the message as we hand it to libotr */
	ConnContext *ctx = NULL;
	irc_t *irc = iu->irc;
	struct im_connection *ic = iu->bu->ic;

	/* don't do OTR on certain (not classic IM) protocols, e.g. twitter */
	if(ic->acc->prpl->options & OPT_NOOTR) {
		return msg;
	}

	ctx = otrl_context_find(irc->otr->us,
			iu->bu->handle, ic->acc->user, ic->acc->prpl->name,
			1, NULL, NULL, NULL);

	/* HTML encoding */
	/* consider OTR plaintext to be HTML if otr_does_html is set */
	if(ctx && ctx->msgstate == OTRL_MSGSTATE_ENCRYPTED &&
	   set_getbool(&ic->bee->set, "otr_does_html") &&
	   (g_strncasecmp(msg, "<html>", 6) != 0)) {
		emsg = escape_html(msg);
	}
	
	st = otrl_message_sending(irc->otr->us, &otr_ops, ic,
		ic->acc->user, ic->acc->prpl->name, iu->bu->handle,
		emsg, NULL, &otrmsg, NULL, NULL);
	if(emsg != msg) {
		g_free(emsg);   /* we're done with this one */
	}
	if(st) {
		return NULL;
	}

	if(otrmsg) {
		if(!ctx) {
			otrl_message_free(otrmsg);
			return NULL;
		}
		st = otrl_message_fragment_and_send(&otr_ops, ic, ctx,
			otrmsg, OTRL_FRAGMENT_SEND_ALL, NULL);
		otrl_message_free(otrmsg);
	} else {
		/* note: otrl_message_sending handles policy, so that if REQUIRE_ENCRYPTION is set,
		   this case does not occur */
		return msg;
	}
	
	/* TODO: Error reporting should be done here now (if st!=0), probably. */
	
	return NULL;
}

static const struct irc_plugin otr_plugin =
{
	otr_irc_new,
	otr_irc_free,
	otr_filter_msg_out,
	otr_filter_msg_in,
	otr_load,
	otr_save,
	otr_remove,
};

static void cmd_otr(irc_t *irc, char **args)
{
	const command_t *cmd;
	
	if(!args[0])
		return;
	
	if(!args[1])
		return;
	
	for(cmd=otr_commands; cmd->command; cmd++) {
		if(strcmp(cmd->command, args[1]) == 0)
			break;
	}
	
	if(!cmd->command) {
		irc_rootmsg(irc, "%s: unknown subcommand \"%s\", see \x02help otr\x02",
			args[0], args[1]);
		return;
	}
	
	if(!args[cmd->required_parameters+1]) {
		irc_rootmsg(irc, "%s %s: not enough arguments (%d req.)",
			args[0], args[1], cmd->required_parameters);
		return;
	}
	
	cmd->execute(irc, args+1);
}


/*** OTR "MessageAppOps" callbacks for global.otr_ui: ***/

OtrlPolicy op_policy(void *opdata, ConnContext *context)
{
	struct im_connection *ic = check_imc(opdata, context->accountname, context->protocol);
	irc_t *irc = ic->bee->ui_data;
	const char *p;
	
	/* policy override during keygen: if we're missing the key for context but are currently
	   generating it, then that's as much as we can do. => temporarily return NEVER. */
	if(keygen_in_progress(irc, context->accountname, context->protocol) &&
	   !otrl_privkey_find(irc->otr->us, context->accountname, context->protocol))
		return OTRL_POLICY_NEVER;

	p = set_getstr(&ic->bee->set, "otr_policy");
	if(!strcmp(p, "never"))
		return OTRL_POLICY_NEVER;
	if(!strcmp(p, "opportunistic"))
		return OTRL_POLICY_OPPORTUNISTIC;
	if(!strcmp(p, "manual"))
		return OTRL_POLICY_MANUAL;
	if(!strcmp(p, "always"))
		return OTRL_POLICY_ALWAYS;
	
	return OTRL_POLICY_OPPORTUNISTIC;
}

void op_create_privkey(void *opdata, const char *accountname,
	const char *protocol)
{
	struct im_connection *ic = check_imc(opdata, accountname, protocol);
	irc_t *irc = ic->bee->ui_data;
	
	/* will fail silently if keygen already in progress */
	otr_keygen(irc, accountname, protocol);
}

int op_is_logged_in(void *opdata, const char *accountname,
	const char *protocol, const char *recipient)
{
	struct im_connection *ic = check_imc(opdata, accountname, protocol);
	bee_user_t *bu;

	/* lookup the irc_user_t for the given recipient */
	bu = bee_user_by_handle(ic->bee, ic, recipient);
	if(bu) {
		if(bu->flags & BEE_USER_ONLINE)
			return 1;
		else
			return 0;
	} else {
		return -1;
	}
}

void op_inject_message(void *opdata, const char *accountname,
	const char *protocol, const char *recipient, const char *message)
{
	struct im_connection *ic = check_imc(opdata, accountname, protocol);
	irc_t *irc = ic->bee->ui_data;

	if (strcmp(accountname, recipient) == 0) {
		/* huh? injecting messages to myself? */
		irc_rootmsg(irc, "note to self: %s", message);
	} else {
		/* need to drop some consts here :-( */
		/* TODO: get flags into op_inject_message?! */
		ic->acc->prpl->buddy_msg(ic, (char *)recipient, (char *)message, 0);
		/* ignoring return value :-/ */
	}
}

int op_display_otr_message(void *opdata, const char *accountname,
	const char *protocol, const char *username, const char *message)
{
	struct im_connection *ic = check_imc(opdata, accountname, protocol);
	char *msg = g_strdup(message);
	irc_t *irc = ic->bee->ui_data;
	irc_user_t *u = peeruser(irc, username, protocol);

	strip_html(msg);
	if(u) {
		/* display as a notice from this particular user */
		irc_usernotice(u, "%s", msg);
	} else {
		irc_rootmsg(irc, "[otr] %s", msg);
	}

	g_free(msg);
	return 0;
}

void op_new_fingerprint(void *opdata, OtrlUserState us,
	const char *accountname, const char *protocol,
	const char *username, unsigned char fingerprint[20])
{
	struct im_connection *ic = check_imc(opdata, accountname, protocol);
	irc_t *irc = ic->bee->ui_data;
	irc_user_t *u = peeruser(irc, username, protocol);
	char hunam[45];		/* anybody looking? ;-) */
	
	otrl_privkey_hash_to_human(hunam, fingerprint);
	if(u) {
		irc_usernotice(u, "new fingerprint: %s", hunam);
	} else {
		/* this case shouldn't normally happen */
		irc_rootmsg(irc, "new fingerprint for %s/%s: %s",
			username, protocol, hunam);
	}
}

void op_write_fingerprints(void *opdata)
{
	struct im_connection *ic = (struct im_connection *)opdata;
	irc_t *irc = ic->bee->ui_data;

	otr_save(irc);
}

void op_gone_secure(void *opdata, ConnContext *context)
{
	struct im_connection *ic =
		check_imc(opdata, context->accountname, context->protocol);
	irc_user_t *u;
	irc_t *irc = ic->bee->ui_data;

	u = peeruser(irc, context->username, context->protocol);
	if(!u) {
		log_message(LOGLVL_ERROR,
			"BUG: otr.c: op_gone_secure: irc_user_t for %s/%s/%s not found!",
			context->username, context->protocol, context->accountname);
		return;
	}
	
	otr_update_uflags(context, u);
	if(!otr_update_modeflags(irc, u)) {
		char *trust = u->flags & IRC_USER_OTR_TRUSTED ? "trusted" : "untrusted!";
		irc_usernotice(u, "conversation is now off the record (%s)", trust);
	}
}

void op_gone_insecure(void *opdata, ConnContext *context)
{
	struct im_connection *ic =
		check_imc(opdata, context->accountname, context->protocol);
	irc_t *irc = ic->bee->ui_data;
	irc_user_t *u;

	u = peeruser(irc, context->username, context->protocol);
	if(!u) {
		log_message(LOGLVL_ERROR,
			"BUG: otr.c: op_gone_insecure: irc_user_t for %s/%s/%s not found!",
			context->username, context->protocol, context->accountname);
		return;
	}
	otr_update_uflags(context, u);
	if(!otr_update_modeflags(irc, u))
		irc_usernotice(u, "conversation is now in cleartext");
}

void op_still_secure(void *opdata, ConnContext *context, int is_reply)
{
	struct im_connection *ic =
		check_imc(opdata, context->accountname, context->protocol);
	irc_t *irc = ic->bee->ui_data;
	irc_user_t *u;

	u = peeruser(irc, context->username, context->protocol);
	if(!u) {
		log_message(LOGLVL_ERROR,
			"BUG: otr.c: op_still_secure: irc_user_t for %s/%s/%s not found!",
			context->username, context->protocol, context->accountname);
		return;
	}

	otr_update_uflags(context, u);
	if(!otr_update_modeflags(irc, u)) {
		char *trust = u->flags & IRC_USER_OTR_TRUSTED ? "trusted" : "untrusted!";
		irc_usernotice(u, "otr connection has been refreshed (%s)", trust);
	}
}

void op_log_message(void *opdata, const char *message)
{
	char *msg = g_strdup(message);
	
	strip_html(msg);
	log_message(LOGLVL_INFO, "otr: %s", msg);
	g_free(msg);
}

int op_max_message_size(void *opdata, ConnContext *context)
{
	struct im_connection *ic =
		check_imc(opdata, context->accountname, context->protocol);

	return ic->acc->prpl->mms;
}

const char *op_account_name(void *opdata, const char *account, const char *protocol)
{
	struct im_connection *ic = (struct im_connection *)opdata;
	irc_t *irc = ic->bee->ui_data;

	return peernick(irc, account, protocol);
}


/*** OTR sub-command handlers ***/

void cmd_otr_reconnect(irc_t *irc, char **args)
{
	cmd_otr_disconnect(irc, args);
	cmd_otr_connect(irc, args);
}

void cmd_otr_disconnect(irc_t *irc, char **args)
{
	irc_user_t *u;

	u = irc_user_by_name(irc, args[1]);
	if(!u || !u->bu || !u->bu->ic) {
		irc_rootmsg(irc, "%s: unknown user", args[1]);
		return;
	}
	
	otrl_message_disconnect(irc->otr->us, &otr_ops,
		u->bu->ic, u->bu->ic->acc->user, u->bu->ic->acc->prpl->name, u->bu->handle);
	
	/* for some reason, libotr (3.1.0) doesn't do this itself: */
	if(u->flags & IRC_USER_OTR_ENCRYPTED) {
		ConnContext *ctx;
		ctx = otrl_context_find(irc->otr->us, u->bu->handle, u->bu->ic->acc->user,
			u->bu->ic->acc->prpl->name, 0, NULL, NULL, NULL);
		if(ctx)
			op_gone_insecure(u->bu->ic, ctx);
		else /* huh? */
			u->flags &= ( IRC_USER_OTR_ENCRYPTED | IRC_USER_OTR_TRUSTED );
	}
}

void cmd_otr_connect(irc_t *irc, char **args)
{
	irc_user_t *u;

	u = irc_user_by_name(irc, args[1]);
	if(!u || !u->bu || !u->bu->ic) {
		irc_rootmsg(irc, "%s: unknown user", args[1]);
		return;
	}
	if(!(u->bu->flags & BEE_USER_ONLINE)) {
		irc_rootmsg(irc, "%s is offline", args[1]);
		return;
	}
	
	bee_user_msg(irc->b, u->bu, "?OTR?v2?", 0);
}

void cmd_otr_smp(irc_t *irc, char **args)
{
	otr_smp_or_smpq(irc, args[1], NULL, args[2]);	/* no question */
}

void cmd_otr_smpq(irc_t *irc, char **args)
{
	otr_smp_or_smpq(irc, args[1], args[2], args[3]);
}

void cmd_otr_trust(irc_t *irc, char **args)
{
	irc_user_t *u;
	ConnContext *ctx;
	unsigned char raw[20];
	Fingerprint *fp;
	int i,j;
	
	u = irc_user_by_name(irc, args[1]);
	if(!u || !u->bu || !u->bu->ic) {
		irc_rootmsg(irc, "%s: unknown user", args[1]);
		return;
	}
	
	ctx = otrl_context_find(irc->otr->us, u->bu->handle,
		u->bu->ic->acc->user, u->bu->ic->acc->prpl->name, 0, NULL, NULL, NULL);
	if(!ctx) {
		irc_rootmsg(irc, "%s: no otr context with user", args[1]);
		return;
	}
	
	/* convert given fingerprint to raw representation */
	for(i=0; i<5; i++) {
		for(j=0; j<4; j++) {
			char *p = args[2+i]+(2*j);
			char *q = p+1;
			int x, y;
			
			if(!*p || !*q) {
				irc_rootmsg(irc, "failed: truncated fingerprint block %d", i+1);
				return;
			}
			
			x = hexval(*p);
			y = hexval(*q);
			if(x<0) {
				irc_rootmsg(irc, "failed: %d. hex digit of block %d out of range", 2*j+1, i+1);
				return;
			}
			if(y<0) {
				irc_rootmsg(irc, "failed: %d. hex digit of block %d out of range", 2*j+2, i+1);
				return;
			}

			raw[i*4+j] = x*16 + y;
		}
	}
	fp = otrl_context_find_fingerprint(ctx, raw, 0, NULL);
	if(!fp) {
		irc_rootmsg(irc, "failed: no such fingerprint for %s", args[1]);
	} else {
		char *trust = args[7] ? args[7] : "affirmed";
		otrl_context_set_trust(fp, trust);
		irc_rootmsg(irc, "fingerprint match, trust set to \"%s\"", trust);
		if(u->flags & IRC_USER_OTR_ENCRYPTED)
			u->flags |= IRC_USER_OTR_TRUSTED;
		otr_update_modeflags(irc, u);
	}
}

void cmd_otr_info(irc_t *irc, char **args)
{
	if(!args[1]) {
		show_general_otr_info(irc);
	} else {
		char *arg = g_strdup(args[1]);
		char *myhandle, *handle=NULL, *protocol;
		ConnContext *ctx;
		
		/* interpret arg as 'user/protocol/account' if possible */
		protocol = strchr(arg, '/');
		myhandle = NULL;
		if(protocol) {
			*(protocol++) = '\0';
			myhandle = strchr(protocol, '/');
		}
		if(protocol && myhandle) {
			*(myhandle++) = '\0';
			handle = arg;
			ctx = otrl_context_find(irc->otr->us, handle, myhandle, protocol, 0, NULL, NULL, NULL);
			if(!ctx) {
				irc_rootmsg(irc, "no such context");
				g_free(arg);
				return;
			}
		} else {
			irc_user_t *u = irc_user_by_name(irc, args[1]);
			if(!u || !u->bu || !u->bu->ic) {
				irc_rootmsg(irc, "%s: unknown user", args[1]);
				g_free(arg);
				return;
			}
			ctx = otrl_context_find(irc->otr->us, u->bu->handle, u->bu->ic->acc->user,
				u->bu->ic->acc->prpl->name, 0, NULL, NULL, NULL);
			if(!ctx) {
				irc_rootmsg(irc, "no otr context with %s", args[1]);
				g_free(arg);
				return;
			}
		}
	
		/* show how we resolved the (nick) argument, if we did */
		if(handle!=arg) {
			irc_rootmsg(irc, "%s is %s/%s; we are %s/%s to them", args[1],
				ctx->username, ctx->protocol, ctx->accountname, ctx->protocol);
		}
		show_otr_context_info(irc, ctx);
		g_free(arg);
	}
}

void cmd_otr_keygen(irc_t *irc, char **args)
{
	int i, n;
	account_t *a;
	
	n = atoi(args[1]);
	if(n<0 || (!n && strcmp(args[1], "0"))) {
		irc_rootmsg(irc, "%s: invalid account number", args[1]);
		return;
	}
	
	a = irc->b->accounts;
	for(i=0; i<n && a; i++, a=a->next);
	if(!a) {
		irc_rootmsg(irc, "%s: no such account", args[1]);
		return;
	}
	
	if(keygen_in_progress(irc, a->user, a->prpl->name)) {
		irc_rootmsg(irc, "keygen for account %d already in progress", n);
		return;
	}
	
	if(otrl_privkey_find(irc->otr->us, a->user, a->prpl->name)) {
		char *s = g_strdup_printf("account %d already has a key, replace it?", n);
		query_add(irc, NULL, s, yes_keygen, NULL, NULL, a);
		g_free(s);
	} else {
		otr_keygen(irc, a->user, a->prpl->name);
	}
}

void yes_forget_fingerprint(void *data)
{
	pair_t *p = (pair_t *)data;
	irc_t *irc = (irc_t *)p->fst;
	Fingerprint *fp = (Fingerprint *)p->snd;

	g_free(p);
	
	if(fp == fp->context->active_fingerprint) {
		irc_rootmsg(irc, "that fingerprint is active, terminate otr connection first");
		return;
	}
		
	otrl_context_forget_fingerprint(fp, 0);
}

void yes_forget_context(void *data)
{
	pair_t *p = (pair_t *)data;
	irc_t *irc = (irc_t *)p->fst;
	ConnContext *ctx = (ConnContext *)p->snd;

	g_free(p);
	
	if(ctx->msgstate == OTRL_MSGSTATE_ENCRYPTED) {
		irc_rootmsg(irc, "active otr connection with %s, terminate it first",
			peernick(irc, ctx->username, ctx->protocol));
		return;
	}
		
	if(ctx->msgstate == OTRL_MSGSTATE_FINISHED)
		otrl_context_force_plaintext(ctx);
	otrl_context_forget(ctx);
}

void yes_forget_key(void *data)
{
	OtrlPrivKey *key = (OtrlPrivKey *)data;
	
	otrl_privkey_forget(key);
	/* Hm, libotr doesn't seem to offer a function for explicitly /writing/
	   keyfiles. So the key will be back on the next load... */
	/* TODO: Actually erase forgotten keys from storage? */
}

void cmd_otr_forget(irc_t *irc, char **args)
{
	if(!strcmp(args[1], "fingerprint"))
	{
		irc_user_t *u;
		ConnContext *ctx;
		Fingerprint *fp;
		char human[54];
		char *s;
		pair_t *p;
		
		if(!args[3]) {
			irc_rootmsg(irc, "otr %s %s: not enough arguments (2 req.)", args[0], args[1]);
			return;
		}
		
		/* TODO: allow context specs ("user/proto/account") in 'otr forget fingerprint'? */
		u = irc_user_by_name(irc, args[2]);
		if(!u || !u->bu || !u->bu->ic) {
			irc_rootmsg(irc, "%s: unknown user", args[2]);
			return;
		}
		
		ctx = otrl_context_find(irc->otr->us, u->bu->handle, u->bu->ic->acc->user,
			u->bu->ic->acc->prpl->name, 0, NULL, NULL, NULL);
		if(!ctx) {
			irc_rootmsg(irc, "no otr context with %s", args[2]);
			return;
		}
		
		fp = match_fingerprint(irc, ctx, ((const char **)args)+3);
		if(!fp) {
			/* match_fingerprint does error messages */
			return;
		}
		
		if(fp == ctx->active_fingerprint) {
			irc_rootmsg(irc, "that fingerprint is active, terminate otr connection first");
			return;
		}
		
		otrl_privkey_hash_to_human(human, fp->fingerprint);
		s = g_strdup_printf("about to forget fingerprint %s, are you sure?", human);
		p = g_malloc(sizeof(pair_t));
		if(!p)
			return;
		p->fst = irc;
		p->snd = fp;
		query_add(irc, NULL, s, yes_forget_fingerprint, NULL, NULL, p);
		g_free(s);
	}
	
	else if(!strcmp(args[1], "context"))
	{
		irc_user_t *u;
		ConnContext *ctx;
		char *s;
		pair_t *p;
		
		/* TODO: allow context specs ("user/proto/account") in 'otr forget contex'? */
		u = irc_user_by_name(irc, args[2]);
		if(!u || !u->bu || !u->bu->ic) {
			irc_rootmsg(irc, "%s: unknown user", args[2]);
			return;
		}
		
		ctx = otrl_context_find(irc->otr->us, u->bu->handle, u->bu->ic->acc->user,
			u->bu->ic->acc->prpl->name, 0, NULL, NULL, NULL);
		if(!ctx) {
			irc_rootmsg(irc, "no otr context with %s", args[2]);
			return;
		}
		
		if(ctx->msgstate == OTRL_MSGSTATE_ENCRYPTED) {
			irc_rootmsg(irc, "active otr connection with %s, terminate it first", args[2]);
			return;
		}
		
		s = g_strdup_printf("about to forget otr data about %s, are you sure?", args[2]);
		p = g_malloc(sizeof(pair_t));
		if(!p)
			return;
		p->fst = irc;
		p->snd = ctx;
		query_add(irc, NULL, s, yes_forget_context, NULL, NULL, p);
		g_free(s);
	}
	
	else if(!strcmp(args[1], "key"))
	{
		OtrlPrivKey *key;
		char *s;
		
		key = match_privkey(irc, ((const char **)args)+2);
		if(!key) {
			/* match_privkey does error messages */
			return;
		}
		
		s = g_strdup_printf("about to forget the private key for %s/%s, are you sure?",
			key->accountname, key->protocol);
		query_add(irc, NULL, s, yes_forget_key, NULL, NULL, key);
		g_free(s);
	}
	
	else
	{
		irc_rootmsg(irc, "otr %s: unknown subcommand \"%s\", see \x02help otr forget\x02",
			args[0], args[1]);
	}
}


/*** local helpers / subroutines: ***/

/* Socialist Millionaires' Protocol */
void otr_handle_smp(struct im_connection *ic, const char *handle, OtrlTLV *tlvs)
{
	irc_t *irc = ic->bee->ui_data;
	OtrlUserState us = irc->otr->us;
	OtrlMessageAppOps *ops = &otr_ops;
	OtrlTLV *tlv = NULL;
	ConnContext *context;
	NextExpectedSMP nextMsg;
	irc_user_t *u;
	bee_user_t *bu;

	bu = bee_user_by_handle(ic->bee, ic, handle);
	if(!bu || !(u = bu->ui_data)) return;
	context = otrl_context_find(us, handle,
		ic->acc->user, ic->acc->prpl->name, 1, NULL, NULL, NULL);
	if(!context) {
		/* huh? out of memory or what? */
		irc_rootmsg(irc, "smp: failed to get otr context for %s", u->nick);
		otrl_message_abort_smp(us, ops, u->bu->ic, context);
		otrl_sm_state_free(context->smstate);
		return;
	}
	nextMsg = context->smstate->nextExpected;

	if (context->smstate->sm_prog_state == OTRL_SMP_PROG_CHEATED) {
		irc_rootmsg(irc, "smp %s: opponent violated protocol, aborting",
			u->nick);
		otrl_message_abort_smp(us, ops, u->bu->ic, context);
		otrl_sm_state_free(context->smstate);
		return;
	}

	tlv = otrl_tlv_find(tlvs, OTRL_TLV_SMP1Q);
	if (tlv) {
		if (nextMsg != OTRL_SMP_EXPECT1) {
			irc_rootmsg(irc, "smp %s: spurious SMP1Q received, aborting", u->nick);
			otrl_message_abort_smp(us, ops, u->bu->ic, context);
			otrl_sm_state_free(context->smstate);
		} else {
			char *question = g_strndup((char *)tlv->data, tlv->len);
			irc_rootmsg(irc, "smp: initiated by %s with question: \x02\"%s\"\x02", u->nick,
				question);
			irc_rootmsg(irc, "smp: respond with \x02otr smp %s <answer>\x02",
				u->nick);
			g_free(question);
			/* smp stays in EXPECT1 until user responds */
		}
	}
	tlv = otrl_tlv_find(tlvs, OTRL_TLV_SMP1);
	if (tlv) {
		if (nextMsg != OTRL_SMP_EXPECT1) {
			irc_rootmsg(irc, "smp %s: spurious SMP1 received, aborting", u->nick);
			otrl_message_abort_smp(us, ops, u->bu->ic, context);
			otrl_sm_state_free(context->smstate);
		} else {
			irc_rootmsg(irc, "smp: initiated by %s"
				" - respond with \x02otr smp %s <secret>\x02",
				u->nick, u->nick);
			/* smp stays in EXPECT1 until user responds */
		}
	}
	tlv = otrl_tlv_find(tlvs, OTRL_TLV_SMP2);
	if (tlv) {
		if (nextMsg != OTRL_SMP_EXPECT2) {
			irc_rootmsg(irc, "smp %s: spurious SMP2 received, aborting", u->nick);
			otrl_message_abort_smp(us, ops, u->bu->ic, context);
			otrl_sm_state_free(context->smstate);
		} else {
			/* SMP2 received, otrl_message_receiving will have sent SMP3 */
			context->smstate->nextExpected = OTRL_SMP_EXPECT4;
		}
	}
	tlv = otrl_tlv_find(tlvs, OTRL_TLV_SMP3);
	if (tlv) {
		if (nextMsg != OTRL_SMP_EXPECT3) {
			irc_rootmsg(irc, "smp %s: spurious SMP3 received, aborting", u->nick);
			otrl_message_abort_smp(us, ops, u->bu->ic, context);
			otrl_sm_state_free(context->smstate);
		} else {
			/* SMP3 received, otrl_message_receiving will have sent SMP4 */
			if(context->smstate->sm_prog_state == OTRL_SMP_PROG_SUCCEEDED) {
				if(context->smstate->received_question) {
					irc_rootmsg(irc, "smp %s: correct answer, you are trusted",
						u->nick);
				} else {
					irc_rootmsg(irc, "smp %s: secrets proved equal, fingerprint trusted",
						u->nick);
				}
			} else {
				if(context->smstate->received_question) {
					irc_rootmsg(irc, "smp %s: wrong answer, you are not trusted",
						u->nick);
				} else {
					irc_rootmsg(irc, "smp %s: secrets did not match, fingerprint not trusted",
						u->nick);
				}
			}
			otrl_sm_state_free(context->smstate);
			/* smp is in back in EXPECT1 */
		}
	}
	tlv = otrl_tlv_find(tlvs, OTRL_TLV_SMP4);
	if (tlv) {
		if (nextMsg != OTRL_SMP_EXPECT4) {
			irc_rootmsg(irc, "smp %s: spurious SMP4 received, aborting", u->nick);
			otrl_message_abort_smp(us, ops, u->bu->ic, context);
			otrl_sm_state_free(context->smstate);
		} else {
			/* SMP4 received, otrl_message_receiving will have set fp trust */
			if(context->smstate->sm_prog_state == OTRL_SMP_PROG_SUCCEEDED) {
				irc_rootmsg(irc, "smp %s: secrets proved equal, fingerprint trusted",
					u->nick);
			} else {
				irc_rootmsg(irc, "smp %s: secrets did not match, fingerprint not trusted",
					u->nick);
			}
			otrl_sm_state_free(context->smstate);
			/* smp is in back in EXPECT1 */
		}
	}
	tlv = otrl_tlv_find(tlvs, OTRL_TLV_SMP_ABORT);
	if (tlv) {
	 	irc_rootmsg(irc, "smp: received abort from %s", u->nick);
		otrl_sm_state_free(context->smstate);
		/* smp is in back in EXPECT1 */
	}
}

/* combined handler for the 'otr smp' and 'otr smpq' commands */
void otr_smp_or_smpq(irc_t *irc, const char *nick, const char *question,
		const char *secret)
{
	irc_user_t *u;
	ConnContext *ctx;

	u = irc_user_by_name(irc, nick);
	if(!u || !u->bu || !u->bu->ic) {
		irc_rootmsg(irc, "%s: unknown user", nick);
		return;
	}
	if(!(u->bu->flags & BEE_USER_ONLINE)) {
		irc_rootmsg(irc, "%s is offline", nick);
		return;
	}
	
	ctx = otrl_context_find(irc->otr->us, u->bu->handle,
		u->bu->ic->acc->user, u->bu->ic->acc->prpl->name, 0, NULL, NULL, NULL);
	if(!ctx || ctx->msgstate != OTRL_MSGSTATE_ENCRYPTED) {
		irc_rootmsg(irc, "smp: otr inactive with %s, try \x02otr connect"
				" %s\x02", nick, nick);
		return;
	}

	if(ctx->smstate->nextExpected != OTRL_SMP_EXPECT1) {
		log_message(LOGLVL_INFO,
			"SMP already in phase %d, sending abort before reinitiating",
			ctx->smstate->nextExpected+1);
		otrl_message_abort_smp(irc->otr->us, &otr_ops, u->bu->ic, ctx);
		otrl_sm_state_free(ctx->smstate);
	}

	if(question) {
		/* this was 'otr smpq', just initiate */
		irc_rootmsg(irc, "smp: initiating with %s...", u->nick);
		otrl_message_initiate_smp_q(irc->otr->us, &otr_ops, u->bu->ic, ctx,
			question, (unsigned char *)secret, strlen(secret));
		/* smp is now in EXPECT2 */
	} else {
		/* this was 'otr smp', initiate or reply */
		/* warning: the following assumes that smstates are cleared whenever an SMP
		   is completed or aborted! */ 
		if(ctx->smstate->secret == NULL) {
			irc_rootmsg(irc, "smp: initiating with %s...", u->nick);
			otrl_message_initiate_smp(irc->otr->us, &otr_ops,
				u->bu->ic, ctx, (unsigned char *)secret, strlen(secret));
			/* smp is now in EXPECT2 */
		} else {
			/* if we're still in EXPECT1 but smstate is initialized, we must have
			   received the SMP1, so let's issue a response */
			irc_rootmsg(irc, "smp: responding to %s...", u->nick);
			otrl_message_respond_smp(irc->otr->us, &otr_ops,
				u->bu->ic, ctx, (unsigned char *)secret, strlen(secret));
			/* smp is now in EXPECT3 */
		}
	}
}

/* helper to assert that account and protocol names given to ops below always
   match the im_connection passed through as opdata */
struct im_connection *check_imc(void *opdata, const char *accountname,
	const char *protocol)
{
	struct im_connection *ic = (struct im_connection *)opdata;

	if (strcmp(accountname, ic->acc->user) != 0) {
		log_message(LOGLVL_WARNING,
			"otr: internal account name mismatch: '%s' vs '%s'",
			accountname, ic->acc->user);
	}
	if (strcmp(protocol, ic->acc->prpl->name) != 0) {
		log_message(LOGLVL_WARNING,
			"otr: internal protocol name mismatch: '%s' vs '%s'",
			protocol, ic->acc->prpl->name);
	}
	
	return ic;
}

irc_user_t *peeruser(irc_t *irc, const char *handle, const char *protocol)
{
	GSList *l;
	
	for(l=irc->b->users; l; l = l->next) {
		bee_user_t *bu = l->data;
		struct prpl *prpl;
		if(!bu->ui_data || !bu->ic || !bu->handle)
			continue;
		prpl = bu->ic->acc->prpl;
		if(strcmp(prpl->name, protocol) == 0
			&& prpl->handle_cmp(bu->handle, handle) == 0) {
			return bu->ui_data;
		}
	}
	
	return NULL;
}

int hexval(char a)
{
	int x=tolower(a);
	
	if(x>='a' && x<='f')
		x = x - 'a' + 10;
	else if(x>='0' && x<='9')
		x = x - '0';
	else
		return -1;
	
	return x;
}

const char *peernick(irc_t *irc, const char *handle, const char *protocol)
{
	static char fallback[512];
	
	irc_user_t *u = peeruser(irc, handle, protocol);
	if(u) {
		return u->nick;
	} else {
		g_snprintf(fallback, 511, "%s/%s", handle, protocol);
		return fallback;
	}
}

void otr_update_uflags(ConnContext *context, irc_user_t *u)
{
	const char *trust;

	if(context->active_fingerprint) {
		u->flags |= IRC_USER_OTR_ENCRYPTED;

		trust = context->active_fingerprint->trust;
		if(trust && trust[0])
			u->flags |= IRC_USER_OTR_TRUSTED;
		else
			u->flags &= ~IRC_USER_OTR_TRUSTED;
	} else {
		u->flags &= ~IRC_USER_OTR_ENCRYPTED;
	}
}

int otr_update_modeflags(irc_t *irc, irc_user_t *u)
{
	return 0;
}

void show_fingerprints(irc_t *irc, ConnContext *ctx)
{
	char human[45];
	Fingerprint *fp;
	const char *trust;
	int count=0;
	
	for(fp=&ctx->fingerprint_root; fp; fp=fp->next) {
		if(!fp->fingerprint)
			continue;
		count++;
		otrl_privkey_hash_to_human(human, fp->fingerprint);
		if(!fp->trust || fp->trust[0] == '\0') {
			trust="untrusted";
		} else {
			trust=fp->trust;
		}
		if(fp == ctx->active_fingerprint) {
			irc_rootmsg(irc, "    \x02%s (%s)\x02", human, trust);
		} else {
			irc_rootmsg(irc, "    %s (%s)", human, trust);
		}
	}
	if(count==0)
		irc_rootmsg(irc, "    (none)");
}

Fingerprint *match_fingerprint(irc_t *irc, ConnContext *ctx, const char **args)
{
	Fingerprint *fp, *fp2;
	char human[45];
	char prefix[45], *p;
	int n;
	int i,j;
	
	/* assemble the args into a prefix in standard "human" form */
	n=0;
	p=prefix;
	for(i=0; args[i]; i++) {
		for(j=0; args[i][j]; j++) {
			char c = toupper(args[i][j]);
			
			if(n>=40) {
				irc_rootmsg(irc, "too many fingerprint digits given, expected at most 40");
				return NULL;
			}
			
			if( (c>='A' && c<='F') || (c>='0' && c<='9') ) {
				*(p++) = c;
			} else {
				irc_rootmsg(irc, "invalid hex digit '%c' in block %d", args[i][j], i+1);
				return NULL;
			}
			
			n++;
			if(n%8 == 0)
				*(p++) = ' ';
		}
	}
	*p = '\0';
	
	/* find first fingerprint with the given prefix */
	n = strlen(prefix);
	for(fp=&ctx->fingerprint_root; fp; fp=fp->next) {
		if(!fp->fingerprint)
			continue;
		otrl_privkey_hash_to_human(human, fp->fingerprint);
		if(!strncmp(prefix, human, n))
			break;
	}
	if(!fp) {
		irc_rootmsg(irc, "%s: no match", prefix);
		return NULL;
	}
	
	/* make sure the match, if any, is unique */
	for(fp2=fp->next; fp2; fp2=fp2->next) {
		if(!fp2->fingerprint)
			continue;
		otrl_privkey_hash_to_human(human, fp2->fingerprint);
		if(!strncmp(prefix, human, n))
			break;
	}
	if(fp2) {
		irc_rootmsg(irc, "%s: multiple matches", prefix);
		return NULL;
	}
	
	return fp;
}

OtrlPrivKey *match_privkey(irc_t *irc, const char **args)
{
	OtrlPrivKey *k, *k2;
	char human[45];
	char prefix[45], *p;
	int n;
	int i,j;
	
	/* assemble the args into a prefix in standard "human" form */
	n=0;
	p=prefix;
	for(i=0; args[i]; i++) {
		for(j=0; args[i][j]; j++) {
			char c = toupper(args[i][j]);
			
			if(n>=40) {
				irc_rootmsg(irc, "too many fingerprint digits given, expected at most 40");
				return NULL;
			}
			
			if( (c>='A' && c<='F') || (c>='0' && c<='9') ) {
				*(p++) = c;
			} else {
				irc_rootmsg(irc, "invalid hex digit '%c' in block %d", args[i][j], i+1);
				return NULL;
			}
			
			n++;
			if(n%8 == 0)
				*(p++) = ' ';
		}
	}
	*p = '\0';
	
	/* find first key which matches the given prefix */
	n = strlen(prefix);
	for(k=irc->otr->us->privkey_root; k; k=k->next) {
		p = otrl_privkey_fingerprint(irc->otr->us, human, k->accountname, k->protocol);
		if(!p) /* gah! :-P */
			continue;
		if(!strncmp(prefix, human, n))
			break;
	}
	if(!k) {
		irc_rootmsg(irc, "%s: no match", prefix);
		return NULL;
	}
	
	/* make sure the match, if any, is unique */
	for(k2=k->next; k2; k2=k2->next) {
		p = otrl_privkey_fingerprint(irc->otr->us, human, k2->accountname, k2->protocol);
		if(!p) /* gah! :-P */
			continue;
		if(!strncmp(prefix, human, n))
			break;
	}
	if(k2) {
		irc_rootmsg(irc, "%s: multiple matches", prefix);
		return NULL;
	}
	
	return k;
}

void show_general_otr_info(irc_t *irc)
{
	ConnContext *ctx;
	OtrlPrivKey *key;
	char human[45];
	kg_t *kg;

	/* list all privkeys (including ones being generated) */
	irc_rootmsg(irc, "\x1fprivate keys:\x1f");
	for(key=irc->otr->us->privkey_root; key; key=key->next) {
		const char *hash;
		
		switch(key->pubkey_type) {
		case OTRL_PUBKEY_TYPE_DSA:
			irc_rootmsg(irc, "  %s/%s - DSA", key->accountname, key->protocol);
			break;
		default:
			irc_rootmsg(irc, "  %s/%s - type %d", key->accountname, key->protocol,
				key->pubkey_type);
		}

		/* No, it doesn't make much sense to search for the privkey again by
		   account/protocol, but libotr currently doesn't provide a direct routine
		   for hashing a given 'OtrlPrivKey'... */
		hash = otrl_privkey_fingerprint(irc->otr->us, human, key->accountname, key->protocol);
		if(hash) /* should always succeed */
			irc_rootmsg(irc, "    %s", human);
	}
	if(irc->otr->sent_accountname) {
		irc_rootmsg(irc, "  %s/%s - DSA", irc->otr->sent_accountname,
			irc->otr->sent_protocol);
		irc_rootmsg(irc, "    (being generated)");
	}
	for(kg=irc->otr->todo; kg; kg=kg->next) {
		irc_rootmsg(irc, "  %s/%s - DSA", kg->accountname, kg->protocol);
		irc_rootmsg(irc, "    (queued)");
	}
	if(key == irc->otr->us->privkey_root &&
	   !irc->otr->sent_accountname &&
	   kg == irc->otr->todo)
		irc_rootmsg(irc, "  (none)");

	/* list all contexts */
	irc_rootmsg(irc, "%s", "");
	irc_rootmsg(irc, "\x1f" "connection contexts:\x1f (bold=currently encrypted)");
	for(ctx=irc->otr->us->context_root; ctx; ctx=ctx->next) {\
		irc_user_t *u;
		char *userstring;
		
		u = peeruser(irc, ctx->username, ctx->protocol);
		if(u)
			userstring = g_strdup_printf("%s/%s/%s (%s)",
				ctx->username, ctx->protocol, ctx->accountname, u->nick);
		else
			userstring = g_strdup_printf("%s/%s/%s",
				ctx->username, ctx->protocol, ctx->accountname);
		
		if(ctx->msgstate == OTRL_MSGSTATE_ENCRYPTED) {
			irc_rootmsg(irc, "  \x02%s\x02", userstring);
		} else {
			irc_rootmsg(irc, "  %s", userstring);
		}
		
		g_free(userstring);
	}
	if(ctx == irc->otr->us->context_root)
		irc_rootmsg(irc, "  (none)");
}

void show_otr_context_info(irc_t *irc, ConnContext *ctx)
{
	switch(ctx->otr_offer) {
	case OFFER_NOT:
		irc_rootmsg(irc, "  otr offer status: none sent");
		break;
	case OFFER_SENT:
		irc_rootmsg(irc, "  otr offer status: awaiting reply");
		break;
	case OFFER_ACCEPTED:
		irc_rootmsg(irc, "  otr offer status: accepted our offer");
		break;
	case OFFER_REJECTED:
		irc_rootmsg(irc, "  otr offer status: ignored our offer");
		break;
	default:
		irc_rootmsg(irc, "  otr offer status: %d", ctx->otr_offer);
	}

	switch(ctx->msgstate) {
	case OTRL_MSGSTATE_PLAINTEXT:
		irc_rootmsg(irc, "  connection state: cleartext");
		break;
	case OTRL_MSGSTATE_ENCRYPTED:
		irc_rootmsg(irc, "  connection state: encrypted (v%d)", ctx->protocol_version);
		break;
	case OTRL_MSGSTATE_FINISHED:
		irc_rootmsg(irc, "  connection state: shut down");
		break;
	default:
		irc_rootmsg(irc, "  connection state: %d", ctx->msgstate);
	}

	irc_rootmsg(irc, "  fingerprints: (bold=active)");	
	show_fingerprints(irc, ctx);
}

int keygen_in_progress(irc_t *irc, const char *handle, const char *protocol)
{
	kg_t *kg;
	
	if(!irc->otr->sent_accountname || !irc->otr->sent_protocol)
		return 0;

	/* are we currently working on this key? */
	if(!strcmp(handle, irc->otr->sent_accountname) &&
	   !strcmp(protocol, irc->otr->sent_protocol))
		return 1;
	
	/* do we have it queued for later? */
	for(kg=irc->otr->todo; kg; kg=kg->next) {
		if(!strcmp(handle, kg->accountname) &&
		   !strcmp(protocol, kg->protocol))
			return 1;
	}
	
	return 0;
}

void otr_keygen(irc_t *irc, const char *handle, const char *protocol)
{
	/* do nothing if a key for the requested account is already being generated */
	if(keygen_in_progress(irc, handle, protocol))
		return;

	/* see if we already have a keygen child running. if not, start one and put a
	   handler on its output. */
	if(!irc->otr->keygen || waitpid(irc->otr->keygen, NULL, WNOHANG)) {
		pid_t p;
		int to[2], from[2];
		FILE *tof, *fromf;
		
		if(pipe(to) < 0 || pipe(from) < 0) {
			irc_rootmsg(irc, "otr keygen: couldn't create pipe: %s", strerror(errno));
			return;
		}
		
		tof = fdopen(to[1], "w");
		fromf = fdopen(from[0], "r");
		if(!tof || !fromf) {
			irc_rootmsg(irc, "otr keygen: couldn't streamify pipe: %s", strerror(errno));
			return;
		}
		
		p = fork();
		if(p<0) {
			irc_rootmsg(irc, "otr keygen: couldn't fork: %s", strerror(errno));
			return;
		}
		
		if(!p) {
			/* child process */
			signal(SIGTERM, exit);
			keygen_child_main(irc->otr->us, to[0], from[1]);
			exit(0);
		}
		
		irc->otr->keygen = p;
		irc->otr->to = tof;
		irc->otr->from = fromf;
		irc->otr->sent_accountname = NULL;
		irc->otr->sent_protocol = NULL;
		irc->otr->todo = NULL;
		b_input_add(from[0], B_EV_IO_READ, keygen_finish_handler, irc);
	}
	
	/* is the keygen slave currently working? */
	if(irc->otr->sent_accountname) {
		/* enqueue our job for later transmission */
		kg_t **kg = &irc->otr->todo;
		while(*kg)
			kg=&((*kg)->next);
		*kg = g_new0(kg_t, 1);
		(*kg)->accountname = g_strdup(handle);
		(*kg)->protocol = g_strdup(protocol);
	} else {
		/* send our job over and remember it */
		fprintf(irc->otr->to, "%s\n%s\n", handle, protocol);
		fflush(irc->otr->to);
		irc->otr->sent_accountname = g_strdup(handle);
		irc->otr->sent_protocol = g_strdup(protocol);
	}
}

void keygen_child_main(OtrlUserState us, int infd, int outfd)
{
	FILE *input, *output;
	char filename[128], accountname[512], protocol[512];
	gcry_error_t e;
	int tempfd;
	
	input = fdopen(infd, "r");
	output = fdopen(outfd, "w");
	
	while(!feof(input) && !ferror(input) && !feof(output) && !ferror(output)) {
		myfgets(accountname, 512, input);
		myfgets(protocol, 512, input);
		
		strncpy(filename, "/tmp/bitlbee-XXXXXX", 128);
		tempfd = mkstemp(filename);
		close(tempfd);

		e = otrl_privkey_generate(us, filename, accountname, protocol);
		if(e) {
			fprintf(output, "\n");  /* this means failure */
			fprintf(output, "otr keygen: %s\n", gcry_strerror(e));
			unlink(filename);
		} else {
			fprintf(output, "%s\n", filename);
			fprintf(output, "otr keygen for %s/%s complete\n", accountname, protocol);
		}
		fflush(output);
	}
	
	fclose(input);
	fclose(output);
}

gboolean keygen_finish_handler(gpointer data, gint fd, b_input_condition cond)
{
	irc_t *irc = (irc_t *)data;
	char filename[512], msg[512];

	myfgets(filename, 512, irc->otr->from);
	myfgets(msg, 512, irc->otr->from);
	
	irc_rootmsg(irc, "%s", msg);
	if(filename[0]) {
		if(strsane(irc->user->nick)) {
			char *kf = g_strdup_printf("%s%s.otr_keys", global.conf->configdir, irc->user->nick);
			char *tmp = g_strdup_printf("%s.new", kf);
			copyfile(filename, tmp);
			unlink(filename);
			rename(tmp,kf);
			otrl_privkey_read(irc->otr->us, kf);
			g_free(kf);
			g_free(tmp);
		} else {
			otrl_privkey_read(irc->otr->us, filename);
			unlink(filename);
		}
	}
	
	/* forget this job */
	g_free(irc->otr->sent_accountname);
	g_free(irc->otr->sent_protocol);
	irc->otr->sent_accountname = NULL;
	irc->otr->sent_protocol = NULL;
	
	/* see if there are any more in the queue */
	if(irc->otr->todo) {
		kg_t *p = irc->otr->todo;
		/* send the next one over */
		fprintf(irc->otr->to, "%s\n%s\n", p->accountname, p->protocol);
		fflush(irc->otr->to);
		irc->otr->sent_accountname = p->accountname;
		irc->otr->sent_protocol = p->protocol;
		irc->otr->todo = p->next;
		g_free(p);
		return TRUE;   /* keep watching */
	} else {
		/* okay, the slave is idle now, so kill him */
		fclose(irc->otr->from);
		fclose(irc->otr->to);
		irc->otr->from = irc->otr->to = NULL;
		kill(irc->otr->keygen, SIGTERM);
		waitpid(irc->otr->keygen, NULL, 0);
		irc->otr->keygen = 0;
		return FALSE;  /* unregister ourselves */
	}
}

void copyfile(const char *a, const char *b)
{
	int fda, fdb;
	int n;
	char buf[1024];
	
	fda = open(a, O_RDONLY);
	fdb = open(b, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	
	while((n=read(fda, buf, 1024)) > 0)
		write(fdb, buf, n);
	
	close(fda);
	close(fdb);	
}

void myfgets(char *s, int size, FILE *stream)
{
	if(!fgets(s, size, stream)) {
		s[0] = '\0';
	} else {
		int n = strlen(s);
		if(n>0 && s[n-1] == '\n')
			s[n-1] = '\0';
	}
}

void yes_keygen(void *data)
{
	account_t *acc = (account_t *)data;
	irc_t *irc = acc->bee->ui_data;
	
	if(keygen_in_progress(irc, acc->user, acc->prpl->name)) {
		irc_rootmsg(irc, "keygen for %s/%s already in progress",
			acc->user, acc->prpl->name);
	} else {
		irc_rootmsg(irc, "starting background keygen for %s/%s",
			acc->user, acc->prpl->name);
		irc_rootmsg(irc, "you will be notified when it completes");
		otr_keygen(irc, acc->user, acc->prpl->name);
	}
}

/* check whether a string is safe to use in a path component */
int strsane(const char *s)
{
	return strpbrk(s, "/\\") == NULL;
}

/* vim: set noet ts=4 sw=4: */
