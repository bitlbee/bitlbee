#ifndef _MD5_H
#define _MD5_H

#include <glib.h>
#include <gmodule.h>

typedef guint8 md5_byte_t;
typedef GChecksum *md5_state_t;

#define MD5_HASH_SIZE 16

#ifdef __GNUC__
#define __MD5_NON_PUBLIC_DEPRECATION__ __attribute__((deprecated("md5.h will be removed from Bitlbee's public API. Please use another library (such as GLib's gchecksum) instead")))
#else
#define __MD5_NON_PUBLIC_DEPRECATION__
#endif

void md5_init(md5_state_t *) __MD5_NON_PUBLIC_DEPRECATION__;
void md5_append(md5_state_t *, const guint8 *, unsigned int) __MD5_NON_PUBLIC_DEPRECATION__;
void md5_finish(md5_state_t *, guint8 digest[MD5_HASH_SIZE]) __MD5_NON_PUBLIC_DEPRECATION__;
void md5_digest_keep(md5_state_t *, guint8 digest[MD5_HASH_SIZE]) __MD5_NON_PUBLIC_DEPRECATION__;
void md5_free(md5_state_t *) __MD5_NON_PUBLIC_DEPRECATION__;

#endif
