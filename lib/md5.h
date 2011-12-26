/*
 * MD5 hashing code copied from Lepton's crack <http://usuarios.lycos.es/reinob/>
 *
 * Adapted to be API-compatible with the previous (GPL-incompatible) code.
 */

/*
 * This code implements the MD5 message-digest algorithm.
 * The algorithm is due to Ron Rivest.  This code was
 * written by Colin Plumb in 1993, no copyright is claimed.
 * This code is in the public domain; do with it what you wish.
 *
 * Equivalent code is available from RSA Data Security, Inc.
 * This code has been tested against that, and is equivalent,
 * except that you don't need to include two pages of legalese
 * with every copy.
 *
 * To compute the message digest of a chunk of bytes, declare an
 * MD5Context structure, pass it to MD5Init, call MD5Update as
 * needed on buffers full of bytes, and then call MD5Final, which
 * will fill a supplied 16-byte array with the digest.
 */

#ifndef _MD5_H
#define _MD5_H

#include <sys/types.h>
#include <gmodule.h>
#if(__sun)
#include <inttypes.h>
#else
#include <stdint.h>
#endif

typedef uint8_t md5_byte_t;
typedef struct MD5Context {
	uint32_t buf[4];
	uint32_t bits[2];
	unsigned char in[64];
} md5_state_t;

G_MODULE_EXPORT void md5_init(struct MD5Context *context);
G_MODULE_EXPORT void md5_append(struct MD5Context *context, const md5_byte_t *buf, unsigned int len);
G_MODULE_EXPORT void md5_finish(struct MD5Context *context, md5_byte_t digest[16]);
G_MODULE_EXPORT void md5_finish_ascii(struct MD5Context *context, char *ascii);

#endif
