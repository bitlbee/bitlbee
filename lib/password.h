#ifndef _PASSWORD_H
#define _PASSWORD_H

size_t password_hash(const char *password, char **hash);
int password_verify(const char *password, const char *hash);
size_t password_encode(const char *password, char **encoded);
size_t password_decode(const char *encoded, char **password);
size_t password_encrypt(const char *password, const char *encryption_key, char **crypt);
size_t password_decrypt(const char *encrypted, const char *encryption_key, char **password);

#endif
