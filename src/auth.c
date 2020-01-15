/* Copyright (c) 2018 Evan Wyatt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation 
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <auth.h>
#include <common.h>
#include <logging.h>

/* Generate a nonce of size 'size' and return an allocated buffer containing
 * the hex encoded representation */

char * generateNonce(int size) {
	unsigned char nonce[size];
	int bytes = 0;

	int fd = open("/dev/urandom", O_RDONLY);

	if (fd < 0) {
		print_msg(JERS_LOG_WARNING, "Failed to open /dev/urandom: %s", strerror(errno));
		return NULL;
	}

	while (bytes < size) {
		int r = read(fd, nonce + bytes, size - bytes);

		if (r == -1) {
			print_msg(JERS_LOG_WARNING, "Failed to read /dev/urandom: %s\n", strerror(errno));
			close(fd);
			return NULL;
		}

		bytes += r;
	}

	close(fd);

	return hexEncode(nonce, size, NULL);
}

/* Generate a HMAC on the NULL terminated input char * array, using the provided key */

#if OPENSSL_VERSION_NUMBER < 0x10100000L
char * generateHMAC(const char ** input, const unsigned char *key, int key_len) {
	unsigned char hmac_result[EVP_MAX_MD_SIZE];
	unsigned int result_size = 0;
	HMAC_CTX ctx;

	HMAC_CTX_init(&ctx);

	if (HMAC_Init_ex(&ctx, key, key_len, EVP_sha256(), NULL) != 1) {
		print_msg(JERS_LOG_WARNING, "HMAC_Init_ex() failed\n");
		return NULL;
	}

	for (int i = 0; input[i]; i++) {
		if (HMAC_Update(&ctx, (const unsigned char *)input[i], strlen(input[i])) != 1) {
			print_msg(JERS_LOG_WARNING, "HMAC_Update() failed\n");
			HMAC_CTX_cleanup(&ctx);
			return NULL;
		}
	}

	if (HMAC_Final(&ctx, hmac_result, &result_size) != 1) {
		print_msg(JERS_LOG_WARNING, "HMAC_Final() failed\n");
		HMAC_CTX_cleanup(&ctx);
		return NULL;
	}

	HMAC_CTX_cleanup(&ctx);

	return hexEncode(hmac_result, result_size, NULL);
}
#else
char * generateHMAC(const char ** input, const unsigned char *key, int key_len) {
	unsigned char hmac_result[EVP_MAX_MD_SIZE];
	unsigned int result_size = 0;
	HMAC_CTX *ctx;

	ctx = HMAC_CTX_new();
	if (ctx == NULL) {
		print_msg(JERS_LOG_WARNING, "HMAC_CTX_new() failed\n");
		return NULL;
	}

	if (HMAC_Init_ex(ctx, key, key_len, EVP_sha256(), NULL) != 1) {
		print_msg(JERS_LOG_WARNING, "HMAC_Init_ex() failed\n");
		return NULL;
	}

	for (int i = 0; input[i]; i++) {
		if (HMAC_Update(ctx, (const unsigned char *)input[i], strlen(input[i])) != 1) {
			print_msg(JERS_LOG_WARNING, "HMAC_Update() failed\n");
			HMAC_CTX_free(ctx);
			return NULL;
		}
	}

	if (HMAC_Final(ctx, hmac_result, &result_size) != 1) {
		print_msg(JERS_LOG_WARNING, "HMAC_Final() failed\n");
		HMAC_CTX_free(ctx);
		return NULL;
	}

	HMAC_CTX_free(ctx);

	return hexEncode(hmac_result, result_size, NULL);
}
#endif

/* Load the secret from 'secret_filename' storing the hash of the file in 'hash'.
 * 'hash' is assummed to be large enough to store a SHA256 hash (SHA256_DIGEST_LENGTH) */

int loadSecret(const char * secret_filename, unsigned char *hash) {
	unsigned char *secret = NULL;
	struct stat buff;
	int bytes = 0;

	int fd = open(secret_filename, O_RDONLY);

	if (fd < 0) {
		print_msg(JERS_LOG_WARNING, "Failed to open secret %s: %s", secret_filename, strerror(errno));
		goto failed;
	}

	if (fstat(fd, &buff) != 0) {
		print_msg(JERS_LOG_WARNING, "Failed to stat() secret file %s: %s", secret_filename, strerror(errno));
		goto failed;
	}

	/* The secret file should only be readable by the user running the JERS Daemon */
	if (getuid() != 0 && (getuid() != buff.st_uid || getgid() != buff.st_gid)) {
		print_msg(JERS_LOG_WARNING, "Ownership of secret file is not uid:%d", getuid());
		goto failed;
	}

	/* Check the permissions */
	if (getuid() != 0 && (buff.st_mode &(S_IRWXO|S_IRWXG)) != 0) {
		print_msg(JERS_LOG_WARNING, "Permissions on the secret file allows group/others access! - Only owner should have access ie. 400");
		goto failed;
	}

	if (buff.st_size <= 0) {
		print_msg(JERS_LOG_WARNING, "Secret file %s is empty", secret_filename);
	}

	secret = malloc(buff.st_size);

	if (secret == NULL) {
		print_msg(JERS_LOG_WARNING, "Failed to allocate memory for secret: %s", strerror(errno));
		goto failed;
	}

	while (bytes < buff.st_size) {
		int l = read(fd, secret + bytes, buff.st_size - bytes);

		if (l <= 0) {
			print_msg(JERS_LOG_WARNING, "Failed to read secret file %s: %s", secret_filename, strerror(errno));
			goto failed;
		}

		bytes += l;
	}

	/* Hash the secret */
	if (SHA256(secret, buff.st_size, hash) == NULL) {
		print_msg(JERS_LOG_WARNING, "Failed to generate SHA256 hash of secret: %s", strerror(errno));
		goto failed;
	}

	close(fd);
	free(secret);

	return 0;

failed:
	if (fd >= 0)
		close(fd);
	free(secret);

	return 1;;
}