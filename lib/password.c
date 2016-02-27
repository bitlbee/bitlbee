#define BITLBEE_CORE
#include "bitlbee.h"
#include "base64.h"
#include "arc.h"
#include "md5.h"

size_t password_hash(const char *password, char **hash)
{
	md5_byte_t pass_md5[21];
	md5_state_t md5_state;

	/* Generate a salted md5sum of the password. Use 5 bytes for the salt
	   (to prevent dictionary lookups of passwords) to end up with a 21-
	   byte password hash, more convenient for base64 encoding. */
	random_bytes(pass_md5 + 16, 5);
	md5_init(&md5_state);
	md5_append(&md5_state, (md5_byte_t *) password, strlen(password));
	md5_append(&md5_state, pass_md5 + 16, 5);   /* Add the salt. */
	md5_finish(&md5_state, pass_md5);

	/* Save the hash in base64-encoded form. */
	*hash = base64_encode(pass_md5, 21);
	return strlen(*hash);
}

/* Returns values: -1 == Failure (base64-decoded to something unexpected)
                    0 == Okay
                    1 == Password doesn't match the hash. */
int password_verify(const char *password, const char *hash)
{
	md5_byte_t *pass_dec = NULL;
	md5_byte_t pass_md5[16];
	md5_state_t md5_state;
	int ret = -1, i;

	if (base64_decode(hash, &pass_dec) == 21) {
		md5_init(&md5_state);
		md5_append(&md5_state, (md5_byte_t *) password, strlen(password));
		md5_append(&md5_state, (md5_byte_t *) pass_dec + 16, 5);  /* Hmmm, salt! */
		md5_finish(&md5_state, pass_md5);

		for (i = 0; i < 16; i++) {
			if (pass_dec[i] != pass_md5[i]) {
				ret = 1;
				break;
			}
		}

		/* If we reached the end of the loop, it was a match! */
		if (i == 16) {
			ret = 0;
		}
	}

	g_free(pass_dec);

	return ret;
}

size_t password_encode(const char *password, char **encoded)
{
	*encoded = base64_encode((unsigned char *)password, strlen(password));
	return *encoded ? -1 : strlen(*encoded);
}

size_t password_decode(const char *encoded, char **password)
{
	return base64_decode(encoded, (unsigned char **)password);
}

size_t password_encrypt(const char *password, const char *encryption_key, char **crypt)
{
	unsigned char *encrypted;
	size_t pass_len = arc_encode((char *)password, strlen(password), &encrypted, (char *)encryption_key, 12);
	*crypt = base64_encode(encrypted, pass_len);
	g_free(encrypted);
	return strlen(*crypt);
}

size_t password_decrypt(const char *encrypted, const char *encryption_key, char **password)
{
	unsigned char *decoded;
	size_t pass_len = base64_decode(encrypted, &decoded);
	if (pass_len) {
		pass_len = arc_decode(decoded, pass_len, password, encryption_key);
		g_free(decoded);
	}
	return pass_len;
}
