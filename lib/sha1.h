
#ifndef _SHA1_H_
#define _SHA1_H_

#include <glib.h>
#include <gmodule.h>

#ifdef __GNUC__
#define __SHA1_NON_PUBLIC_DEPRECATION__ __attribute__((deprecated("sha1.h will be removed from Bitlbee's public API. Please use another library (such as GLib's gchecksum) instead")))
#else
#define __SHA1_NON_PUBLIC_DEPRECATION__
#endif

#define SHA1_HASH_SIZE 20

typedef GChecksum *sha1_state_t;

void sha1_init(sha1_state_t *) __SHA1_NON_PUBLIC_DEPRECATION__;
void sha1_append(sha1_state_t *, const guint8 *, unsigned int) __SHA1_NON_PUBLIC_DEPRECATION__;
void sha1_finish(sha1_state_t *, guint8 digest[SHA1_HASH_SIZE]) __SHA1_NON_PUBLIC_DEPRECATION__;
void sha1_hmac(const char *, size_t, const char *, size_t, guint8 digest[SHA1_HASH_SIZE]) ;
char *sha1_random_uuid(sha1_state_t *);

#endif
