#ifndef BPURPLE_H
# define BPURPLE_H

#include <purple.h>
#include <glib.h>

#define PURPLE_REQUEST_HANDLE "purple_request"

struct purple_data
{
    PurpleAccount *account;

    GHashTable *input_requests;
    unsigned int next_request_id;
};

#endif /* !BPURPLE_H */
