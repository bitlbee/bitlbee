#include "md5.h"

/* Creates a new GChecksum in ctx */
void md5_init(md5_state_t *ctx)
{
	*ctx = g_checksum_new(G_CHECKSUM_MD5);
}

/* Wrapper for g_checksum_update */
void md5_append(md5_state_t *ctx, const guint8 *buf, unsigned int len)
{
	g_checksum_update(*ctx, buf, len);
}

/* Wrapper for g_checksum_get_digest
 * Also takes care of g_checksum_free(), since it can't be reused anyway
 * (the GChecksum is closed after get_digest) */
void md5_finish(md5_state_t *ctx, guint8 digest[MD5_HASH_SIZE])
{
	gsize digest_len = MD5_HASH_SIZE;

	g_checksum_get_digest(*ctx, digest, &digest_len);
	g_checksum_free(*ctx);
}

/* Variant of md5_finish that copies the GChecksum
 * and finishes that one instead of the original */
void md5_digest_keep(md5_state_t *ctx, guint8 digest[MD5_HASH_SIZE])
{
	md5_state_t copy = g_checksum_copy(*ctx);

	md5_finish(&copy, digest);
}

void md5_free(md5_state_t *ctx)
{
	g_checksum_free(*ctx);
}
