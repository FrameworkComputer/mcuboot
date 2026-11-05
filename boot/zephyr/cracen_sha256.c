/*
 * Copyright (c) 2026 Framework Computer Inc
 * SPDX-License-Identifier: Apache-2.0
 *
 * SHA-256 on the nRF54L CRACEN hash engine (BA413), for image validation.
 *
 * bootutil_sha_update() only records a DMA descriptor for each chunk; the
 * caller's buffers must stay valid until bootutil_sha_finish(), which is the
 * case with BOOT_IMG_HASH_DIRECTLY_ON_STORAGE (one update over the
 * memory-mapped slot) and for the key hashes. finish() then runs the whole
 * chain in one CryptoMaster job with hardware padding. If the job reports a
 * bus error or doesn't finish, the same chunks are hashed with tinycrypt, so
 * a failure costs time, not the boot.
 *
 * Programming model as in Nordic's sxsymcrypt (hash.c, cmdma.h): config
 * word = mode | HW padding (bit 9) | final (bit 10), tagged for the hash
 * engine's config register; message descriptors tagged hash/message, the
 * last one with LAST and the count of padding bytes it ignores.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <bootutil/crypto/sha.h>
#include <tinycrypt/sha256.h>
#include <tinycrypt/constants.h>

#include <hal/nrf_cracen.h>
#include <hal/nrf_cracen_cm.h>
#include <helpers/nrf_cracen_cm_dma.h>

#define BA413_MODE_SHA2_256 0x08u
#define BA413_HW_PAD        (1u << 9)
#define BA413_FINAL         (1u << 10)
#define TAG_IGN(n)          ((uint32_t)(n) << 8)
/* ~ a few seconds at 128 MHz; the engine needs ~5 ms for 600 KB. */
#define POLL_LIMIT          50000000u

void bootutil_cracen_sha256_init(struct bootutil_cracen_sha256 *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}

int bootutil_cracen_sha256_update(struct bootutil_cracen_sha256 *ctx, const void *data,
                                  uint32_t len)
{
    if (len == 0) {
        return 0;
    }
    if (ctx->n >= BOOTUTIL_CRACEN_SHA_SEGS) {
        ctx->overflow = true;
        return -1;
    }
    ctx->seg[ctx->n].p = data;
    ctx->seg[ctx->n].len = len;
    ctx->n++;
    ctx->total += len;
    return 0;
}

static int sw_fallback(struct bootutil_cracen_sha256 *ctx, uint8_t *out)
{
    struct tc_sha256_state_struct s;

    tc_sha256_init(&s);
    for (uint32_t i = 0; i < ctx->n; i++) {
        tc_sha256_update(&s, ctx->seg[i].p, ctx->seg[i].len);
    }
    return tc_sha256_final(out, &s) == TC_CRYPTO_SUCCESS ? 0 : -1;
}

static int hw_run(struct bootutil_cracen_sha256 *ctx, uint8_t *out)
{
    static const uint32_t cfg = BA413_MODE_SHA2_256 | BA413_HW_PAD | BA413_FINAL;
    static struct nrf_cracen_cm_dma_desc in[1 + BOOTUTIL_CRACEN_SHA_SEGS] __aligned(4);
    static struct nrf_cracen_cm_dma_desc o __aligned(4);
    static uint32_t digest[8];
    uint32_t ign = (4u - (ctx->total & 3u)) & 3u;
    uint32_t pend, busy, i;
    int rc = -1;

    if (ctx->n == 0 || ctx->overflow) {
        return -1;
    }

    in[0].p_addr = (uint8_t *)&cfg;
    in[0].length = sizeof(cfg) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
    in[0].dmatag = NRF_CRACEN_CM_DMA_TAG_ENGINE_HASH | NRF_CRACEN_CM_DMA_TAG_CONFIG;
    in[0].p_next = &in[1];
    for (i = 0; i < ctx->n; i++) {
        bool last = i + 1 == ctx->n;

        in[1 + i].p_addr = (uint8_t *)ctx->seg[i].p;
        in[1 + i].length = (ctx->seg[i].len + (last ? ign : 0)) |
                           NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
        in[1 + i].dmatag = NRF_CRACEN_CM_DMA_TAG_ENGINE_HASH |
                           NRF_CRACEN_CM_DMA_TAG_DATATYPE_HASH_MESSAGE |
                           (last ? NRF_CRACEN_CM_DMA_TAG_LAST | TAG_IGN(ign) : 0);
        in[1 + i].p_next = last ? NRF_CRACEN_CM_DMA_DESC_STOP : &in[2 + i];
    }

    o.p_addr = (uint8_t *)digest;
    o.length = sizeof(digest) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
    o.dmatag = NRF_CRACEN_CM_DMA_TAG_LAST;
    o.p_next = NRF_CRACEN_CM_DMA_DESC_STOP;

    nrf_cracen_module_enable(NRF_CRACEN, NRF_CRACEN_MODULE_CRYPTOMASTER_MASK);
    nrf_cracen_cm_fetch_addr_set(NRF_CRACENCORE, (void *)in);
    nrf_cracen_cm_push_addr_set(NRF_CRACENCORE, (void *)&o);
    nrf_cracen_cm_config_indirect_set(NRF_CRACENCORE,
                                      (nrf_cracen_cm_config_indirect_mask_t)
                                      (NRF_CRACEN_CM_CONFIG_INDIRECT_FETCH_MASK |
                                       NRF_CRACEN_CM_CONFIG_INDIRECT_PUSH_MASK));
    __DMB();
    nrf_cracen_cm_start(NRF_CRACENCORE);

    for (i = 0; i < POLL_LIMIT; i++) {
        pend = nrf_cracen_cm_int_pending_get(NRF_CRACENCORE);
        if (pend & (NRF_CRACEN_CM_INT_FETCH_ERROR_MASK | NRF_CRACEN_CM_INT_PUSH_ERROR_MASK)) {
            break;
        }
        busy = nrf_cracen_cm_status_get(NRF_CRACENCORE,
                                        NRF_CRACEN_CM_STATUS_BUSY_FETCH_MASK |
                                        NRF_CRACEN_CM_STATUS_BUSY_PUSH_MASK |
                                        NRF_CRACEN_CM_STATUS_PUSH_WAITING_MASK);
        if (!busy) {
            rc = 0;
            break;
        }
    }

    nrf_cracen_cm_softreset(NRF_CRACENCORE);
    nrf_cracen_module_disable(NRF_CRACEN, NRF_CRACEN_MODULE_CRYPTOMASTER_MASK);

    if (rc == 0) {
        memcpy(out, digest, sizeof(digest));
    }
    return rc;
}

/* Diagnostics for the attach-time experiment, readable over SWD. */
volatile uint32_t bootutil_cracen_sha256_hw_ok;
volatile uint32_t bootutil_cracen_sha256_fallbacks;

int bootutil_cracen_sha256_finish(struct bootutil_cracen_sha256 *ctx, uint8_t *out)
{
    if (hw_run(ctx, out) == 0) {
        bootutil_cracen_sha256_hw_ok++;
        return 0;
    }
    bootutil_cracen_sha256_fallbacks++;
    return sw_fallback(ctx, out);
}
