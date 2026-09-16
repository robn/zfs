#include <linux/module.h>
#include <crypto/sha2.h>
#include <sys/zalgo.h>
#include <sys/registry.h>

static int
zg_linux_hmac_sha512_init(void **ctxp, const uint8_t *key, size_t keylen)
{
	struct hmac_sha512_ctx *ctx =
	    kmem_alloc(sizeof (struct hmac_sha512_ctx), KM_SLEEP);

	hmac_sha512_init_usingrawkey(ctx, key, keylen);

	*ctxp = ctx;

	return (0);
}

static int
zg_linux_hmac_sha512_update(void **ctxp, const uint8_t *msg, size_t msglen)
{
	hmac_sha512_update(*ctxp, msg, msglen);
	return (0);
}

static int
zg_linux_hmac_sha512_final(void **ctxp, uint8_t *mac)
{
	hmac_sha512_final(*ctxp, mac);
	kmem_free(*ctxp, sizeof (struct hmac_sha512_ctx));
	return (0);
}

static int
zg_linux_hmac_sha512_once(const uint8_t *key, size_t keylen,
    const uint8_t *msg, size_t msglen, uint8_t *mac)
{
	hmac_sha512_usingrawkey(key, keylen, msg, msglen, mac);
	return (0);
}

static const zalgo_mac_ops_t zg_linux_hmac_sha512_ops = {
	.zgm_op_init = zg_linux_hmac_sha512_init,
	.zgm_op_update = zg_linux_hmac_sha512_update,
	.zgm_op_final = zg_linux_hmac_sha512_final,
	.zgm_op_once = zg_linux_hmac_sha512_once,
};

static zalgo_mac_register_fn_t zg_mac_register;

static int __init
zalgo_linux_init(void)
{
	int ret = 0, err;

	err = 0;
	if ((err = spl_registry_hold("zalgo_mac_register",
	    (void **)&zg_mac_register)) == 0)
		err = zg_mac_register(ZG_MAC_HMAC_SHA512, "linux",
		    "Linux HMAC-SHA512", &zg_linux_hmac_sha512_ops);
	if (err != 0 && ret == 0)
		ret = err;

	return (-ret);
}

static void __exit
zalgo_linux_fini(void)
{
}

module_init(zalgo_linux_init);
module_exit(zalgo_linux_fini);

MODULE_DESCRIPTION("zalgo_linux");
MODULE_LICENSE("GPL");
