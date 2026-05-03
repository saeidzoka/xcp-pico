/**
 * @file    main.c
 * @brief   xcp-pico firmware entry point.
 *
 * Milestone 1 scope: bring up the USB CDC transport and provide a byte-level
 * loopback. Anything sent by the host over the virtual COM port is echoed
 * back. This validates the end-to-end byte stream before any XCP framing or
 * protocol logic is added.
 *
 * Milestone 2 will replace the loopback with SxI framing on top of the same
 * transport API.
 */

#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "xcp_config.h"
#include "xcp_transport_usb.h"
#include "xcp_transport_sxi.h"

/* Heartbeat: blink the on-board LED at ~2 Hz so it's obvious whether the
 * firmware is running or has hung. The interval is checked against the
 * SDK's millisecond clock, not delayed with sleep_ms(), to keep the USB
 * task responsive.
 */
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

/* Loopback buffer. Sized to MAX_CTO so it can hold the largest XCP packet
 * we'll deal with in later milestones, even though Milestone 1 only treats
 * the contents as opaque bytes.
 */
static uint8_t loopback_buffer[XCP_MAX_CTO];

static void loopback_step(void)
{
    const size_t received = xcp_transport_usb_receive(loopback_buffer,
                                                      sizeof(loopback_buffer));
    if (received == 0U) {
        return;
    }

    /* Echo whatever we received. If the TX FIFO can't accept all of it
     * right now, the unsent tail is dropped for this Milestone; proper
     * back-pressure handling lands with the SxI framing layer in M2.
     */
    (void)xcp_transport_usb_send(loopback_buffer, received);
}

int main(void)
{
    /* Bring up the SDK's stdio routing first. With pico_enable_stdio_usb=1
     * this attaches printf to the USB CDC interface; we'll share the same
     * interface with the XCP transport for now (see note in
     * xcp_transport_usb.c).
     */
    stdio_init_all();

    heartbeat_init();
    xcp_transport_usb_init();
    xcp_transport_sxi_init();

    /* Main loop. tinyusb is cooperative, so xcp_transport_usb_task() must
     * run frequently. Anything that blocks here will stall USB.
     */
    while (true) {
        xcp_transport_usb_task();
        loopback_step();
        heartbeat_update();
    }

    /* Unreachable. */
    return 0;
}