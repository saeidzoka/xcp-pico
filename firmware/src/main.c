/**
 * @file    main.c
 * @brief   xcp-pico firmware entry point.
 *
 * Milestone 3 scope: XCP protocol layer on top of the SxI framing layer.
 * The main loop drives the USB transport, SxI framing, and XCP command
 * processor. The loopback from Milestone 2 is replaced by the protocol task.
 */

#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "xcp_config.h"
#include "xcp_transport_usb.h"
#include "xcp_transport_sxi.h"
#include "xcp_protocol.h"

#define HEARTBEAT_INTERVAL_MS   500U

static void heartbeat_init(void)
{
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
}

static void heartbeat_update(void)
{
    static uint32_t last_toggle_ms = 0U;
    static bool led_state = false;

    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    if ((now_ms - last_toggle_ms) >= HEARTBEAT_INTERVAL_MS) {
        led_state = !led_state;
        gpio_put(PICO_DEFAULT_LED_PIN, led_state);
        last_toggle_ms = now_ms;
    }
}

int main(void)
{
    stdio_init_all();

    heartbeat_init();
    xcp_transport_usb_init();
    xcp_transport_sxi_init();
    xcp_protocol_init();

    while (true) {
        xcp_transport_usb_task();
        xcp_transport_sxi_task();
        xcp_protocol_task();
        heartbeat_update();
    }

    return 0;
}