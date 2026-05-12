/**
 * @file    xcp_transport_usb.c
 * @brief   USB CDC byte-stream transport implementation.
 *
 * Implements the byte-stream transport API declared in xcp_transport_usb.h
 * by sitting directly on top of tinyusb's CDC class. We deliberately bypass
 * the SDK's pico_stdio_usb wrapper for XCP traffic, because that wrapper is
 * designed for text streams and adds buffering that is undesirable for a
 * binary protocol.
 *
 * NOTE for later milestones: pico_stdio_usb is currently enabled (so that
 * printf still works for diagnostic logging during early development), and
 * it shares the same CDC interface as our XCP transport. Once XCP traffic
 * begins in Milestone 2, printf-based logging on this interface must be
 * disabled or moved to a second CDC interface to avoid corrupting framed
 * XCP packets.
 */

#include "xcp_transport_usb.h"

#include "pico/stdlib.h"
#include "tusb.h"

/* The CDC interface index. tinyusb supports multiple CDC interfaces; we use
 * the first (and only) one for now.
 */
#define XCP_CDC_ITF     0U

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

void xcp_transport_usb_init(void)
{
    /* tinyusb device init. The board-specific USB hardware (RP2350 USB
     * controller) is brought up by the SDK; we just kick tinyusb.
     *
     * Note: stdio_init_all() in main.c also initialises tinyusb if
     * pico_enable_stdio_usb is set in CMake. Calling tud_init() a second
     * time is safe (idempotent), but we rely on the order: stdio first,
     * then this module.
     */
    tusb_init();
}

void xcp_transport_usb_task(void)
{
    /* Bring up tinyusb in device mode. The board-specific USB hardware
     * (RP2350 USB controller) is configured by the SDK; tusb_init() picks
     * up the port and class configuration from tusb_config.h.
     *
     * Note: stdio_init_all() in main.c also initialises tinyusb if
     * pico_enable_stdio_usb is set in CMake. Calling tusb_init() a second
     * time is safe (idempotent), but we rely on the order: stdio first,
     * then this module.
     */
    tud_task();
}

bool xcp_transport_usb_is_connected(void)
{
    /* tud_cdc_n_connected() returns true when the host has asserted DTR,
     * which (for our purposes) means a host-side application has opened
     * the virtual COM port.
     */
    return tud_cdc_n_connected(XCP_CDC_ITF);
}

size_t xcp_transport_usb_send(const uint8_t *data, size_t len)
{
    if ((data == NULL) || (len == 0U)) {
        return 0U;
    }

    /* Write as much as the TX FIFO can absorb right now. Any unsent bytes
     * are the caller's responsibility to retry on a later call.
     */
    const uint32_t written = tud_cdc_n_write(XCP_CDC_ITF, data, (uint32_t)len);

    /* Flush hint: tells tinyusb to send what's queued at the next
     * opportunity rather than waiting for the FIFO to fill. Critical for
     * low-latency request/response patterns like XCP.
     */
    (void)tud_cdc_n_write_flush(XCP_CDC_ITF);

    return (size_t)written;
}

size_t xcp_transport_usb_send_blocking(const uint8_t *data, size_t len)
{
    if ((data == NULL) || (len == 0U)) {
        return 0U;
    }

    /* 100 ms is generous compared to USB Full-Speed wire time for any
     * frame we will produce. XCP_MAX_CTO is 64 bytes, so a full SxI frame
     * is at most 68 bytes; that is on the order of 70 us at 12 Mbit/s.
     * The timeout exists to bound the wait if the host disconnects mid-
     * transfer, not to handle steady-state congestion.
     */
    const uint32_t timeout_us = 100U * 1000U;
    const uint32_t deadline   = time_us_32() + timeout_us;

    size_t total_sent = 0U;

    while (total_sent < len) {
        if (!tud_cdc_n_connected(XCP_CDC_ITF)) {
            /* Host disconnected. Stop trying; the caller will see the
             * partial count and decide what to do.
             */
            break;
        }

        if ((int32_t)(time_us_32() - deadline) > 0) {
            /* Timeout. Same handling as disconnect: stop and report.
             */
            break;
        }

        const size_t remaining = len - total_sent;
        const uint32_t written = tud_cdc_n_write(XCP_CDC_ITF,
                                                  &data[total_sent],
                                                  (uint32_t)remaining);
        total_sent += (size_t)written;

        /* Hint tinyusb to push queued bytes out at the next opportunity. */
        (void)tud_cdc_n_write_flush(XCP_CDC_ITF);

        /* Service the USB stack so it actually moves bytes from FIFO to
         * the wire. Without this, the FIFO stays full and the next
         * tud_cdc_n_write() call would write zero bytes forever.
         */
        tud_task();
    }

    return total_sent;
}

size_t xcp_transport_usb_receive(uint8_t *buffer, size_t max_len)
{
    if ((buffer == NULL) || (max_len == 0U)) {
        return 0U;
    }

    /* Non-blocking: returns 0 if the RX FIFO is empty.
     */
    if (!tud_cdc_n_available(XCP_CDC_ITF)) {
        return 0U;
    }

    const uint32_t read = tud_cdc_n_read(XCP_CDC_ITF, buffer, (uint32_t)max_len);
    return (size_t)read;
}