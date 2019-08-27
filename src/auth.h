#include <openssl/sha.h>
#include <openssl/hmac.h>

#define NONCE_SIZE 16 // Bytes

#define SECRET_HASH_SIZE SHA256_DIGEST_LENGTH
#define MAX_AUTH_TIME 300 // Seconds

char * generateNonce(int size);
char * generateHMAC(const char **input, const unsigned char *key, int key_len);
int loadSecret(const char * secret_filename, unsigned char *hash);