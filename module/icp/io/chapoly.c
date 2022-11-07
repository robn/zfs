/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2022, Rob Norris <robn@despairlabs.com>
 */

/*
 * ChaCha20-Poly1305 provider for the Kernel Cryptographic Framework (KCF)
 */

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/icp.h>


static int
chapoly_copy_blocks(void __attribute__((unused)) *ctx,
    caddr_t data, size_t length, crypto_data_t *out)
{
	int rv = crypto_put_output_data((uchar_t *)data, out, length);
	if (rv != CRYPTO_SUCCESS)
		return (rv);

	out->cd_offset += length;
	return (CRYPTO_SUCCESS);
}


static int
chapoly_encrypt_atomic(crypto_mechanism_t __attribute__((unused)) *mechanism,
    crypto_key_t __attribute__((unused)) *key,
    crypto_data_t *plaintext, crypto_data_t *ciphertext,
    crypto_spi_ctx_template_t __attribute__((unused)) template)
{
	off_t saved_offset = ciphertext->cd_offset;
	size_t saved_length = ciphertext->cd_length;

	int rv;
	switch (plaintext->cd_format) {
	case CRYPTO_DATA_RAW:
		rv = crypto_update_iov(NULL, plaintext, ciphertext,
		    chapoly_copy_blocks);
		break;
	case CRYPTO_DATA_UIO:
		rv = crypto_update_uio(NULL, plaintext, ciphertext,
		    chapoly_copy_blocks);
		break;
	default:
		rv = CRYPTO_ARGUMENTS_BAD;
	}

	if (rv == CRYPTO_SUCCESS)
		ciphertext->cd_length = ciphertext->cd_offset - saved_offset;
	else
		ciphertext->cd_length = saved_length;
	ciphertext->cd_offset = saved_offset;

	return (rv);
}


static int
chapoly_decrypt_atomic(crypto_mechanism_t *mechanism,
    crypto_key_t __attribute__((unused)) *key,
    crypto_data_t *ciphertext, crypto_data_t *plaintext,
    crypto_spi_ctx_template_t __attribute__((unused)) template)
{
	/* We don't actually do AES or GCM here, its just the default */
	/* parameter option in zio_do_crypt_uio and we only need this single */
	/* value from it, so its easier to just take that instead of making */
	/* our own thing. */
	CK_AES_GCM_PARAMS *gcmp =
	    (CK_AES_GCM_PARAMS *) mechanism->cm_param;
	size_t maclen = CRYPTO_BITS2BYTES(gcmp->ulTagBits);

	off_t saved_offset = plaintext->cd_offset;
	size_t saved_length = plaintext->cd_length;

	int rv;
	switch (plaintext->cd_format) {
	case CRYPTO_DATA_RAW:
		rv = crypto_update_iov(NULL, ciphertext, plaintext,
		    chapoly_copy_blocks);
		break;
	case CRYPTO_DATA_UIO:
		rv = crypto_update_uio(NULL, ciphertext, plaintext,
		    chapoly_copy_blocks);
		break;
	default:
		rv = CRYPTO_ARGUMENTS_BAD;
	}

	if (rv == CRYPTO_DATA_LEN_RANGE)
		if (plaintext->cd_length - plaintext->cd_offset == maclen)
			rv = CRYPTO_SUCCESS;

	if (rv == CRYPTO_SUCCESS)
		plaintext->cd_length = plaintext->cd_offset - saved_offset;
	else
		plaintext->cd_length = saved_length;
	plaintext->cd_offset = saved_offset;

	return (rv);
}


static const crypto_mech_info_t chapoly_mech_info_tab[] = {
	{SUN_CKM_CHACHA20_POLY1305, 0,
    CRYPTO_FG_ENCRYPT_ATOMIC | CRYPTO_FG_DECRYPT_ATOMIC },
};

static const crypto_cipher_ops_t chapoly_cipher_ops = {
	.encrypt_init = NULL,
	.encrypt = NULL,
	.encrypt_update = NULL,
	.encrypt_final = NULL,
	.encrypt_atomic = chapoly_encrypt_atomic,
	.decrypt_init = NULL,
	.decrypt = NULL,
	.decrypt_update = NULL,
	.decrypt_final = NULL,
	.decrypt_atomic = chapoly_decrypt_atomic
};

static const crypto_ops_t chapoly_crypto_ops = {
	.co_digest_ops = NULL,
	.co_cipher_ops = &chapoly_cipher_ops,
	.co_mac_ops = NULL,
	.co_ctx_ops = NULL,
};

static const crypto_provider_info_t chapoly_prov_info = {
	"Chacha20-Poly1305 Software Provider",
	&chapoly_crypto_ops,
	sizeof (chapoly_mech_info_tab) / sizeof (crypto_mech_info_t),
	chapoly_mech_info_tab
};

static crypto_kcf_provider_handle_t chapoly_prov_handle = 0;

int
chapoly_mod_init(void)
{
	/* Register with KCF.  If the registration fails, remove the module. */
	if (crypto_register_provider(&chapoly_prov_info, &chapoly_prov_handle))
		return (EACCES);

	return (0);
}

int
chapoly_mod_fini(void)
{
	/* Unregister from KCF if module is registered */
	if (chapoly_prov_handle != 0) {
		if (crypto_unregister_provider(chapoly_prov_handle))
			return (EBUSY);

		chapoly_prov_handle = 0;
	}

	return (0);
}
