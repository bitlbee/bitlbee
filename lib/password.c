#define BITLBEE_CORE

#include <gcrypt.h>
#include "bitlbee.h"
#include "base64.h"
#include "arc.h"
#include "md5.h"


/* pbkdf2-hashed password. We store an algoritm identifier ("1"), the salt
 * used, the number of rounds and the final hash */
size_t password_hash(const char *password, char **hash)
{
	unsigned char rawhash[64], salt[8];
	char *b64hash, *b64salt;
	int saltlen = 8, rounds = 10000, keylen = 64;

	random_bytes(salt, saltlen);
	gcry_kdf_derive(password, strlen(password), GCRY_KDF_PBKDF2, GCRY_MD_SHA1,
			salt, saltlen, rounds, keylen, rawhash);
	b64salt = base64_encode(salt, saltlen);
	b64hash = base64_encode(rawhash, keylen);

	*hash = g_strdup_printf("$1$%s$%d$%s", b64salt, rounds, b64hash);
	g_free(b64salt);
	g_free(b64hash);
	return strlen(*hash);
}

/* Returns values: -1 == Failure (base64-decoded to something unexpected)
                    0 == Okay
                    1 == Password doesn't match the hash. */
static int password_verify_old(const char *password, const char *hash)
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

int password_verify(const char *password, const char *hash) {
	int ret;
	int rounds = -1;
	unsigned char *salt = NULL, *oldhash = NULL, *newhash = NULL;
	int saltlen = -1, hashlen = -1;

	if (hash[0] != '$')
		return password_verify_old(password, hash);

	char **tokens = g_strsplit(hash+1, "$", 4);
	if(!*tokens[0] || !*tokens[1] || !*tokens[2] || !*tokens[3]) {
		g_strfreev(tokens);
		return -1;
	}

	/* First token is algorithm, only "1" is supported for now */
	if(strcmp(tokens[0], "1") != 0) {
		g_strfreev(tokens);
		return -1;
	}

	/* Next come the hash parameters */
	saltlen = base64_decode(tokens[1], &salt);
	rounds = g_ascii_strtoll(tokens[2], NULL, 10);
	hashlen = base64_decode(tokens[3], &oldhash);
	g_strfreev(tokens);

	if (saltlen <= 0 || rounds <= 0 || hashlen <= 0)
		return -1;

	/* And now we calculate and verify */
	newhash = g_new0(unsigned char, hashlen);
	gcry_kdf_derive(password, strlen(password), GCRY_KDF_PBKDF2, GCRY_MD_SHA1,
			salt, saltlen, rounds, hashlen, newhash);

	ret = memcmp(oldhash, newhash, hashlen) == 0 ? 0 : 1;
	g_free(newhash);
	return ret;
}

/* When using authentication backends, passwords are stored in plain text. We
 * encode/decode them with base64 to allow "weird" characters in passwords */
size_t password_encode(const char *password, char **encoded)
{
	*encoded = base64_encode((unsigned char *)password, strlen(password));
	return *encoded ? -1 : strlen(*encoded);
}

size_t password_decode(const char *encoded, char **password)
{
	return base64_decode(encoded, (unsigned char **)password);
}

/* Encrypt/decrypt data using AES-GCM with a random IV and "bitlbee" as auth
   data. The key is not used directly, but first turned into a 32-bit key using
   pbkdf2 as kdf and the same IV as we use for encryption. Stores the IV,
   encrypted data and tag in the last argument and returns the length or a
   negative number on error. */
size_t password_encrypt(const char *password, const char *encryption_key, char **crypt)
{
#if GCRYPT_VERSION_NUMBER < 0x010600
	/* On systems that don't have a modern enough gcrypt, fall back to our old scheme */
	unsigned char *encrypted;
	size_t pass_len = arc_encode((char *)password, strlen(password), &encrypted, (char *)encryption_key, 12);
	*crypt = base64_encode(encrypted, pass_len);
	g_free(encrypted);
	return strlen(*crypt);
#else
	size_t pwlen = strlen(password);
	unsigned char *iv, *key, *tag, *enc, dkey[32];
	gcry_error_t st;
	gcry_cipher_hd_t gcr;

	enc = g_new(unsigned char, pwlen + 32);
	iv  = enc;
	key = enc + 16;
	tag = enc + 16 + pwlen;

	random_bytes(iv, 16);

	gcry_kdf_derive(encryption_key, strlen(encryption_key), GCRY_KDF_PBKDF2, GCRY_MD_SHA1,
			iv, 16, 10000, 32, dkey);

	if ((st = gcry_cipher_open(&gcr, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_GCM, 0)) != 0) {
		fprintf(stderr, "gcry_cipher_open failed: %s/%s\n", gcry_strsource(st), gcry_strerror(st));
	} else if ((st = gcry_cipher_setkey(gcr, dkey, 32)) != 0) {
		fprintf(stderr, "gcry_cipher_setkey failed: %s/%s\n", gcry_strsource(st), gcry_strerror(st));
	} else if ((st = gcry_cipher_setiv(gcr, iv, 16)) != 0) {
		fprintf(stderr, "gcry_cipher_setiv failed: %s/%s\n", gcry_strsource(st), gcry_strerror(st));
	} else if ((st = gcry_cipher_authenticate(gcr, (const void *)"bitlbee", 7)) != 0) {
		fprintf(stderr, "gcry_cipher_authenticate failed: %s/%s\n", gcry_strsource(st), gcry_strerror(st));
	} else if ((st = gcry_cipher_encrypt(gcr, key, pwlen, password, pwlen)) != 0) {
		fprintf(stderr, "gcry_cipher_encrypt failed: %s/%s\n", gcry_strsource(st), gcry_strerror(st));
	} else if ((st = gcry_cipher_gettag(gcr, tag, 16)) != 0) {
		fprintf(stderr, "gcry_cipher_gettag failed: %s/%s\n", gcry_strsource(st), gcry_strerror(st));
	}

	gcry_cipher_close(gcr);

	if (st != 0) {
		g_free(enc);
		return 0;
	}

	*crypt = base64_encode(enc, pwlen + 32);
	g_free(enc);
	return strlen(*crypt);
#endif
}

int password_decrypt(const char *encrypted, const char *encryption_key, char **password)
{
#if GCRYPT_VERSION_NUMBER < 0x010600
	/* On systems that don't have a modern enough gcrypt, fall back to our old scheme */
	unsigned char *decoded;
	size_t pass_len = base64_decode(encrypted, &decoded);
	if (pass_len) {
		pass_len = arc_decode(decoded, pass_len, password, encryption_key);
		g_free(decoded);
	}
	return pass_len;

#else
	unsigned char *iv, *key, *tag, *dec, dkey[32];
	gcry_error_t st;
	gcry_cipher_hd_t gcr;

	size_t pwlen = base64_decode(encrypted, &dec);

	/* An aes-encrypted password is at least 32 bytes long: the nonce and tag are 16 bytes each */
	if (pwlen < 32) {
		pwlen = arc_decode(dec, pwlen, password, encryption_key);
		g_free(dec);
		return pwlen;
	}

	pwlen -= 32;
	iv = dec;
	key = dec + 16;
	tag = dec + 16 + pwlen;
	*password = g_new0(char, pwlen+1);

	gcry_kdf_derive(encryption_key, strlen(encryption_key), GCRY_KDF_PBKDF2, GCRY_MD_SHA1,
			iv, 16, 10000, 32, dkey);

	if ((st = gcry_cipher_open(&gcr, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_GCM, 0)) != 0) {
		fprintf(stderr, "gcry_cipher_open failed: %s/%s\n", gcry_strsource(st), gcry_strerror(st));
	} else if ((st = gcry_cipher_setkey(gcr, dkey, 32)) != 0) {
		fprintf(stderr, "gcry_cipher_setkey failed: %s/%s\n", gcry_strsource(st), gcry_strerror(st));
	} else if ((st = gcry_cipher_setiv(gcr, iv, 16)) != 0) {
		fprintf(stderr, "gcry_cipher_setiv failed: %s/%s\n", gcry_strsource(st), gcry_strerror(st));
	} else if ((st = gcry_cipher_authenticate(gcr, (const void *)"bitlbee", 7)) != 0) {
		fprintf(stderr, "gcry_cipher_authenticate failed: %s/%s\n", gcry_strsource(st), gcry_strerror(st));
	} else if ((st = gcry_cipher_decrypt(gcr, *password, pwlen, key, pwlen)) != 0) {
		fprintf(stderr, "gcry_cipher_decrypt failed: %s/%s\n", gcry_strsource(st), gcry_strerror(st));
	} else if ((st = gcry_cipher_checktag(gcr, tag, 16)) != 0) {
		fprintf(stderr, "gcry_cipher_checktag failed: %s/%s\n", gcry_strsource(st), gcry_strerror(st));
		/* Let's fall back to our old arc4 passwords */
		fprintf(stderr, "falling back to old password algorithm\n");
		st = 0;
		pwlen = arc_decode(dec, pwlen+32, password, encryption_key);
	}

	g_free(dec);
	gcry_cipher_close(gcr);

	if (st != 0) {
		g_free(*password);
		*password = NULL;
		return 0;
	}
	return strlen(*password);
#endif
}
