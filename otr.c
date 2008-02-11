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

void op_gone_insecure(void *opdata, ConnContext *context);

void op_still_secure(void *opdata, ConnContext *context, int is_reply);

void op_log_message(void *opdata, const char *message);

int op_max_message_size(void *opdata, ConnContext *context);

const char *op_account_name(void *opdata, const char *account, const char *protocol);


/** otr sub-command handlers: **/

void cmd_otr_connect(irc_t *irc, char **args);
void cmd_otr_disconnect(irc_t *irc, char **args);
void cmd_otr_smp(irc_t *irc, char **args);
void cmd_otr_trust(irc_t *irc, char **args);
void cmd_otr_info(irc_t *irc, char **args);
void cmd_otr_keygen(irc_t *irc, char **args);
/* void cmd_otr_forget(irc_t *irc, char **args); */

const command_t otr_commands[] = {
	{ "connect",     1, &cmd_otr_connect,    0 },
	{ "disconnect",  1, &cmd_otr_disconnect, 0 },
	{ "smp",         2, &cmd_otr_smp,        0 },
	{ "trust",       6, &cmd_otr_trust,      0 },
	{ "info",        0, &cmd_otr_info,       0 },
	{ "keygen",      1, &cmd_otr_keygen,     0 },
	/*
	{ "forget",      1, &cmd_otr_forget,     0 },
	*/
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

/* determine the nick for a given handle/protocol pair
   returns "handle/protocol" if not found */
const char *peernick(irc_t *irc, const char *handle, const char *protocol);

/* turn a hexadecimal digit into its numerical value */
int hexval(char a);

/* determine the user_t for a given handle/protocol pair
   returns NULL if not found */
user_t *peeruser(irc_t *irc, const char *handle, const char *protocol);

/* handle SMP TLVs from a received message */
void otr_handle_smp(struct im_connection *ic, const char *handle, OtrlTLV *tlvs);

/* update op/voice flag of given user according to encryption state and settings
   returns 0 if neither op_buddies nor voice_buddies is set to "encrypted",
   i.e. msgstate should be announced seperately */
int otr_update_modeflags(irc_t *irc, user_t *u);

/* show general info about the OTR subsystem; called by 'otr info' */
void show_general_otr_info(irc_t *irc);

/* show info about a given OTR context */
void show_otr_context_info(irc_t *irc, ConnContext *ctx);

/* show the list of fingerprints associated with a given context */
void show_fingerprints(irc_t *irc, ConnContext *ctx);

/* to log out accounts during keygen */
extern void cmd_account(irc_t *irc, char **cmd);


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
	global.otr_ops.max_message_size = &op_max_message_size;
	global.otr_ops.account_name = &op_account_name;
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
	OtrlPrivKey *k;
	
	k = otrl_privkey_find(irc->otr_us, a->user, a->prpl->name);
	if(k) {
		irc_usermsg(irc, "otr: %s/%s ready",
			a->user, a->prpl->name);
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
		irc_usermsg(ic->irc, "otr keygen in progress - msg from %s dropped",
			peernick(ic->irc, handle, ic->acc->prpl->name));
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
		irc_usermsg(ic->irc, "otr keygen in progress - msg to %s not sent",
			peernick(ic->irc, handle, ic->acc->prpl->name));
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
	const char *protocol, const char *username, const char *message)
{
	struct im_connection *ic = check_imc(opdata, accountname, protocol);
	char *msg = g_strdup(message);

	log_message(LOGLVL_DEBUG, "op_display_otr_message '%s' '%s' '%s' '%s'", accountname, protocol, username, message);

	strip_html(msg);
	irc_usermsg(ic->irc, "otr: %s", msg);

	g_free(msg);
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
	user_t *u;

	log_message(LOGLVL_DEBUG, "op_gone_secure '%s' '%s' '%s'", context->accountname, context->protocol, context->username);

	u = peeruser(ic->irc, context->username, context->protocol);
	if(!u) {
		log_message(LOGLVL_ERROR,
			"BUG: otr.c: op_gone_secure: user_t for %s/%s/%s not found!",
			context->username, context->protocol, context->accountname);
		return;
	}
	if(context->active_fingerprint->trust[0])
		u->encrypted = 2;
	else
		u->encrypted = 1;
	if(!otr_update_modeflags(ic->irc, u))
		irc_usermsg(ic->irc, "conversation with %s is now off the record", u->nick);
}

void op_gone_insecure(void *opdata, ConnContext *context)
{
	struct im_connection *ic =
		check_imc(opdata, context->accountname, context->protocol);
	user_t *u;

	log_message(LOGLVL_DEBUG, "op_gone_insecure '%s' '%s' '%s'", context->accountname, context->protocol, context->username);

	u = peeruser(ic->irc, context->username, context->protocol);
	if(!u) {
		log_message(LOGLVL_ERROR,
			"BUG: otr.c: op_gone_insecure: user_t for %s/%s/%s not found!",
			context->username, context->protocol, context->accountname);
		return;
	}
	u->encrypted = 0;
	if(!otr_update_modeflags(ic->irc, u))
		irc_usermsg(ic->irc, "conversation with %s is now in the clear", u->nick);
}

void op_still_secure(void *opdata, ConnContext *context, int is_reply)
{
	struct im_connection *ic =
		check_imc(opdata, context->accountname, context->protocol);
	user_t *u;

	log_message(LOGLVL_DEBUG, "op_still_secure '%s' '%s' '%s' is_reply=%d",
		context->accountname, context->protocol, context->username, is_reply);

	u = peeruser(ic->irc, context->username, context->protocol);
	if(!u) {
		log_message(LOGLVL_ERROR,
			"BUG: otr.c: op_still_secure: user_t for %s/%s/%s not found!",
			context->username, context->protocol, context->accountname);
		return;
	}
	if(context->active_fingerprint->trust[0])
		u->encrypted = 2;
	else
		u->encrypted = 1;
	if(!otr_update_modeflags(ic->irc, u))
		irc_usermsg(ic->irc, "otr connection with %s has been refreshed", u->nick);
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

	log_message(LOGLVL_DEBUG, "op_account_name '%s' '%s'", account, protocol);
	
	return peernick(ic->irc, account, protocol);
}


/*** OTR sub-command handlers ***/

void cmd_otr_disconnect(irc_t *irc, char **args)
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

void cmd_otr_connect(irc_t *irc, char **args)
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

void cmd_otr_smp(irc_t *irc, char **args)
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

void cmd_otr_trust(irc_t *irc, char **args)
{
	user_t *u;
	ConnContext *ctx;
	unsigned char raw[20];
	Fingerprint *fp;
	int i,j;
	
	u = user_find(irc, args[1]);
	if(!u || !u->ic) {
		irc_usermsg(irc, "%s: unknown user", args[1]);
		return;
	}
	
	ctx = otrl_context_find(irc->otr_us, u->handle,
		u->ic->acc->user, u->ic->acc->prpl->name, 0, NULL, NULL, NULL);
	if(!ctx) {
		irc_usermsg(irc, "%s: no otr context with user", args[1]);
		return;
	}
	
	/* convert given fingerprint to raw representation */
	for(i=0; i<5; i++) {
		for(j=0; j<4; j++) {
			char *p = args[2+i]+(2*j);
			char *q = p+1;
			int x, y;
			
			if(!*p || !*q) {
				irc_usermsg(irc, "failed: truncated fingerprint block %d", i+1);
				return;
			}
			
			x = hexval(*p);
			y = hexval(*q);
			if(x<0) {
				irc_usermsg(irc, "failed: %d. hex digit of block %d out of range", 2*j+1, i+1);
				return;
			}
			if(y<0) {
				irc_usermsg(irc, "failed: %d. hex digit of block %d out of range", 2*j+2, i+1);
				return;
			}

			raw[i*4+j] = x*16 + y;
		}
	}
	fp = otrl_context_find_fingerprint(ctx, raw, 0, NULL);
	if(!fp) {
		irc_usermsg(irc, "failed: no such fingerprint for %s", args[1]);
	} else {
		char *trust = args[7] ? args[7] : "affirmed";
		otrl_context_set_trust(fp, trust);
		irc_usermsg(irc, "fingerprint match, trust set to \"%s\"", trust);
		if(u->encrypted)
			u->encrypted = 2;
		otr_update_modeflags(irc, u);
	}
}

void cmd_otr_info(irc_t *irc, char **args)
{
	if(!args[1]) {
		show_general_otr_info(irc);
	} else {
		char *arg = g_strdup(args[1]);
		char *myhandle, *handle, *protocol;
		ConnContext *ctx;
		
		/* interpret arg as 'user/protocol/account' if possible */
		protocol = strchr(arg, '/');
		if(protocol) {
			*(protocol++) = '\0';
			myhandle = strchr(protocol, '/');
			if(!myhandle) {
				/* TODO: try to find a unique account for this context */
			}
		}
		if(protocol && myhandle) {
			*(myhandle++) = '\0';
			handle = arg;
			ctx = otrl_context_find(irc->otr_us, handle, myhandle, protocol, 0, NULL, NULL, NULL);
			if(!ctx) {
				irc_usermsg(irc, "no such context (%s %s %s)", handle, protocol, myhandle);
				g_free(arg);
				return;
			}
		} else {
			user_t *u = user_find(irc, args[1]);
			if(!u || !u->ic) {
				irc_usermsg(irc, "%s: unknown user", args[1]);
				g_free(arg);
				return;
			}
			ctx = otrl_context_find(irc->otr_us, u->handle, u->ic->acc->user,
				u->ic->acc->prpl->name, 0, NULL, NULL, NULL);
			if(!ctx) {
				irc_usermsg(irc, "no otr context with %s", args[1]);
				g_free(arg);
				return;
			}
		}
	
		/* show how we resolved the (nick) argument, if we did */
		if(handle!=arg) {
			irc_usermsg(irc, "%s is %s/%s; we are %s/%s to them", args[1],
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
		irc_usermsg(irc, "%s: invalid account number", args[1]);
		return;
	}
	
	a = irc->accounts;
	for(i=0; i<n && a; i++, a=a->next);
	if(!a) {
		irc_usermsg(irc, "%s: no such account", args[1]);
		return;
	}
	
	if(otrl_privkey_find(irc->otr_us, a->user, a->prpl->name)) {
		char *s = g_strdup_printf("account %d already has a key, replace it?", n);
		query_add(irc, a->ic, s, yes_keygen, no_keygen, a);
	} else {
		otr_keygen(irc, a->user, a->prpl->name);
	}
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

user_t *peeruser(irc_t *irc, const char *handle, const char *protocol)
{
	user_t *u;
	
	log_message(LOGLVL_DEBUG, "peeruser '%s' '%s'", handle, protocol);
	
	for(u=irc->users; u; u=u->next) {
		struct prpl *prpl;
		if(!u->ic || !u->handle)
			continue;
		prpl = u->ic->acc->prpl;
		if(strcmp(prpl->name, protocol) == 0
			&& prpl->handle_cmp(u->handle, handle) == 0) {
			return u;
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
	
	user_t *u = peeruser(irc, handle, protocol);
	if(u) {
		return u->nick;
	} else {
		g_snprintf(fallback, 511, "%s/%s", handle, protocol);
		return fallback;
	}
}

int otr_update_modeflags(irc_t *irc, user_t *u)
{
	char *vb = set_getstr(&irc->set, "voice_buddies");
	char *hb = set_getstr(&irc->set, "halfop_buddies");
	char *ob = set_getstr(&irc->set, "op_buddies");
	int encrypted = u->encrypted;
	int trusted = u->encrypted > 1;
	char flags[7];
	int nflags;
	char *p = flags;
	char *from;
	int i;
	
	if(!strcmp(vb, "encrypted")) {
		*(p++) = encrypted ? '+' : '-';
		*(p++) = 'v';
		nflags++;
	} else if(!strcmp(vb, "trusted")) {
		*(p++) = trusted ? '+' : '-';
		*(p++) = 'v';
		nflags++;
	}
	if(!strcmp(hb, "encrypted")) {
		*(p++) = encrypted ? '+' : '-';
		*(p++) = 'h';
		nflags++;
	} else if(!strcmp(hb, "trusted")) {
		*(p++) = trusted ? '+' : '-';
		*(p++) = 'h';
		nflags++;
	}
	if(!strcmp(ob, "encrypted")) {
		*(p++) = encrypted ? '+' : '-';
		*(p++) = 'o';
		nflags++;
	} else if(!strcmp(ob, "trusted")) {
		*(p++) = trusted ? '+' : '-';
		*(p++) = 'o';
		nflags++;
	}
	*p = '\0';
	
	p = g_malloc(nflags * (strlen(u->nick)+1) + 1);
	*p = '\0';
	if(!p)
		return 0;
	for(i=0; i<nflags; i++) {
		strcat(p, " ");
		strcat(p, u->nick);
	}
	if(set_getbool(&irc->set, "simulate_netsplit"))
		from = g_strdup(irc->myhost);
	else
		from = g_strdup_printf("%s!%s@%s", irc->mynick, irc->mynick, irc->myhost);
	irc_write(irc, ":%s MODE %s %s%s", from, irc->channel, flags, p);
	g_free(from);
	g_free(p);
		
	return 1;
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
			irc_usermsg(irc, "  \x02%s (%s)\x02", human, trust);
		} else {
			irc_usermsg(irc, "  %s (%s)", human, trust);
		}
	}
	if(count==0)
		irc_usermsg(irc, "  no fingerprints");
}

void show_general_otr_info(irc_t *irc)
{
	ConnContext *ctx;
	OtrlPrivKey *key;
	char human[45];

	/* list all privkeys */
	irc_usermsg(irc, "\x1fprivate keys:\x1f");
	for(key=irc->otr_us->privkey_root; key; key=key->next) {
		const char *hash;
		
		switch(key->pubkey_type) {
		case OTRL_PUBKEY_TYPE_DSA:
			irc_usermsg(irc, "  %s/%s - DSA", key->accountname, key->protocol);
			break;
		default:
			irc_usermsg(irc, "  %s/%s - type %d", key->accountname, key->protocol,
				key->pubkey_type);
		}

		/* No, it doesn't make much sense to search for the privkey again by
		   account/protocol, but libotr currently doesn't provide a direct routine
		   for hashing a given 'OtrlPrivKey'... */
		hash = otrl_privkey_fingerprint(irc->otr_us, human, key->accountname, key->protocol);
		if(hash) /* should always succeed */
			irc_usermsg(irc, "    %s", human);
	}

	/* list all contexts */
	irc_usermsg(irc, "%s", "");
	irc_usermsg(irc, "\x1f" "connection contexts:\x1f (bold=currently encrypted)");
	for(ctx=irc->otr_us->context_root; ctx; ctx=ctx->next) {\
		user_t *u;
		char *userstring;
		
		u = peeruser(irc, ctx->username, ctx->protocol);
		if(u)
			userstring = g_strdup_printf("%s/%s/%s (%s)",
				ctx->username, ctx->protocol, ctx->accountname, u->nick);
		else
			userstring = g_strdup_printf("%s/%s/%s",
				ctx->username, ctx->protocol, ctx->accountname);
		
		if(ctx->msgstate == OTRL_MSGSTATE_ENCRYPTED) {
			otrl_privkey_hash_to_human(human, ctx->active_fingerprint->fingerprint);
			irc_usermsg(irc, "  \x02%s\x02", userstring);
			irc_usermsg(irc, "    %s", human);
		} else {
			irc_usermsg(irc, "  %s", userstring);
		}
		
		g_free(userstring);
	}
}

void show_otr_context_info(irc_t *irc, ConnContext *ctx)
{
	switch(ctx->otr_offer) {
	case OFFER_NOT:
		irc_usermsg(irc, "  otr offer status: none sent");
		break;
	case OFFER_SENT:
		irc_usermsg(irc, "  otr offer status: awaiting reply");
		break;
	case OFFER_ACCEPTED:
		irc_usermsg(irc, "  otr offer status: accepted our offer");
		break;
	case OFFER_REJECTED:
		irc_usermsg(irc, "  otr offer status: ignored our offer");
		break;
	default:
		irc_usermsg(irc, "  otr offer status: %d", ctx->otr_offer);
	}

	switch(ctx->msgstate) {
	case OTRL_MSGSTATE_PLAINTEXT:
		irc_usermsg(irc, "  connection state: cleartext");
		break;
	case OTRL_MSGSTATE_ENCRYPTED:
		irc_usermsg(irc, "  connection state: encrypted (v%d)", ctx->protocol_version);
		break;
	case OTRL_MSGSTATE_FINISHED:
		irc_usermsg(irc, "  connection state: shut down");
		break;
	default:
		irc_usermsg(irc, "  connection state: %d", ctx->msgstate);
	}

    irc_usermsg(irc, "  known fingerprints: (bold=active)");	
	show_fingerprints(irc, ctx);
}

void otr_keygen(irc_t *irc, const char *handle, const char *protocol)
{
	char *account_off[] = {"account", "off", NULL};
	GError *err;
	GThread *thr;
	struct kgdata *kg;
	gint ev;
	
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

	/* tell the user what's happening, go comatose, and start the keygen */
	irc_usermsg(irc, "going comatose for otr key generation, this will take a moment");
	irc_usermsg(irc, "all accounts logging out, user commands disabled");
	cmd_account(irc, account_off);
	irc_usermsg(irc, "generating new otr privkey for %s/%s...",
		handle, protocol);
	
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
	
	irc_usermsg(acc->irc, "keygen cancelled for %s/%s",
		acc->user, acc->prpl->name);
}


#else /* WITH_OTR undefined */

void cmd_otr(irc_t *irc, char **args)
{
	irc_usermsg(irc, "otr: n/a, compiled without OTR support");
}

#endif
