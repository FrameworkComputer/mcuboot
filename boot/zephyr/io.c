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

#include <assert.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/usb/usb_device.h>
#include <soc.h>
#include <zephyr/linker/linker-defs.h>

#include "target.h"
#include "bootutil/bootutil_log.h"

BOOT_LOG_MODULE_DECLARE(mcuboot);

#if defined(CONFIG_BOOT_SERIAL_PIN_RESET) || defined(CONFIG_BOOT_FIRMWARE_LOADER_PIN_RESET)
#include <zephyr/drivers/hwinfo.h>
#endif

#if defined(CONFIG_BOOT_SERIAL_BOOT_MODE) || defined(CONFIG_BOOT_FIRMWARE_LOADER_BOOT_MODE)
#include <zephyr/retention/bootmode.h>
#endif

#ifdef CONFIG_BOOT_SERIAL_EXIT_GPIO
#include <zephyr/sys/reboot.h>
#endif

/* Validate serial recovery configuration */
#ifdef CONFIG_MCUBOOT_SERIAL
#if !defined(CONFIG_BOOT_SERIAL_ENTRANCE_GPIO) && \
    !defined(CONFIG_BOOT_SERIAL_WAIT_FOR_DFU) && \
    !defined(CONFIG_BOOT_SERIAL_BOOT_MODE) && \
    !defined(CONFIG_BOOT_SERIAL_NO_APPLICATION) && \
    !defined(CONFIG_BOOT_SERIAL_PIN_RESET)
#error "Serial recovery selected without an entrance mode set"
#endif
#endif

/* Validate firmware loader configuration */
#ifdef CONFIG_BOOT_FIRMWARE_LOADER
#if !defined(CONFIG_BOOT_FIRMWARE_LOADER_ENTRANCE_GPIO) && \
    !defined(CONFIG_BOOT_FIRMWARE_LOADER_BOOT_MODE) && \
    !defined(CONFIG_BOOT_FIRMWARE_LOADER_NO_APPLICATION) && \
    !defined(CONFIG_BOOT_FIRMWARE_LOADER_PIN_RESET)
#error "Firmware loader selected without an entrance mode set"
#endif
#endif

#ifdef CONFIG_MCUBOOT_INDICATION_LED

/*
 * The led0 devicetree alias is optional. If present, we'll use it
 * to turn on the LED whenever the button is pressed.
 */
#if DT_NODE_EXISTS(DT_ALIAS(mcuboot_led0))
#define LED0_NODE DT_ALIAS(mcuboot_led0)
#endif

#if DT_NODE_HAS_STATUS(LED0_NODE, okay) && DT_NODE_HAS_PROP(LED0_NODE, gpios)
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#else
/* A build error here means your board isn't set up to drive an LED. */
#error "Unsupported board: led0 devicetree alias is not defined"
#endif

void io_led_init(void)
{
    if (!device_is_ready(led0.port)) {
        BOOT_LOG_ERR("Didn't find LED device referred by the LED0_NODE\n");
        return;
    }

    gpio_pin_configure_dt(&led0, GPIO_OUTPUT);
    gpio_pin_set_dt(&led0, 0);
}

void io_led_set(int value)
{
    gpio_pin_set_dt(&led0, value);
}
#endif /* CONFIG_MCUBOOT_INDICATION_LED */

#if defined(CONFIG_BOOT_SERIAL_ENTRANCE_GPIO) || defined(CONFIG_BOOT_USB_DFU_GPIO) || \
    defined(CONFIG_BOOT_FIRMWARE_LOADER_ENTRANCE_GPIO)

#if defined(CONFIG_MCUBOOT_SERIAL)
#define BUTTON_0_DETECT_DELAY CONFIG_BOOT_SERIAL_DETECT_DELAY
#elif defined(CONFIG_BOOT_FIRMWARE_LOADER)
#define BUTTON_0_DETECT_DELAY CONFIG_BOOT_FIRMWARE_LOADER_DETECT_DELAY
#else
#define BUTTON_0_DETECT_DELAY CONFIG_BOOT_USB_DFU_DETECT_DELAY
#endif

#define BUTTON_0_NODE DT_ALIAS(mcuboot_button0)

#if DT_NODE_EXISTS(BUTTON_0_NODE) && DT_NODE_HAS_PROP(BUTTON_0_NODE, gpios)
static const struct gpio_dt_spec button0 = GPIO_DT_SPEC_GET(BUTTON_0_NODE, gpios);
#else
#error "Serial recovery/USB DFU button must be declared in device tree as 'mcuboot_button0'"
#endif

bool io_detect_pin(void)
{
    int rc;
    int pin_active;

    if (!device_is_ready(button0.port)) {
        BOOT_LOG_DBG("GPIO device is not ready.");
        return false;
    }

    rc = gpio_pin_configure_dt(&button0, GPIO_INPUT);
    if (rc != 0) {
        BOOT_LOG_DBG("Failed to initialize boot detect pin.");
        return false;
    }

    rc = gpio_pin_get_dt(&button0);
    pin_active = rc;

    if (rc < 0) {
        BOOT_LOG_DBG("Failed to read boot detect pin.");
        return false;
    }


    if (pin_active) {
        if (BUTTON_0_DETECT_DELAY > 0) {
#ifdef CONFIG_MULTITHREADING
            k_sleep(K_MSEC(50));
#else
            k_busy_wait(50000);
#endif

            /* Get the uptime for debounce purposes. */
            int64_t timestamp = k_uptime_get();

            for(;;) {
                rc = gpio_pin_get_dt(&button0);
                pin_active = rc;
                if (rc < 0) {
                    BOOT_LOG_DBG("Failed to read boot detect pin.");
                    return false;
                }

                /* Get delta from when this started */
                uint32_t delta = k_uptime_get() -  timestamp;

                /* If not pressed OR if pressed > debounce period, stop. */
                if (delta >= BUTTON_0_DETECT_DELAY || !pin_active) {
                    break;
                }

                /* Delay 1 ms */
#ifdef CONFIG_MULTITHREADING
                k_sleep(K_MSEC(1));
#else
                k_busy_wait(1000);
#endif
            }
        }
    }

    return (bool)pin_active;
}
#endif

#ifdef CONFIG_BOOT_SERIAL_EXIT_GPIO
#define EXIT_SWITCH_NODE DT_ALIAS(mcuboot_exit_switch)

#if DT_NODE_EXISTS(EXIT_SWITCH_NODE) && DT_NODE_HAS_PROP(EXIT_SWITCH_NODE, gpios)
static const struct gpio_dt_spec exit_switch = GPIO_DT_SPEC_GET(EXIT_SWITCH_NODE, gpios);
#else
#error "Serial recovery exit GPIO must be declared in device tree as 'mcuboot_exit_switch'"
#endif

static int exit_switch_entry_level;
static struct gpio_callback exit_switch_cb;

/* Runs in ISR context, debounce time after the last edge. */
static void exit_switch_settled(struct k_timer *timer)
{
    int level = gpio_pin_get_dt(&exit_switch);

    (void)timer;

    if (level < 0 || level == exit_switch_entry_level) {
        return;
    }

    BOOT_LOG_INF("Exit switch changed, leaving serial recovery");
    sys_reboot(SYS_REBOOT_COLD);
}

static K_TIMER_DEFINE(exit_switch_timer, exit_switch_settled, NULL);

static void exit_switch_edge(const struct device *port, struct gpio_callback *cb,
                             gpio_port_pins_t pins)
{
    (void)port;
    (void)cb;
    (void)pins;

    /* Restart on every edge so contact bounce only delays the check. */
    k_timer_start(&exit_switch_timer, K_MSEC(CONFIG_BOOT_SERIAL_EXIT_GPIO_DEBOUNCE_MS),
                  K_NO_WAIT);
}

void io_serial_exit_gpio_init(void)
{
    int rc;

    if (!gpio_is_ready_dt(&exit_switch)) {
        BOOT_LOG_ERR("Exit switch GPIO device is not ready");
        return;
    }

    rc = gpio_pin_configure_dt(&exit_switch, GPIO_INPUT);
    if (rc != 0) {
        BOOT_LOG_ERR("Failed to configure exit switch: %d", rc);
        return;
    }

    exit_switch_entry_level = gpio_pin_get_dt(&exit_switch);
    if (exit_switch_entry_level < 0) {
        BOOT_LOG_ERR("Failed to read exit switch: %d", exit_switch_entry_level);
        return;
    }

    gpio_init_callback(&exit_switch_cb, exit_switch_edge, BIT(exit_switch.pin));
    rc = gpio_add_callback_dt(&exit_switch, &exit_switch_cb);
    if (rc == 0) {
        rc = gpio_pin_interrupt_configure_dt(&exit_switch, GPIO_INT_EDGE_BOTH);
    }
    if (rc != 0) {
        BOOT_LOG_ERR("Failed to arm exit switch: %d", rc);
    }
}
#endif /* CONFIG_BOOT_SERIAL_EXIT_GPIO */

#if defined(CONFIG_BOOT_SERIAL_PIN_RESET) || defined(CONFIG_BOOT_FIRMWARE_LOADER_PIN_RESET)
bool io_detect_pin_reset(void)
{
    uint32_t reset_cause;
    int rc;

    rc = hwinfo_get_reset_cause(&reset_cause);

    if (rc == 0 && (reset_cause & RESET_PIN)) {
        (void)hwinfo_clear_reset_cause();
        return true;
    }

    return false;
}
#endif

#if defined(CONFIG_BOOT_SERIAL_BOOT_MODE) || defined(CONFIG_BOOT_FIRMWARE_LOADER_BOOT_MODE)
bool io_detect_boot_mode(void)
{
    int32_t boot_mode;

    boot_mode = bootmode_check(BOOT_MODE_TYPE_BOOTLOADER);

    if (boot_mode == 1) {
        /* Boot mode to stay in bootloader, clear status and enter serial
         * recovery mode
         */
        bootmode_clear();

        return true;
    }

    return false;
}
#endif
