/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 * Copyright (c) 2020 Arm Limited
 * Copyright (c) 2021-2023 Nordic Semiconductor ASA
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef H_IO_
#define H_IO_

#include <stddef.h>

#ifdef CONFIG_SOC_FAMILY_NORDIC_NRF
#include <helpers/nrfx_reset_reason.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialises the configured LED.
 */
void io_led_init(void);

/*
 * Sets value of the configured LED.
 */
void io_led_set(int value);

/*
 * Checks if GPIO is set in the required way to remain in serial recovery mode
 *
 * @retval	false for normal boot, true for serial recovery boot
 */
bool io_detect_pin(void);

/*
 * Checks if board was reset using reset pin and if device should stay in
 * serial recovery mode
 *
 * @retval	false for normal boot, true for serial recovery boot
 */
bool io_detect_pin_reset(void);

/*
 * Checks board boot mode via retention subsystem and if device should stay in
 * serial recovery mode
 *
 * @retval	false for normal boot, true for serial recovery boot
 */
bool io_detect_boot_mode(void);

#ifdef CONFIG_SOC_FAMILY_NORDIC_NRF
static inline bool io_boot_skip_serial_recovery()
{
    uint32_t rr = nrfx_reset_reason_get();

#ifdef NRF_RESETINFO
    /* The following reset reasons should allow to enter firmware recovery or
     * loader through an IO state.
     */
    const uint32_t allowed_reset_reasons = (
        /* Reset from power on reset (reset reason POR or BOR). */
        NRFX_RESET_REASON_POR_MASK
#ifdef RESETINFO_RESETREAS_GLOBAL_RESETPOR_Msk
        /* Reset from power on reset (reset reason other than POR or BOR). */
        | RESETINFO_RESETREAS_GLOBAL_RESETPOR_Msk
#endif
        /* Reset from pin reset is not masked: it always enables the io-based recovery. */
        /* Reset from the watchdog timer. */
        | NRFX_RESET_REASON_DOG_MASK
        /* Reset from CTRL-AP. */
        | NRFX_RESET_REASON_CTRLAP_MASK
        /* Reset due to secure domain system reset request. */
        | NRFX_RESET_REASON_SREQ_MASK
        /* Reset due to secure domain watchdog 0 timer. */
        | NRFX_RESET_REASON_SECWDT0_MASK
        /* Reset due to secure domain watchdog 1 timer. */
        | NRFX_RESET_REASON_SECWDT1_MASK
        /* Reset due to secure domain lockup. */
        | NRFX_RESET_REASON_LOCKUP_MASK
#if NRF_RESETINFO_HAS_LOCAL_WDT
        /* Reset from the local watchdog timer 0. */
        | NRFX_RESET_REASON_LOCAL_DOG0_MASK
        /* Reset from the local watchdog timer 1. */
        | NRFX_RESET_REASON_LOCAL_DOG1_MASK
#endif
        /* Reset from the local soft reset request. */
        | NRFX_RESET_REASON_LOCAL_SREQ_MASK
        /* Reset from local CPU lockup. */
        | NRFX_RESET_REASON_LOCAL_LOCKUP_MASK
    );

    rr &= ~allowed_reset_reasons;

    return !(rr == 0 || (rr & NRFX_RESET_REASON_RESETPIN_MASK));
#else
    /* Only a plain wake-up from System OFF (GPIO DETECT, LPCOMP, NFC, VBUS,
     * GRTC) skips the IO-based entrance: a button that doubles as the wake-up
     * source must not drop the device into recovery on every wake. Every
     * other reset honors the button.
     *
     * RESETREAS is cumulative until software clears it, and Zephyr clears it
     * right before entering System OFF. So a wake-up shows the wake bit alone,
     * while any further bit means the device has reset again since (soft
     * reset, watchdog, lockup, ...) without the application clearing the
     * register -- typically an application that faults early in boot. An
     * allowlist of reset reasons would keep vetoing the button forever there
     * because of the stale wake bit, which is exactly when recovery is needed.
     * An empty register is a power-on or brown-out reset.
     */
    const uint32_t system_off_wake_reasons = (
        NRFX_RESET_REASON_OFF_MASK
#if NRFX_RESET_REASON_HAS_LPCOMP
        | NRFX_RESET_REASON_LPCOMP_MASK
#endif
#if NRFX_RESET_REASON_HAS_NFC
        | NRFX_RESET_REASON_NFC_MASK
#endif
#if NRFX_RESET_REASON_HAS_VBUS
        | NRFX_RESET_REASON_VBUS_MASK
#endif
#if NRFX_RESET_REASON_HAS_GRTC
        | NRFX_RESET_REASON_GRTC_MASK
#endif
    );

    return rr != 0 && (rr & ~system_off_wake_reasons) == 0;
#endif /* NRF_RESETINFO */
}
#else
static inline bool io_boot_skip_serial_recovery()
{
    return false;
}
#endif

#ifdef __cplusplus
}
#endif

#endif
