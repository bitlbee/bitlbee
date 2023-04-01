#include "sha1.h"
#include <string.h>
#include <stdio.h>


void sha1_init(sha1_state_t *ctx)
{
	*ctx = g_checksum_new(G_CHECKSUM_SHA1);
}

void sha1_append(sha1_state_t *ctx, const guint8 * message_array, guint len)
{
	g_checksum_update(*ctx, message_array, len);
}

void sha1_finish(sha1_state_t *ctx, guint8 digest[SHA1_HASH_SIZE])
{
	gsize digest_len = SHA1_HASH_SIZE;

	g_checksum_get_digest(*ctx, digest, &digest_len);
	g_checksum_free(*ctx);
}

#define HMAC_BLOCK_SIZE 64

void b_hmac(GChecksumType checksum_type, const char *key_, size_t key_len,
            const char *payload, size_t payload_len, guint8 **digest)
{
	GChecksum *checksum;
	size_t hash_len;
	guint8 *hash;
	guint8 key[HMAC_BLOCK_SIZE + 1];
	int i;

	hash_len = g_checksum_type_get_length(checksum_type);

	if (hash_len == (size_t) -1) {
		return;
	}

	hash = g_malloc(hash_len);

	if (key_len == 0) {
		key_len = strlen(key_);
	}
	if (payload_len == 0) {
		payload_len = strlen(payload);
	}

	/* Create K. If our current key is >64 chars we have to hash it,
	   otherwise just pad. */
	memset(key, 0, HMAC_BLOCK_SIZE + 1);
	if (key_len > HMAC_BLOCK_SIZE) {
		checksum = g_checksum_new(checksum_type);
		g_checksum_update(checksum, (guint8 *) key_, key_len);
		g_checksum_get_digest(checksum, key, &hash_len);
		g_checksum_free(checksum);
	} else {
		memcpy(key, key_, key_len);
	}

	/* Inner part: H(K XOR 0x36, text) */
	checksum = g_checksum_new(checksum_type);
	for (i = 0; i < HMAC_BLOCK_SIZE; i++) {
		key[i] ^= 0x36;
	}
	g_checksum_update(checksum, key, HMAC_BLOCK_SIZE);
	g_checksum_update(checksum, (const guint8 *) payload, payload_len);
	g_checksum_get_digest(checksum, hash, &hash_len);
	g_checksum_free(checksum);

	/* Final result: H(K XOR 0x5C, inner stuff) */
	checksum = g_checksum_new(checksum_type);
	for (i = 0; i < HMAC_BLOCK_SIZE; i++) {
		key[i] ^= 0x36 ^ 0x5c;
	}
	g_checksum_update(checksum, key, HMAC_BLOCK_SIZE);
	g_checksum_update(checksum, hash, hash_len);
	g_checksum_get_digest(checksum, *digest, &hash_len);
	g_checksum_free(checksum);

	g_free(hash);
}

void sha1_hmac(const char *key_, size_t key_len, const char *payload, size_t payload_len, guint8 digest[SHA1_HASH_SIZE])
{
	b_hmac(G_CHECKSUM_SHA1, key_, key_len, payload, payload_len, &digest);
}


/* I think this follows the scheme described on:
   http://en.wikipedia.org/wiki/Universally_unique_identifier#Version_4_.28random.29
   My random data comes from a SHA1 generator but hey, it's random enough for
   me, and RFC 4122 looks way more complicated than I need this to be.

   Returns a value that must be free()d. */
char *sha1_random_uuid(sha1_state_t * context)
{
	guint8 dig[SHA1_HASH_SIZE];
	char *ret = g_new0(char, 40);   /* 36 chars + \0 */
	int i, p;
	gsize digest_len = SHA1_HASH_SIZE;

	g_checksum_get_digest(*context, dig, &digest_len);
	g_checksum_free(*context);

	for (p = i = 0; i < 16; i++) {
		if (i == 4 || i == 6 || i == 8 || i == 10) {
			ret[p++] = '-';
		}
		if (i == 6) {
			dig[i] = (dig[i] & 0x0f) | 0x40;
		}
		if (i == 8) {
			dig[i] = (dig[i] & 0x30) | 0x80;
		}

		sprintf(ret + p, "%02x", dig[i]);
		p += 2;
	}
	ret[p] = '\0';

	return ret;
}
