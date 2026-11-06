/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Expected image vendor and class UUIDs (BOOT_UUID_VID_NAME,
 * BOOT_UUID_CID_NAME), turned into bytes by CMakeLists.txt. An image whose
 * IMAGE_TLV_UUID_VID / _CID is missing or different fails validation
 * (image_validate.c), so a bootloader only boots, swaps in or accepts
 * through recovery images built for its own product and board.
 */

#include <string.h>

#include "bootutil/mcuboot_uuid.h"
#include "bootutil/fault_injection_hardening.h"

#ifdef CONFIG_MCUBOOT_UUID_VID
static const struct image_uuid expected_vid = {{BOOT_UUID_VID_BYTES}};
#endif
#ifdef CONFIG_MCUBOOT_UUID_CID
static const struct image_uuid expected_cid = {{BOOT_UUID_CID_BYTES}};
#endif

fih_ret boot_uuid_init(void)
{
    FIH_RET(FIH_SUCCESS);
}

static fih_ret uuid_match(const struct image_uuid *a, const struct image_uuid *b)
{
    FIH_DECLARE(rc, FIH_FAILURE);

    if (memcmp(a->raw, b->raw, sizeof(a->raw)) == 0) {
        rc = FIH_SUCCESS;
    }
    FIH_RET(rc);
}

#ifdef CONFIG_MCUBOOT_UUID_VID
fih_ret boot_uuid_vid_match(uint32_t image_id, const struct image_uuid *uuid_vid)
{
    FIH_DECLARE(rc, FIH_FAILURE);

    (void)image_id;
    FIH_CALL(uuid_match, rc, uuid_vid, &expected_vid);
    FIH_RET(rc);
}
#endif

#ifdef CONFIG_MCUBOOT_UUID_CID
fih_ret boot_uuid_cid_match(uint32_t image_id, const struct image_uuid *uuid_cid)
{
    FIH_DECLARE(rc, FIH_FAILURE);

    (void)image_id;
    FIH_CALL(uuid_match, rc, uuid_cid, &expected_cid);
    FIH_RET(rc);
}
#endif
