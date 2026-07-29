/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2024, Rob Norris <robn@despairlabs.com>
 * Copyright (c) 2026, TrueNAS.
 */

/*
 * This is a userspace ZFS crypto backend based on OpenSSL. Everything is
 * spelled out in long form because the OpenSSL Crypto API is quite complicated
 * but also because this should be a good reference implementation for the ZFS
 * crypto backend API.
 */

#include <sys/zio_crypt.h>
#include <sys/backtrace.h>
#include <openssl/evp.h>
#include <openssl/err.h>

static void
_dump_ossl_errors(void)
{
	ERR_print_errors_fp(stderr);
	libspl_backtrace(STDERR_FILENO);
}

typedef enum {
	ZC_OPENSSL_CIPHER_AES_128_CCM,
	ZC_OPENSSL_CIPHER_AES_192_CCM,
	ZC_OPENSSL_CIPHER_AES_256_CCM,
	ZC_OPENSSL_CIPHER_AES_128_GCM,
	ZC_OPENSSL_CIPHER_AES_192_GCM,
	ZC_OPENSSL_CIPHER_AES_256_GCM,
	ZC_OPENSSL_CIPHER_MAX
} zc_openssl_cipher_t;

static const char *zc_openssl_cipher_names[] = {
	"AES-128-CCM",
	"AES-192-CCM",
	"AES-256-CCM",
	"AES-128-GCM",
	"AES-192-GCM",
	"AES-256-GCM",
};

static EVP_CIPHER *zc_openssl_cipher[ZC_OPENSSL_CIPHER_MAX] = {0};

static EVP_CIPHER *
zio_crypt_cipher_get(const zio_crypt_info_t *ci)
{
	zc_openssl_cipher_t alg;

	switch (ci->ci_crypt_type) {
	case ZC_TYPE_CCM:
		alg =
		    ci->ci_keylen == 16 ? ZC_OPENSSL_CIPHER_AES_128_CCM :
		    ci->ci_keylen == 24 ? ZC_OPENSSL_CIPHER_AES_192_CCM :
		    ci->ci_keylen == 32 ? ZC_OPENSSL_CIPHER_AES_256_CCM :
		    ZC_OPENSSL_CIPHER_MAX;
		break;
	case ZC_TYPE_GCM:
		alg =
		    ci->ci_keylen == 16 ? ZC_OPENSSL_CIPHER_AES_128_GCM :
		    ci->ci_keylen == 24 ? ZC_OPENSSL_CIPHER_AES_192_GCM :
		    ci->ci_keylen == 32 ? ZC_OPENSSL_CIPHER_AES_256_GCM :
		    ZC_OPENSSL_CIPHER_MAX;
		break;
	default:
		alg = ZC_OPENSSL_CIPHER_MAX;
	}

	VERIFY3P(alg, <, ZC_OPENSSL_CIPHER_MAX);

	EVP_CIPHER *cipher = zc_openssl_cipher[alg];
	if (cipher)
		return (cipher);

	cipher = EVP_CIPHER_fetch(NULL,
	    zc_openssl_cipher_names[alg], NULL);
	if (!cipher) {
		fprintf(stderr, "zio_crypt_cipher_get: couldn't fetch "
		    "algorithm: %s\n", zc_openssl_cipher_names[alg]);
		_dump_ossl_errors();
		return (NULL);
	}

	zc_openssl_cipher[alg] = cipher;
	return (cipher);
}

typedef struct {
	EVP_MAC *mp_mac;
	OSSL_PARAM mp_params[2];
} zc_openssl_mac_params_t;

static zc_openssl_mac_params_t zc_openssl_mac_params = {0};

static zc_openssl_mac_params_t *
zio_crypt_mac_get(void)
{
	if (zc_openssl_mac_params.mp_mac)
		return (&zc_openssl_mac_params);

	EVP_MAC *mac = EVP_MAC_fetch(NULL, "HMAC", NULL);
	if (!mac) {
		fprintf(stderr, "zio_crypt_mac_get: couldn't fetch "
		    "algorithm: HMAC\n");
		_dump_ossl_errors();
		return (NULL);
	}

	zc_openssl_mac_params.mp_mac = mac;

	zc_openssl_mac_params.mp_params[0] =
	    OSSL_PARAM_construct_utf8_string("digest", (char *)"SHA512", 0);
	zc_openssl_mac_params.mp_params[1] = OSSL_PARAM_construct_end();

	return (&zc_openssl_mac_params);
}

int
zio_crypt_key_open_os(zio_crypt_key_t *key, const zio_crypt_info_t *ci)
{
	(void) key; (void) ci;
	return (0);
}

void
zio_crypt_key_close_os(zio_crypt_key_t *key)
{
	(void) key;
}

int
zio_crypt_key_reopen_os(zio_crypt_key_t *key, const zio_crypt_info_t *ci)
{
	(void) key; (void) ci;
	return (0);
}

int
zio_crypt_uios_init_os(zfs_uio_t *u1, zfs_uio_t *u2, int iovcnt, int *idx)
{
	memset(u1, 0, sizeof (zfs_uio_t));
	memset(u2, 0, sizeof (zfs_uio_t));

	zfs_uio_iov(u1) = umem_zalloc(iovcnt * sizeof (iovec_t), KM_SLEEP);
	zfs_uio_iov(u2) = umem_zalloc(iovcnt * sizeof (iovec_t), KM_SLEEP);

	zfs_uio_iovcnt(u1) = zfs_uio_iovcnt(u2) = iovcnt;
	zfs_uio_segflg(u1) = zfs_uio_segflg(u2) = UIO_SYSSPACE;

	*idx = 0;

	return (0);
}

void
zio_crypt_uios_fini_os(zfs_uio_t *u1, zfs_uio_t *u2)
{
	ASSERT3U(zfs_uio_iovcnt(u1), ==, zfs_uio_iovcnt(u2));

	umem_free(zfs_uio_iov(u1), zfs_uio_iovcnt(u1) * sizeof (iovec_t));
	umem_free(zfs_uio_iov(u2), zfs_uio_iovcnt(u2) * sizeof (iovec_t));
}

static int
zio_encrypt_ccm_ossl(EVP_CIPHER *cipher, crypto_key_t *key,
    zfs_uio_t *plaintext, zfs_uio_t *ciphertext, size_t datalen,
    const uint8_t iv[ZIO_DATA_IV_LEN], const uint8_t *ad, size_t adlen,
    uint8_t mac[ZIO_DATA_MAC_LEN])
{
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if (!ctx)
		goto error;

	int len;

	/* Initialise encryption context with wanted cipher */
	if (EVP_EncryptInit_ex(ctx, cipher, NULL, NULL, NULL) != 1)
		goto error;

	/* Set IV and MAC length */
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_IVLEN,
	    ZIO_DATA_IV_LEN, NULL) != 1)
		goto error;
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_TAG,
	    ZIO_DATA_MAC_LEN, NULL) != 1)
		goto error;

	/*
	 * Update initialisation with key and IV. We can't do this at the
	 * start because the IV length has not been set yet.
	 */
	if (EVP_EncryptInit_ex(ctx, NULL, NULL, key->ck_data, iv) != 1)
		goto error;

	/* OpenSSL CCM requires that the data length be provided up front */
	if (EVP_EncryptUpdate(ctx, NULL, &len, NULL, datalen) != 1)
		goto error;

	/*
	 * If available, mix in the AAD. OpenSSL CCM will still mix the state
	 * even if the length is zero, so we skip it if we don't have any.
	 */
	if (adlen > 0 && EVP_EncryptUpdate(ctx, NULL, &len, ad, adlen) != 1)
		goto error;

	/*
	 * OpenSSL CCM crypt can be called once, and always needs a buffer,
	 * even if its empty. So, if we have more than one iovec, we need to
	 * copy the input iovecs into a single linear buffer, crypt that, then
	 * copy back to the output iovecs. Meanwhile, if we have no data at
	 * all, we just make a valid pointer to some arbitrary place on the
	 * stack.
	 */
	uint8_t *pbuf = NULL, *cbuf = NULL, zbuf[0];
	if (datalen == 0) {
		pbuf = zbuf;
		cbuf = zbuf;
	} else if (zfs_uio_iovcnt(plaintext) > 1) {
		pbuf = umem_alloc(datalen, UMEM_NOFAIL);
		cbuf = umem_alloc(datalen, UMEM_NOFAIL);

		len = 0;
		for (int i = 0; i < zfs_uio_iovcnt(plaintext); i++) {
			memcpy(&pbuf[len], zfs_uio_iovbase(plaintext, i),
			    zfs_uio_iovlen(plaintext, i));
			len += zfs_uio_iovlen(plaintext, i);
			ASSERT3U(len, <=, datalen);
		}
		ASSERT3U(len, ==, datalen);
	} else {
		pbuf = zfs_uio_iovbase(plaintext, 0);
		cbuf = zfs_uio_iovbase(ciphertext, 0);
		ASSERT3U(zfs_uio_iovlen(plaintext, 0), ==, datalen);
		ASSERT3U(zfs_uio_iovlen(ciphertext, 0), ==, datalen);
	}

	/* Run the crypt op */
	int rc = EVP_EncryptUpdate(ctx, cbuf, &len, pbuf, datalen);

	/* If success and we had multiple iovecs, copy it back */
	if (rc == 1 && zfs_uio_iovcnt(plaintext) > 1) {
		len = 0;
		for (int i = 0; i < zfs_uio_iovcnt(ciphertext); i++) {
			memcpy(zfs_uio_iovbase(ciphertext, i),
			    &cbuf[len], zfs_uio_iovlen(ciphertext, i));
			len += zfs_uio_iovlen(ciphertext, i);
			ASSERT3U(len, <=, datalen);
		}
		ASSERT3U(len, ==, datalen);
	}

	/* Zero and free work buffers */
	if (zfs_uio_iovcnt(plaintext) > 1) {
		/* XXX should be a secure zero -- robn, 2024-07-29 */
		memset(pbuf, 0, datalen);
		memset(cbuf, 0, datalen);
		umem_free(pbuf, datalen);
		umem_free(cbuf, datalen);
	}

	if (rc != 1)
		goto error;

	/* Finalise the encryption, which will compute the MAC internally */
	if (EVP_EncryptFinal_ex(ctx, NULL, &len) != 1)
		goto error;
	ASSERT0(len);

	/* Fill the MAC */
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_GET_TAG,
	    ZIO_DATA_MAC_LEN, mac) != 1)
		goto error;

	EVP_CIPHER_CTX_free(ctx);

	return (0);

error:
	fprintf(stderr, "zio_encrypt_ccm_ossl: internal OpenSSL failure; "
	    "datalen=%lu, adlen=%lu\n", datalen, adlen);
	_dump_ossl_errors();
	if (ctx)
		EVP_CIPHER_CTX_free(ctx);
	return (SET_ERROR(EIO));
}

static int
zio_decrypt_ccm_ossl(EVP_CIPHER *cipher, crypto_key_t *key,
    zfs_uio_t *ciphertext, zfs_uio_t *plaintext, size_t datalen,
    const uint8_t iv[ZIO_DATA_IV_LEN], const uint8_t *ad, size_t adlen,
    uint8_t mac[ZIO_DATA_MAC_LEN])
{
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if (!ctx)
		goto error;

	int len;

	/* Initialise decryption context with wanted cipher */
	if (EVP_DecryptInit_ex(ctx, cipher, NULL, NULL, NULL) != 1)
		goto error;

	/* Set IV length and MAC */
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_IVLEN,
	    ZIO_DATA_IV_LEN, NULL) != 1)
		goto error;
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_TAG,
	    ZIO_DATA_MAC_LEN, mac) != 1)
		goto error;

	/*
	 * Update initialisation with key and IV. We can't do this at the
	 * start because the IV length has not been set yet.
	 */
	if (EVP_DecryptInit_ex(ctx, NULL, NULL, key->ck_data, iv) != 1)
		goto error;

	/* OpenSSL CCM requires that the data length be provided up front */
	if (EVP_DecryptUpdate(ctx, NULL, &len, NULL, datalen) != 1)
		goto error;

	/*
	 * If available, mix in the AAD. OpenSSL CCM will still mix the state
	 * even if the length is zero, so we skip it if we don't have any.
	 */
	if (adlen > 0 && EVP_DecryptUpdate(ctx, NULL, &len, ad, adlen) != 1)
		goto error;

	/*
	 * OpenSSL CCM crypt can be called once, and always needs a buffer,
	 * even if its empty. So, if we have more than one iovec, we need to
	 * copy the input iovecs into a single linear buffer, crypt that, then
	 * copy back to the output iovecs. Meanwhile, if we have no data at
	 * all, we just make a valid pointer to some arbitrary place on the
	 * stack.
	 */
	uint8_t *pbuf = NULL, *cbuf = NULL, zbuf[0];
	if (datalen == 0) {
		pbuf = zbuf;
		cbuf = zbuf;
	} else if (zfs_uio_iovcnt(ciphertext) > 1) {
		pbuf = umem_alloc(datalen, UMEM_NOFAIL);
		cbuf = umem_alloc(datalen, UMEM_NOFAIL);

		len = 0;
		for (int i = 0; i < zfs_uio_iovcnt(ciphertext); i++) {
			memcpy(&cbuf[len], zfs_uio_iovbase(ciphertext, i),
			    zfs_uio_iovlen(ciphertext, i));
			len += zfs_uio_iovlen(ciphertext, i);
			ASSERT3U(len, <=, datalen);
		}
		ASSERT3U(len, ==, datalen);
	} else {
		pbuf = zfs_uio_iovbase(plaintext, 0);
		cbuf = zfs_uio_iovbase(ciphertext, 0);
		ASSERT3U(zfs_uio_iovlen(plaintext, 0), ==, datalen);
		ASSERT3U(zfs_uio_iovlen(ciphertext, 0), ==, datalen);
	}

	/* Run the crypt op */
	int rc = EVP_DecryptUpdate(ctx, pbuf, &len, cbuf, datalen);

	/* If success and we had multiple iovecs, copy it back */
	if (rc == 1 && zfs_uio_iovcnt(ciphertext) > 1) {
		len = 0;
		for (int i = 0; i < zfs_uio_iovcnt(plaintext); i++) {
			memcpy(zfs_uio_iovbase(plaintext, i),
			    &pbuf[len], zfs_uio_iovlen(plaintext, i));
			len += zfs_uio_iovlen(plaintext, i);
			ASSERT3U(len, <=, datalen);
		}
		ASSERT3U(len, ==, datalen);
	}

	/* Zero and free work buffers */
	if (zfs_uio_iovcnt(ciphertext) > 1) {
		/* XXX should be a secure zero -- robn, 2024-07-29 */
		memset(pbuf, 0, datalen);
		memset(cbuf, 0, datalen);
		umem_free(pbuf, datalen);
		umem_free(cbuf, datalen);
	}

	if (rc != 1)
		goto error;

	/* XXX what verifies the MAC for CCM? -- robn, 2024-07-29 */

	EVP_CIPHER_CTX_free(ctx);

	return (0);

error:
	fprintf(stderr, "zio_decrypt_ccm_ossl: internal OpenSSL failure; "
	    "datalen=%lu, adlen=%lu\n", datalen, adlen);
	_dump_ossl_errors();
	if (ctx)
		EVP_CIPHER_CTX_free(ctx);
	return (SET_ERROR(EIO));
}

static int
zio_encrypt_gcm_ossl(EVP_CIPHER *cipher, crypto_key_t *key,
    zfs_uio_t *plaintext, zfs_uio_t *ciphertext, size_t datalen,
    const uint8_t iv[ZIO_DATA_IV_LEN], const uint8_t *ad, size_t adlen,
    uint8_t mac[ZIO_DATA_MAC_LEN])
{
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if (!ctx)
		goto error;

	int len;

	/* Initialise encryption context with wanted cipher */
	if (EVP_EncryptInit_ex(ctx, cipher, NULL, NULL, NULL) != 1)
		goto error;

	/* Set IV length */
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
	    ZIO_DATA_IV_LEN, NULL) != 1)
		goto error;

	/*
	 * Update initialisation with key and IV. We can't do this at the
	 * start because the IV length has not been set yet.
	 */
	if (EVP_EncryptInit_ex(ctx, NULL, NULL, key->ck_data, iv) != 1)
		goto error;

	/* Mix in any AAD provided */
	if (EVP_EncryptUpdate(ctx, NULL, &len, ad, adlen) != 1)
		goto error;

	/* Walk the iovecs and run the crypt op for each pair */
	for (int i = 0; i < zfs_uio_iovcnt(plaintext); i++) {
		ASSERT3U(zfs_uio_iovlen(plaintext, i), ==,
		    zfs_uio_iovlen(ciphertext, i));
		if (EVP_EncryptUpdate(ctx, zfs_uio_iovbase(ciphertext, i), &len,
		    zfs_uio_iovbase(plaintext, i),
		    zfs_uio_iovlen(plaintext, i)) != 1)
			goto error;
		ASSERT3U(zfs_uio_iovlen(ciphertext, i), ==, len);
	}

	/* Finalise the encryption, which will compute the MAC internally */
	if (EVP_EncryptFinal_ex(ctx, NULL, &len) != 1)
		goto error;
	ASSERT0(len);

	/* Fill the MAC */
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG,
	    ZIO_DATA_MAC_LEN, mac) != 1)
		goto error;

	EVP_CIPHER_CTX_free(ctx);

	return (0);

error:
	fprintf(stderr, "zio_encrypt_gcm_ossl: internal OpenSSL failure; "
	    "datalen=%lu, adlen=%lu\n", datalen, adlen);
	_dump_ossl_errors();
	if (ctx)
		EVP_CIPHER_CTX_free(ctx);
	return (SET_ERROR(EIO));
}

static int
zio_decrypt_gcm_ossl(EVP_CIPHER *cipher, crypto_key_t *key,
    zfs_uio_t *ciphertext, zfs_uio_t *plaintext, size_t datalen,
    const uint8_t iv[ZIO_DATA_IV_LEN], const uint8_t *ad, size_t adlen,
    uint8_t mac[ZIO_DATA_MAC_LEN])
{
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if (!ctx)
		goto error;

	int len;

	/* Initialise decryption context with wanted cipher */
	if (EVP_DecryptInit_ex(ctx, cipher, NULL, NULL, NULL) != 1)
		goto error;

	/* Set IV length and MAC */
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
	    ZIO_DATA_IV_LEN, NULL) != 1)
		goto error;
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG,
	    ZIO_DATA_MAC_LEN, mac) != 1)
		goto error;

	/*
	 * Update initialisation with key and IV. We can't do this at the
	 * start because the IV length has not been set yet.
	 */
	if (EVP_DecryptInit_ex(ctx, NULL, NULL, key->ck_data, iv) != 1)
		goto error;

	/* Mix in any AAD provided */
	if (EVP_DecryptUpdate(ctx, NULL, &len, ad, adlen) != 1)
		goto error;

	/* Walk the iovecs and run the crypt op for each pair */
	for (int i = 0; i < zfs_uio_iovcnt(ciphertext); i++) {
		ASSERT3U(zfs_uio_iovlen(ciphertext, i), ==,
		    zfs_uio_iovlen(plaintext, i));
		if (EVP_DecryptUpdate(ctx, zfs_uio_iovbase(plaintext, i), &len,
		    zfs_uio_iovbase(ciphertext, i),
		    zfs_uio_iovlen(ciphertext, i)) != 1)
			goto error;
		ASSERT3U(zfs_uio_iovlen(plaintext, i), ==, len);
	}

	/* Finalise the decryption, which will verify the MAC internally */
	if (EVP_DecryptFinal_ex(ctx, NULL, &len) != 1)
		goto error;
	ASSERT0(len);

	/* Clean up */
	EVP_CIPHER_CTX_free(ctx);

	return (0);

error:
	fprintf(stderr, "zio_decrypt_gcm_ossl: internal OpenSSL failure; "
	    "datalen=%lu, adlen=%lu\n", datalen, adlen);
	_dump_ossl_errors();
	if (ctx)
		EVP_CIPHER_CTX_free(ctx);
	return (SET_ERROR(EIO));
}

int
zio_encrypt_os(const zio_crypt_info_t *ci,
    crypto_key_t *key, zio_crypt_session_t *sess,
    zfs_uio_t *plaintext, zfs_uio_t *ciphertext, size_t datalen,
    const uint8_t iv[ZIO_DATA_IV_LEN], const uint8_t *ad, size_t adlen,
    uint8_t mac[ZIO_DATA_MAC_LEN])
{
	(void) sess;

	EVP_CIPHER *cipher = zio_crypt_cipher_get(ci);
	if (!cipher)
		return (SET_ERROR(ENOTSUP));

	switch (ci->ci_crypt_type) {
	case ZC_TYPE_CCM:
		return (zio_encrypt_ccm_ossl(cipher, key,
		    plaintext, ciphertext, datalen, iv, ad, adlen, mac));
	case ZC_TYPE_GCM:
		return (zio_encrypt_gcm_ossl(cipher, key,
		    plaintext, ciphertext, datalen, iv, ad, adlen, mac));
	default:
		return (ENOTSUP);
	}
}

int zio_decrypt_os(const zio_crypt_info_t *ci,
    crypto_key_t *key, zio_crypt_session_t *sess,
    zfs_uio_t *ciphertext, zfs_uio_t *plaintext, size_t datalen,
    const uint8_t iv[ZIO_DATA_IV_LEN], const uint8_t *ad, size_t adlen,
    uint8_t mac[ZIO_DATA_MAC_LEN])
{
	(void) sess;

	EVP_CIPHER *cipher = zio_crypt_cipher_get(ci);
	if (!cipher)
		return (SET_ERROR(ENOTSUP));

	switch (ci->ci_crypt_type) {
	case ZC_TYPE_GCM:
		return (zio_decrypt_gcm_ossl(cipher, key,
		    ciphertext, plaintext, datalen, iv, ad, adlen, mac));
	case ZC_TYPE_CCM:
		return (zio_decrypt_ccm_ossl(cipher, key,
		    ciphertext, plaintext, datalen, iv, ad, adlen, mac));
	default:
		return (ENOTSUP);
	}
}

int
zio_crypt_hmac_os(zio_crypt_key_t *key, const uint8_t *data, size_t datalen,
    uint8_t digest[SHA512_HMAC_LEN])
{
	zio_crypt_hmac_t hmac;

	int err = zio_crypt_hmac_init_os(&hmac, key);
	if (err != 0)
		return (err);
	err = zio_crypt_hmac_update_os(&hmac, data, datalen);
	if (err != 0)
		return (err);
	err = zio_crypt_hmac_final_os(&hmac, digest);
	return (err);
}

int
zio_crypt_hmac_init_os(zio_crypt_hmac_t *hmac, zio_crypt_key_t *key)
{
	zc_openssl_mac_params_t *mp = zio_crypt_mac_get();
	if (!mp)
		return (SET_ERROR(ENOTSUP));

	EVP_MAC_CTX *ctx = EVP_MAC_CTX_new(mp->mp_mac);
	if (!ctx)
		goto error;

	if (EVP_MAC_init(ctx,
	    key->zk_hmac_keydata, SHA512_HMAC_KEYLEN, mp->mp_params) != 1)
		goto error;

	hmac->zh_ctx = ctx;
	return (0);

error:
	fprintf(stderr, "zio_crypt_hmac_init_os: internal OpenSSL failure\n");
	_dump_ossl_errors();
	if (ctx)
		EVP_MAC_CTX_free(ctx);
	return (SET_ERROR(EIO));
}

int
zio_crypt_hmac_update_os(zio_crypt_hmac_t *hmac, const uint8_t *data,
    size_t datalen)
{
	EVP_MAC_CTX *ctx = hmac->zh_ctx;

	if (EVP_MAC_update(ctx, data, datalen) != 1)
		goto error;

	return (0);

error:
	fprintf(stderr, "zio_crypt_hmac_update_os: internal OpenSSL failure; "
	    "datalen=%lu\n", datalen);
	_dump_ossl_errors();
	EVP_MAC_CTX_free(ctx);
	return (SET_ERROR(EIO));
}

int
zio_crypt_hmac_final_os(zio_crypt_hmac_t *hmac,
    uint8_t digest[SHA512_HMAC_LEN])
{
	EVP_MAC_CTX *ctx = hmac->zh_ctx;
	size_t len;

	int rc = EVP_MAC_final(ctx, digest, &len, SHA512_HMAC_LEN);
	if (rc != 1) {
		fprintf(stderr, "zio_crypt_hmac_final_os: "
		    "internal OpenSSL failure\n");
		_dump_ossl_errors();
	}

	EVP_MAC_CTX_free(ctx);

	return ((rc == 1) ? 0 : SET_ERROR(EIO));
}
