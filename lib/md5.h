#ifndef _MD5_H
#define _MD5_H

#include <glib.h>
#include <gmodule.h>

typedef guint8 md5_byte_t;
typedef GChecksum *md5_state_t;


#define MD5_HASH_SIZE 16

void md5_init(md5_state_t *);
void md5_append(md5_state_t *, const guint8 *, unsigned int);
void md5_finish(md5_state_t *, guint8 digest[MD5_HASH_SIZE]);
void md5_digest_keep(md5_state_t *, guint8 digest[MD5_HASH_SIZE]);
void md5_free(md5_state_t *);

#endif
