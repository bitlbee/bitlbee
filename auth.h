#ifndef __BITLBEE_AUTH_H__
#define __BITLBEE_AUTH_H__

#include "storage.h"

typedef struct {
	const char *name;
	storage_status_t (*check_pass)(const char *nick, const char *password);
} auth_backend_t;

GList *auth_init(const char *backend);
storage_status_t auth_check_pass(const char *backend, const char *nick, const char *password);
#endif
