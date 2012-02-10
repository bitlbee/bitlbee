/*
 * SHA1 hashing code copied from Lepton's crack <http://usuarios.lycos.es/reinob/>
 *
 * Adapted to be API-compatible with the previous (GPL-incompatible) code.
 */

/*
 *  sha1.h
 *
 *  Description:
 *      This is the header file for code which implements the Secure
 *      Hashing Algorithm 1 as defined in FIPS PUB 180-1 published
 *      April 17, 1995.
 *
 *      Many of the variable names in this code, especially the
 *      single character names, were used because those were the names
 *      used in the publication.
 *
 *      Please read the file sha1.c for more information.
 *
 */

#ifndef _SHA1_H_
#define _SHA1_H_

#if(__sun)
#include <inttypes.h>
#else
#include <stdint.h>
#endif
#include <gmodule.h>

#ifndef _SHA_enum_
#define _SHA_enum_
enum {
	shaSuccess = 0,
	shaNull,		/* Null pointer parameter */
	shaInputTooLong,	/* input data too long */
	shaStateError		/* called Input after Result */
};
#endif
#define sha1_hash_size 20

/*
 *  This structure will hold context information for the SHA-1
 *  hashing operation
 */
typedef struct SHA1Context {
	uint32_t Intermediate_Hash[sha1_hash_size/4];	/* Message Digest   */

	uint32_t Length_Low;            /* Message length in bits           */
	uint32_t Length_High;           /* Message length in bits           */

	/* Index into message block array   */
	int_least16_t Message_Block_Index;
	uint8_t Message_Block[64];	/* 512-bit message blocks           */

	int Computed;                   /* Is the digest computed?          */
	int Corrupted;                  /* Is the message digest corrupted? */
} sha1_state_t;

/*
 *  Function Prototypes
 */

G_MODULE_EXPORT int sha1_init(sha1_state_t *);
G_MODULE_EXPORT int sha1_append(sha1_state_t *, const uint8_t *, unsigned int);
G_MODULE_EXPORT int sha1_finish(sha1_state_t *, uint8_t Message_Digest[sha1_hash_size]);
G_MODULE_EXPORT void sha1_hmac(const char *key_, size_t key_len, const char *payload, size_t payload_len, uint8_t Message_Digest[sha1_hash_size]);
G_MODULE_EXPORT char *sha1_random_uuid( sha1_state_t * context );

#endif
