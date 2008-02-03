#include "bitlbee.h"
#ifdef WITH_OTR
#include "irc.h"
#include "otr.h"
#include <sys/types.h>
#include <unistd.h>

/**
files used to store OTR data:
  $configdir/$nick.otr_keys
  $configdir/$nick.otr_fprints
 **/


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

void op_gone_secure(void *opdata, ConnContext *context);

void op_gone_insecure(void *opdata, ConnContext *context);

void op_still_secure(void *opdata, ConnContext *context, int is_reply);

void op_log_message(void *opdata, const char *message);

/* TODO: int op_max_message_size(void *opdata, ConnContext *context); */

/* TODO: const char *op_account_name(void *opdata, const char *account,
	const char *protocol); */


/** otr sub-command handlers: **/

/* TODO: void cmd_otr_keygen(irc_t *irc, char **args); */
void cmd_otr_abort(irc_t *irc, char **args); /* TODO: does this cmd even make sense? */
void cmd_otr_request(irc_t *irc, char **args); /* TODO: do we even need this? */
void cmd_otr_auth(irc_t *irc, char **args);
/* TODO: void cmd_otr_affirm(irc_t *irc, char **args); */
void cmd_otr_fprints(irc_t *irc, char **args);
void cmd_otr_info(irc_t *irc, char **args);
void cmd_otr_policy(irc_t *irc, char **args);

const command_t otr_commands[] = {
	{ "abort",    1, &cmd_otr_abort,    0 },
	{ "request",  1, &cmd_otr_request,  0 },
	{ "auth",     2, &cmd_otr_auth,     0 },
	{ "fprints",  0, &cmd_otr_fprints,  0 },
	{ "info",     1, &cmd_otr_info,     0 },
	{ "policy",   0, &cmd_otr_policy,   0 },
	{ NULL }
};


/** misc. helpers/subroutines: **/

/* start background thread to generate a (new) key for a given account */
void otr_keygen(irc_t *irc, const char *handle, const char *protocol);
/* keygen thread main func */
gpointer otr_keygen_thread_func(gpointer data);
/* mainloop handler for when keygen thread finishes */
gboolean keygen_finish_handler(gpointer data, gint fd, b_input_condition cond);
/* data to be passed to otr_keygen_thread_func */
struct kgdata {
	irc_t *irc;            /* access to OTR userstate */
	char *keyfile;         /* free me! */
	const char *handle;      /* don't free! */
	const char *protocol;    /* don't free! */
	GMutex *mutex;         /* lock for the 'done' flag, free me! */
	int done;              /* is the thread done? */
	gcry_error_t result;   /* return value of otrl_privkey_generate */
};

/* yes/no handlers for "generate key now?" */
void yes_keygen(gpointer w, void *data);
void no_keygen(gpointer w, void *data);

/* helper to make sure accountname and protocol match the incoming "opdata" */
struct im_connection *check_imc(void *opdata, const char *accountname,
	const char *protocol);

/* determine the nick for a given handle/protocol pair */
const char *peernick(irc_t *irc, const char *handle, const char *protocol);

/* handle SMP TLVs from a received message */
void otr_handle_smp(struct im_connection *ic, const char *handle, OtrlTLV *tlvs);

/* show the list of fingerprints associated with a given context */
void show_fingerprints(irc_t *irc, ConnContext *ctx);



/*** routines declared in otr.h: ***/

void otr_init(void)
{
	if(!g_thread_supported()) g_thread_init(NULL);
	OTRL_INIT;
	
	/* fill global OtrlMessageAppOps */
	global.otr_ops.policy = &op_policy;
	global.otr_ops.create_privkey = &op_create_privkey;
	global.otr_ops.is_logged_in = &op_is_logged_in;
	global.otr_ops.inject_message = &op_inject_message;
	global.otr_ops.notify = NULL;
	global.otr_ops.display_otr_message = &op_display_otr_message;
	global.otr_ops.update_context_list = NULL;
	global.otr_ops.protocol_name = NULL;
	global.otr_ops.protocol_name_free = NULL;
	global.otr_ops.new_fingerprint = &op_new_fingerprint;
	global.otr_ops.write_fingerprints = &op_write_fingerprints;
	global.otr_ops.gone_secure = &op_gone_secure;
	global.otr_ops.gone_insecure = &op_gone_insecure;
	global.otr_ops.still_secure = &op_still_secure;
	global.otr_ops.log_message = &op_log_message;
	global.otr_ops.max_message_size = NULL;
	global.otr_ops.account_name = NULL;
	global.otr_ops.account_name_free = NULL;
}

/* Notice on the otr_mutex:

   The incoming/outgoing message handlers try to lock the otr_mutex. If they succeed,
   this will prevent a concurrent keygen (possibly spawned by that very command)
   from messing up the userstate. If the lock fails, that means there already is
   a keygen in progress. Instead of blocking for an unknown time, they
   will bail out gracefully, informing the user of this temporary "coma".
   TODO: Hold back incoming/outgoing messages and process them when keygen completes?

   The other routines do not lock the otr_mutex themselves, it is done as a
   catch-all in the root command handler. Rationale:
     a) it's easy to code
     b) it makes it obvious that no command can get its userstate corrupted
     c) the "irc" struct is readily available there for feedback to the user
 */

void otr_load(irc_t *irc)
{
	char s[512];
	account_t *a;
	gcry_error_t e;

	log_message(LOGLVL_DEBUG, "otr_load '%s'", irc->nick);

	g_snprintf(s, 511, "%s%s.otr_keys", global.conf->configdir, irc->nick);
	e = otrl_privkey_read(irc->otr_us, s);
	if(e && e!=ENOENT) {
		log_message(LOGLVL_ERROR, "otr load: %s: %s", s, strerror(e));
	}
	g_snprintf(s, 511, "%s%s.otr_fprints", global.conf->configdir, irc->nick);
	e = otrl_privkey_read_fingerprints(irc->otr_us, s, NULL, NULL);
	if(e && e!=ENOENT) {
		log_message(LOGLVL_ERROR, "otr load: %s: %s", s, strerror(e));
	}
	
	/* check for otr keys on all accounts */
	for(a=irc->accounts; a; a=a->next) {
		otr_check_for_key(a);
	}
}

void otr_save(irc_t *irc)
{
	char s[512];
	gcry_error_t e;

	log_message(LOGLVL_DEBUG, "otr_save '%s'", irc->nick);

	g_snprintf(s, 511, "%s%s.otr_fprints", global.conf->configdir, irc->nick);
	e = otrl_privkey_write_fingerprints(irc->otr_us, s);
	if(e) {
		log_message(LOGLVL_ERROR, "otr save: %s: %s", s, strerror(e));
	}
}

void otr_remove(const char *nick)
{
	char s[512];
	
	log_message(LOGLVL_DEBUG, "otr_remove '%s'", nick);

	g_snprintf(s, 511, "%s%s.otr_keys", global.conf->configdir, nick);
	unlink(s);
	g_snprintf(s, 511, "%s%s.otr_fprints", global.conf->configdir, nick);
	unlink(s);
}

void otr_rename(const char *onick, const char *nnick)
{
	char s[512], t[512];
	
	log_message(LOGLVL_DEBUG, "otr_rename '%s' -> '%s'", onick, nnick);

	g_snprintf(s, 511, "%s%s.otr_keys", global.conf->configdir, onick);
	g_snprintf(t, 511, "%s%s.otr_keys", global.conf->configdir, nnick);
	rename(s,t);
	g_snprintf(s, 511, "%s%s.otr_fprints", global.conf->configdir, onick);
	g_snprintf(t, 511, "%s%s.otr_fprints", global.conf->configdir, nnick);
	rename(s,t);
}

void otr_check_for_key(account_t *a)
{
	irc_t *irc = a->irc;
	char buf[45];
	char *fp;
	
	fp = otrl_privkey_fingerprint(irc->otr_us, buf, a->user, a->prpl->name);
	if(fp) {
		irc_usermsg(irc, "otr: %s/%s ready with f'print %s",
			a->user, a->prpl->name, fp);
	} else {
		otr_keygen(irc, a->user, a->prpl->name);
	}
}

char *otr_handle_message(struct im_connection *ic, const char *handle, const char *msg)
{
	int ignore_msg;
	char *newmsg = NULL;
	OtrlTLV *tlvs = NULL;
	char *colormsg;
	
    if(!g_mutex_trylock(ic->irc->otr_mutex)) {
    	/* TODO: queue msgs received during keygen for later */
		irc_usermsg(ic->irc, "msg from %s/%s during keygen - dropped",
			handle, ic->acc->prpl->name);
		return NULL;
	}

	ignore_msg = otrl_message_receiving(ic->irc->otr_us, &global.otr_ops, ic,
		ic->acc->user, ic->acc->prpl->name, handle, msg, &newmsg,
		&tlvs, NULL, NULL);

	otr_handle_smp(ic, handle, tlvs);
	
	if(ignore_msg) {
		/* this was an internal OTR protocol message */
		g_mutex_unlock(ic->irc->otr_mutex);
		return NULL;
	} else if(!newmsg) {
		/* this was a non-OTR message */
		g_mutex_unlock(ic->irc->otr_mutex);
		return g_strdup(msg);
	} else {
		/* OTR has processed this message */
		ConnContext *context = otrl_context_find(ic->irc->otr_us, handle,
			ic->acc->user, ic->acc->prpl->name, 0, NULL, NULL, NULL);
		if(context && context->msgstate == OTRL_MSGSTATE_ENCRYPTED) {
			/* color according to f'print trust */
			char color;
			const char *trust = context->active_fingerprint->trust;
			if(trust && trust[0] != '\0')
				color='3';   /* green */
			else
				color='5';   /* red */
			colormsg = g_strdup_printf("\x03%c%s\x0F", color, newmsg);
		} else {
			colormsg = g_strdup(newmsg);
		}
		otrl_message_free(newmsg);
		g_mutex_unlock(ic->irc->otr_mutex);
		return colormsg;
	}
}

int otr_send_message(struct im_connection *ic, const char *handle, const char *msg, int flags)
{	
    int st;
    char *otrmsg = NULL;
    ConnContext *ctx = NULL;
    
    if(!g_mutex_trylock(ic->irc->otr_mutex)) {
    	irc_usermsg(ic->irc, "msg to %s/%s during keygen - not sent",
    		handle, ic->acc->prpl->name);
    	return 1;
    }
    
	st = otrl_message_sending(ic->irc->otr_us, &global.otr_ops, ic,
		ic->acc->user, ic->acc->prpl->name, handle,
		msg, NULL, &otrmsg, NULL, NULL);
	if(st) {
		g_mutex_unlock(ic->irc->otr_mutex);
		return st;
	}

	ctx = otrl_context_find(ic->irc->otr_us,
			handle, ic->acc->user, ic->acc->prpl->name,
			1, NULL, NULL, NULL);

	if(otrmsg) {
		if(!ctx) {
			otrl_message_free(otrmsg);
			g_mutex_unlock(ic->irc->otr_mutex);
			return 1;
		}
		st = otrl_message_fragment_and_send(&global.otr_ops, ic, ctx,
			otrmsg, OTRL_FRAGMENT_SEND_ALL, NULL);
		otrl_message_free(otrmsg);
	} else {
		/* yeah, well, some const casts as usual... ;-) */
		st = ic->acc->prpl->buddy_msg( ic, (char *)handle, (char *)msg, flags );
	}
	
	g_mutex_unlock(ic->irc->otr_mutex);
	return st;
}

void cmd_otr(irc_t *irc, char **args)
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
		irc_usermsg(irc, "%s %s: unknown subcommand, see \x02help otr\x02",
			args[0], args[1]);
		return;
	}
	
	if(!args[cmd->required_parameters+1]) {
		irc_usermsg(irc, "%s %s: not enough arguments (%d req.)",
			args[0], args[1], cmd->required_parameters);
		return;
	}
	
	cmd->execute(irc, args+1);
}


/*** OTR "MessageAppOps" callbacks for global.otr_ui: ***/

OtrlPolicy op_policy(void *opdata, ConnContext *context)
{
	/* TODO: OTR policy configurable */
	return OTRL_POLICY_OPPORTUNISTIC;
}

void op_create_privkey(void *opdata, const char *accountname,
	const char *protocol)
{
	struct im_connection *ic = check_imc(opdata, accountname, protocol);
	char *s;
	
	log_message(LOGLVL_DEBUG, "op_create_privkey '%s' '%s'", accountname, protocol);

	s = g_strdup_printf("oops, no otr privkey for %s/%s - generate one now?",
		accountname, protocol);
	query_add(ic->irc, ic, s, yes_keygen, no_keygen, ic->acc);
}

int op_is_logged_in(void *opdata, const char *accountname,
	const char *protocol, const char *recipient)
{
	struct im_connection *ic = check_imc(opdata, accountname, protocol);
	user_t *u;

	log_message(LOGLVL_DEBUG, "op_is_logged_in '%s' '%s' '%s'", accountname, protocol, recipient);
	
	/* lookup the user_t for the given recipient */
	u = user_findhandle(ic, recipient);
	if(u) {
		if(u->online)
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

	log_message(LOGLVL_DEBUG, "op_inject_message '%s' '%s' '%s' '%s'", accountname, protocol, recipient, message);

	if (strcmp(accountname, recipient) == 0) {
		/* huh? injecting messages to myself? */
		irc_usermsg(ic->irc, "note to self: %s", message);
	} else {
		/* need to drop some consts here :-( */
		/* TODO: get flags into op_inject_message?! */
		ic->acc->prpl->buddy_msg(ic, (char *)recipient, (char *)message, 0);
		/* ignoring return value :-/ */
	}
}

int op_display_otr_message(void *opdata, const char *accountname,
	const char *protocol, const char *username, const char *msg)
{
	struct im_connection *ic = check_imc(opdata, accountname, protocol);

	log_message(LOGLVL_DEBUG, "op_display_otr_message '%s' '%s' '%s' '%s'", accountname, protocol, username, msg);

	irc_usermsg(ic->irc, "%s", msg);

	return 0;
}

void op_new_fingerprint(void *opdata, OtrlUserState us,
	const char *accountname, const char *protocol,
	const char *username, unsigned char fingerprint[20])
{
	struct im_connection *ic = check_imc(opdata, accountname, protocol);
	char hunam[45];		/* anybody looking? ;-) */
	
	otrl_privkey_hash_to_human(hunam, fingerprint);
	log_message(LOGLVL_DEBUG, "op_new_fingerprint '%s' '%s' '%s' '%s'", accountname, protocol, username, hunam);

	irc_usermsg(ic->irc, "new fingerprint for %s: %s",
		peernick(ic->irc, username, protocol), hunam);
}

void op_write_fingerprints(void *opdata)
{
	struct im_connection *ic = (struct im_connection *)opdata;

	log_message(LOGLVL_DEBUG, "op_write_fingerprints");

	otr_save(ic->irc);
}

void op_gone_secure(void *opdata, ConnContext *context)
{
	struct im_connection *ic =
		check_imc(opdata, context->accountname, context->protocol);

	log_message(LOGLVL_DEBUG, "op_gone_secure '%s' '%s' '%s'", context->accountname, context->protocol, context->username);

	irc_usermsg(ic->irc, "conversation with %s is now off the record",
		peernick(ic->irc, context->username, context->protocol));
}

void op_gone_insecure(void *opdata, ConnContext *context)
{
	struct im_connection *ic =
		check_imc(opdata, context->accountname, context->protocol);

	log_message(LOGLVL_DEBUG, "op_gone_insecure '%s' '%s' '%s'", context->accountname, context->protocol, context->username);

	irc_usermsg(ic->irc, "conversation with %s is now in the clear",
		peernick(ic->irc, context->username, context->protocol));
}

void op_still_secure(void *opdata, ConnContext *context, int is_reply)
{
	struct im_connection *ic =
		check_imc(opdata, context->accountname, context->protocol);

	log_message(LOGLVL_DEBUG, "op_still_secure '%s' '%s' '%s' is_reply=%d",
		context->accountname, context->protocol, context->username, is_reply);

	irc_usermsg(ic->irc, "otr connection with %s has been refreshed",
		peernick(ic->irc, context->username, context->protocol));
}

void op_log_message(void *opdata, const char *message)
{
	log_message(LOGLVL_INFO, "%s", message);
}


/*** OTR sub-command handlers ***/

void cmd_otr_abort(irc_t *irc, char **args)
{
	user_t *u;

	u = user_find(irc, args[1]);
	if(!u || !u->ic) {
		irc_usermsg(irc, "%s: unknown user", args[1]);
		return;
	}
	
	otrl_message_disconnect(irc->otr_us, &global.otr_ops,
		u->ic, u->ic->acc->user, u->ic->acc->prpl->name, u->handle);
}

void cmd_otr_request(irc_t *irc, char **args)
{
	user_t *u;

	u = user_find(irc, args[1]);
	if(!u || !u->ic) {
		irc_usermsg(irc, "%s: unknown user", args[1]);
		return;
	}
	if(!u->online) {
		irc_usermsg(irc, "%s is offline", args[1]);
		return;
	}
	
	imc_buddy_msg(u->ic, u->handle, "?OTR?", 0);
}

void cmd_otr_auth(irc_t *irc, char **args)
{
	user_t *u;
	ConnContext *ctx;
	
	u = user_find(irc, args[1]);
	if(!u || !u->ic) {
		irc_usermsg(irc, "%s: unknown user", args[1]);
		return;
	}
	if(!u->online) {
		irc_usermsg(irc, "%s is offline", args[1]);
		return;
	}
	
	ctx = otrl_context_find(irc->otr_us, u->handle,
		u->ic->acc->user, u->ic->acc->prpl->name, 1, NULL, NULL, NULL);
	if(!ctx) {
		/* huh? out of memory or what? */
		return;
	}

	if(ctx->smstate->nextExpected != OTRL_SMP_EXPECT1) {
		log_message(LOGLVL_INFO,
			"SMP already in phase %d, sending abort before reinitiating",
			ctx->smstate->nextExpected+1);
		otrl_message_abort_smp(irc->otr_us, &global.otr_ops, u->ic, ctx);
		otrl_sm_state_free(ctx->smstate);
	}
	
	/* warning: the following assumes that smstates are cleared whenever an SMP
	   is completed or aborted! */ 
	if(ctx->smstate->secret == NULL) {
		irc_usermsg(irc, "smp: initiating with %s...", u->nick);
		otrl_message_initiate_smp(irc->otr_us, &global.otr_ops,
			u->ic, ctx, (unsigned char *)args[2], strlen(args[2]));
		/* smp is now in EXPECT2 */
	} else {
		/* if we're still in EXPECT1 but smstate is initialized, we must have
		   received the SMP1, so let's issue a response */
		irc_usermsg(irc, "smp: responding to %s...", u->nick);
		otrl_message_respond_smp(irc->otr_us, &global.otr_ops,
			u->ic, ctx, (unsigned char *)args[2], strlen(args[2]));
		/* smp is now in EXPECT3 */
	}
}

void cmd_otr_fprints(irc_t *irc, char **args)
{
	if(args[1]) {
		/* list given buddy's fingerprints */
		user_t *u;
		ConnContext *ctx;
	
		u = user_find(irc, args[1]);
		if(!u || !u->ic) {
			irc_usermsg(irc, "%s: unknown user", args[1]);
			return;
		}
	
		ctx = otrl_context_find(irc->otr_us, u->handle,
			u->ic->acc->user, u->ic->acc->prpl->name, 0, NULL, NULL, NULL);
		if(!ctx) {
			irc_usermsg(irc, "no fingerprints");
		} else {
			show_fingerprints(irc, ctx);
		}
	} else {
		/* list all known fingerprints */
		ConnContext *ctx;
		for(ctx=irc->otr_us->context_root; ctx; ctx=ctx->next) {
			irc_usermsg(irc, "[%s]", peernick(irc, ctx->username, ctx->protocol));
			show_fingerprints(irc, ctx);
		}
		if(!irc->otr_us->context_root) {
			irc_usermsg(irc, "no fingerprints");
		}
	}
}

void cmd_otr_info(irc_t *irc, char **args)
{
	user_t *u;
	ConnContext *ctx;
	Fingerprint *fp;
	char human[45];
	const char *offer_status;
	const char *message_state;
	const char *trust;

	if(!args) {
		irc_usermsg(irc, "no args?!");
		return;
	}
	if(!args[1]) {
		irc_usermsg(irc, "no args[1]?!");
		return;
	}
	u = user_find(irc, args[1]);
	if(!u || !u->ic) {
		irc_usermsg(irc, "%s: unknown user", args[1]);
		return;
	}
	
	ctx = otrl_context_find(irc->otr_us, u->handle,
		u->ic->acc->user, u->ic->acc->prpl->name, 0, NULL, NULL, NULL);
	if(!ctx) {
		irc_usermsg(irc, "no otr info on %s", args[1]);
		return;
	}

	switch(ctx->otr_offer) {
	case OFFER_NOT:       offer_status="none sent";          break;
	case OFFER_SENT:      offer_status="awaiting reply";     break;
	case OFFER_ACCEPTED:  offer_status="accepted our offer"; break;
	case OFFER_REJECTED:  offer_status="ignored our offer";  break;
	default:              offer_status="?";
	}

	switch(ctx->msgstate) {
	case OTRL_MSGSTATE_PLAINTEXT: message_state="cleartext"; break;
	case OTRL_MSGSTATE_ENCRYPTED: message_state="encrypted"; break;
	case OTRL_MSGSTATE_FINISHED:  message_state="shut down"; break;
	default:                      message_state="?";
	}

	irc_usermsg(irc, "%s is %s/%s; we are %s/%s to them", args[1],
		ctx->username, ctx->protocol, ctx->accountname, ctx->protocol);
	irc_usermsg(irc, "  otr offer status: %s", offer_status);
	irc_usermsg(irc, "  connection state: %s", message_state);
	irc_usermsg(irc, "  protocol version: %d", ctx->protocol_version);
	fp = ctx->active_fingerprint;
	if(!fp) {
		irc_usermsg(irc, "  active f'print:   none");
	} else {
		otrl_privkey_hash_to_human(human, fp->fingerprint);
		if(!fp->trust || fp->trust[0] == '\0') {
			trust="untrusted";
		} else {
			trust=fp->trust;
		}
		irc_usermsg(irc, "  active f'print:   %s (%s)", human, trust);
	}
}

void cmd_otr_policy(irc_t *irc, char **args)
{
	irc_usermsg(irc, "n/a: not implemented");
}


/*** local helpers / subroutines: ***/

/* Socialist Millionaires' Protocol */
void otr_handle_smp(struct im_connection *ic, const char *handle, OtrlTLV *tlvs)
{
	irc_t *irc = ic->irc;
	OtrlUserState us = irc->otr_us;
	OtrlMessageAppOps *ops = &global.otr_ops;
	OtrlTLV *tlv = NULL;
	ConnContext *context;
	NextExpectedSMP nextMsg;
	user_t *u;

	u = user_findhandle(ic, handle);
	if(!u) return;
	context = otrl_context_find(us, handle,
		ic->acc->user, ic->acc->prpl->name, 1, NULL, NULL, NULL);
	if(!context) {
		/* huh? out of memory or what? */
		return;
	}
	nextMsg = context->smstate->nextExpected;

	tlv = otrl_tlv_find(tlvs, OTRL_TLV_SMP1);
	if (tlv) {
		if (nextMsg != OTRL_SMP_EXPECT1) {
			irc_usermsg(irc, "smp %s: spurious SMP1 received, aborting", u->nick);
			otrl_message_abort_smp(us, ops, u->ic, context);
			otrl_sm_state_free(context->smstate);
		} else {
			irc_usermsg(irc, "smp: initiated by %s"
				" - respond with \x02otr smp %s <secret>\x02",
				u->nick, u->nick);
			/* smp stays in EXPECT1 until user responds */
		}
	}
	tlv = otrl_tlv_find(tlvs, OTRL_TLV_SMP2);
	if (tlv) {
		if (nextMsg != OTRL_SMP_EXPECT2) {
			irc_usermsg(irc, "smp %s: spurious SMP2 received, aborting", u->nick);
			otrl_message_abort_smp(us, ops, u->ic, context);
			otrl_sm_state_free(context->smstate);
		} else {
			/* SMP2 received, otrl_message_receiving will have sent SMP3 */
			context->smstate->nextExpected = OTRL_SMP_EXPECT4;
		}
	}
	tlv = otrl_tlv_find(tlvs, OTRL_TLV_SMP3);
	if (tlv) {
		if (nextMsg != OTRL_SMP_EXPECT3) {
			irc_usermsg(irc, "smp %s: spurious SMP3 received, aborting", u->nick);
			otrl_message_abort_smp(us, ops, u->ic, context);
			otrl_sm_state_free(context->smstate);
		} else {
			/* SMP3 received, otrl_message_receiving will have sent SMP4 and set fp trust */
			const char *trust = context->active_fingerprint->trust;
			if(!trust || trust[0]=='\0') {
				irc_usermsg(irc, "smp %s: secrets did not match, fingerprint not trusted",
					u->nick);
			} else {
				irc_usermsg(irc, "smp %s: secrets proved equal, fingerprint trusted",
					u->nick);
			}
			otrl_sm_state_free(context->smstate);
			/* smp is in back in EXPECT1 */
		}
	}
	tlv = otrl_tlv_find(tlvs, OTRL_TLV_SMP4);
	if (tlv) {
		if (nextMsg != OTRL_SMP_EXPECT4) {
			irc_usermsg(irc, "smp %s: spurious SMP4 received, aborting", u->nick);
			otrl_message_abort_smp(us, ops, u->ic, context);
			otrl_sm_state_free(context->smstate);
		} else {
			/* SMP4 received, otrl_message_receiving will have set fp trust */
			const char *trust = context->active_fingerprint->trust;
			if(!trust || trust[0]=='\0') {
				irc_usermsg(irc, "smp %s: secrets did not match, fingerprint not trusted",
					u->nick);
			} else {
				irc_usermsg(irc, "smp %s: secrets proved equal, fingerprint trusted",
					u->nick);
			}
			otrl_sm_state_free(context->smstate);
			/* smp is in back in EXPECT1 */
		}
	}
	tlv = otrl_tlv_find(tlvs, OTRL_TLV_SMP_ABORT);
	if (tlv) {
	 	irc_usermsg(irc, "smp: received abort from %s", u->nick);
		otrl_sm_state_free(context->smstate);
		/* smp is in back in EXPECT1 */
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

const char *peernick(irc_t *irc, const char *handle, const char *protocol)
{
	user_t *u;
	static char fallback[512];
	
	g_snprintf(fallback, 511, "%s/%s", handle, protocol);
	for(u=irc->users; u; u=u->next) {
		struct prpl *prpl;
		if(!u->ic || !u->handle)
			break;
		prpl = u->ic->acc->prpl;
		if(strcmp(prpl->name, protocol) == 0
			&& prpl->handle_cmp(u->handle, handle) == 0) {
			return u->nick;
		}
	}
	
	return fallback;
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
			irc_usermsg(irc, "\x02%s (%s)\x02", human, trust);
		} else {
			irc_usermsg(irc, "%s (%s)", human, trust);
		}
	}
	if(count==0)
		irc_usermsg(irc, "no fingerprints");
}

void otr_keygen(irc_t *irc, const char *handle, const char *protocol)
{
	GError *err;
	GThread *thr;
	struct kgdata *kg;
	gint ev;
	
	irc_usermsg(irc, "generating new otr privkey for %s/%s...",
		handle, protocol);
	
	kg = g_new0(struct kgdata, 1);
	if(!kg) {
		irc_usermsg(irc, "otr keygen failed: out of memory");
		return;
	}

	/* Assemble the job description to be passed to thread and handler */
	kg->irc = irc;
	kg->keyfile = g_strdup_printf("%s%s.otr_keys", global.conf->configdir, kg->irc->nick);
	if(!kg->keyfile) {
		irc_usermsg(irc, "otr keygen failed: out of memory");
		g_free(kg);
		return;
	}
	kg->handle = handle;
	kg->protocol = protocol;
	kg->mutex = g_mutex_new();
	if(!kg->mutex) {
		irc_usermsg(irc, "otr keygen failed: couldn't create mutex");
		g_free(kg->keyfile);
		g_free(kg);
		return;
	}
	kg->done = FALSE;

	/* Poll for completion of the thread periodically. I would have preferred
	   to just wait on a pipe but this way it's portable to Windows. *sigh*
	*/
	ev = b_timeout_add(1000, &keygen_finish_handler, kg);
	if(!ev) {
		irc_usermsg(irc, "otr keygen failed: couldn't register timeout");
		g_free(kg->keyfile);
		g_mutex_free(kg->mutex);
		g_free(kg);
		return;
	}

	thr = g_thread_create(&otr_keygen_thread_func, kg, FALSE, &err);
	if(!thr) {
		irc_usermsg(irc, "otr keygen failed: %s", err->message);
		g_free(kg->keyfile);
		g_mutex_free(kg->mutex);
		g_free(kg);
		b_event_remove(ev);
	}
}

gpointer otr_keygen_thread_func(gpointer data)
{
	struct kgdata *kg = (struct kgdata *)data;
	
	/* lock OTR subsystem and do the work */
	g_mutex_lock(kg->irc->otr_mutex);
	kg->result = otrl_privkey_generate(kg->irc->otr_us, kg->keyfile, kg->handle,
		kg->protocol);
	g_mutex_unlock(kg->irc->otr_mutex);
	/* OTR enabled again */
	
	/* notify mainloop */
	g_mutex_lock(kg->mutex);
	kg->done = TRUE;
	g_mutex_unlock(kg->mutex);
	
	return NULL;
}

gboolean keygen_finish_handler(gpointer data, gint fd, b_input_condition cond)
{
	struct kgdata *kg = (struct kgdata *)data;
	int done;
	
	g_mutex_lock(kg->mutex);
	done = kg->done;
	g_mutex_unlock(kg->mutex);
	if(kg->done) {
		if(kg->result) {
			irc_usermsg(kg->irc, "otr keygen: %s", strerror(kg->result));
		} else {
			irc_usermsg(kg->irc, "otr keygen for %s/%s complete", kg->handle, kg->protocol);
		}
		g_free(kg->keyfile);
		g_mutex_free(kg->mutex);
		g_free(kg);
		return FALSE; /* unregister timeout */
	}

	return TRUE;  /* still working, continue checking */
}

void yes_keygen(gpointer w, void *data)
{
	account_t *acc = (account_t *)data;
	
	otr_keygen(acc->irc, acc->user, acc->prpl->name);
}

void no_keygen(gpointer w, void *data)
{
	account_t *acc = (account_t *)data;
	
	irc_usermsg(acc->irc, "proceeding without key, otr inoperable on %s/%s",
		acc->user, acc->prpl->name);
}


#else /* WITH_OTR undefined */

void cmd_otr(irc_t *irc, char **args)
{
	irc_usermsg(irc, "otr: n/a, compiled without OTR support");
}

#endif
