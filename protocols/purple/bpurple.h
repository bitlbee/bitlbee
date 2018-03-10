#ifndef BPURPLE_H
# define BPURPLE_H

#include <purple.h>
#include <glib.h>

#define PURPLE_REQUEST_HANDLE "purple_request"

#define PURPLE_OPT_SHOULD_SET_NICK 1

struct purple_data
{
    PurpleAccount *account;

    GHashTable *input_requests;
    guint next_request_id;
    char *chat_list_server;
    GSList *filetransfers;

    int flags;
};

#endif /* !BPURPLE_H */
