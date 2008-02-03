#ifndef BITLBEE_PROTOCOLS_OTR_H
#define BITLBEE_PROTOCOLS_OTR_H

#include "bitlbee.h"


// forward decls to avoid mutual dependencies
struct irc;
struct im_connection;
struct account;

// 'otr' root command, hooked up in root_commands.c
void cmd_otr(struct irc *, char **args);


#ifdef WITH_OTR
#include <libotr/proto.h>
#include <libotr/message.h>
#include <libotr/privkey.h>

/* called from main() */
void otr_init(void);

/* called by storage_* functions */
void otr_load(struct irc *irc);
void otr_save(struct irc *irc);
void otr_remove(const char *nick);
void otr_rename(const char *onick, const char *nnick);

/* called from account_add() */
void otr_check_for_key(struct account *a);

/* called from imcb_buddy_msg() */
char *otr_handle_message(struct im_connection *ic, const char *handle,
	const char *msg);
	
/* called from imc_buddy_msg() */
int otr_send_message(struct im_connection *ic, const char *handle, const char *msg,
	int flags);

#else

typedef void *OtrlUserState;
typedef void *OtrlMessageAppOps;

#define otrl_userstate_create() (NULL)
#define otrl_userstate_free(us) {}

#define otr_init() {}
#define otr_load(irc) {}
#define otr_save(irc) {}
#define otr_remove(nick) {}
#define otr_rename(onick,nnick) {}
#define otr_check_for_key(acc) {}
#define otr_handle_msg(ic,handle,msg) (g_strdup(msg))
#define otr_send_message(ic,h,m,f) (ic->acc->prpl->buddy_msg(ic,h,m,f))

void cmd_otr_nosupport(void *, char **);

#endif
#endif
