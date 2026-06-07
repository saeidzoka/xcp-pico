/**
 * @file    main.c
 * @brief   xcp-pico firmware entry point.
 *
 * Milestone 3 scope: XCP protocol layer on top of the SxI framing layer.
 * The main loop drives the USB transport, SxI framing, and XCP command
 * processor. The loopback from Milestone 2 is replaced by the protocol task.
 *
 * Transport selection is controlled by xcp_config.h. This file owns the
 * concrete xcp_transport_ops_t registration: it knows which transport is
 * active and wires the vtable accordingly. xcp_protocol.c knows nothing
 * about the transport. See ADR-002.
 */

#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "xcp_config.h"
#include "xcp_transport.h"
#include "xcp_protocol.h"

/* -------------------------------------------------------------------------
 * Transport selection and vtable registration
 *
 * Each transport path provides four functions matching xcp_transport_ops_t.
 * Where the concrete transport API does not match the bool-returning vtable
 * signature, a thin adapter is provided here.
 * ------------------------------------------------------------------------- */

#if XCP_TRANSPORT_USB_CDC

#include "xcp_transport_usb.h"
#include "xcp_transport_sxi.h"

/* xcp_transport_sxi_get_packet and xcp_transport_sxi_send_packet return
 * xcp_sxi_status_t. The vtable expects bool. These adapters convert without
 * touching the SxI module.                                                  */
static bool sxi_get_packet(uint8_t *buf, size_t max_len, size_t *actual_len)
{
    return xcp_transport_sxi_get_packet(buf, max_len, actual_len) == XCP_SXI_OK;
}

static bool sxi_send_packet(const uint8_t *data, size_t len)
{
    return xcp_transport_sxi_send_packet(data, len) == XCP_SXI_OK;
}

static const xcp_transport_ops_t s_transport_ops = {
    .packet_available = xcp_transport_sxi_packet_available,
    .get_packet       = sxi_get_packet,
    .send_packet      = sxi_send_packet,
    .is_connected     = xcp_transport_usb_is_connected,
};

#elif XCP_TRANSPORT_CAN

#include "xcp_transport_can.h"

static const xcp_transport_ops_t s_transport_ops = {
    .packet_available = xcp_transport_can_packet_available,
    .get_packet       = xcp_transport_can_get_packet,
    .send_packet      = xcp_transport_can_send_packet,
    .is_connected     = xcp_transport_can_is_connected,
};

#endif /* XCP_TRANSPORT_* */

/* -------------------------------------------------------------------------
 * Heartbeat
 * ------------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

int main(void)
{
    stdio_init_all();

    heartbeat_init();

#if XCP_TRANSPORT_USB_CDC
    xcp_transport_usb_init();
    xcp_transport_sxi_init();
#elif XCP_TRANSPORT_CAN
    xcp_transport_can_init();
#endif

    xcp_protocol_init(&s_transport_ops);

    while (true) {
#if XCP_TRANSPORT_USB_CDC
        xcp_transport_usb_task();
        xcp_transport_sxi_task();
#elif XCP_TRANSPORT_CAN
        xcp_transport_can_task();
#endif
        xcp_protocol_task();
        heartbeat_update();
    }

    return 0;
}