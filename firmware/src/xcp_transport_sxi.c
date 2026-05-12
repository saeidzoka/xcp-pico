/**
 * @file    xcp_transport_sxi.c
 * @brief   XCP on SxI transport layer implementation.
 *
 * See xcp_transport_sxi.h for API documentation and the ADR at
 * docs/adr/001-xcp-on-sxi-over-usb-cdc.md for the design rationale.
 */

#include "xcp_transport_sxi.h"
#include "xcp_transport_usb.h"
#include "xcp_config.h"

#include "pico/time.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal constants                                                  */
/* ------------------------------------------------------------------ */

/**
 * Inter-byte idle timeout in microseconds. If the receive state machine
 * is mid-frame and no new bytes arrive within this window, the parser
 * is reset to recover from a stalled or aborted transmission.
 *
 * 50 ms chosen as a balance between responsiveness and tolerance for
 * USB CDC scheduling jitter on the host side.
 */
#define XCP_SXI_IDLE_TIMEOUT_US  (50u * 1000u)

/**
 * Size of the SxI header in bytes: LEN(2) + CTR(2).
 * Defined here as well as in xcp_config.h for local clarity.
 */
#define SXI_HEADER_SIZE          (4u)

/* ------------------------------------------------------------------ */
/* State machine                                                       */
/* ------------------------------------------------------------------ */

typedef enum {
    SXI_STATE_WAIT_HEADER = 0,   /**< Accumulating bytes 0..3 (LEN+CTR) */
    SXI_STATE_WAIT_PAYLOAD,      /**< Accumulating LEN bytes of payload */
    SXI_STATE_FRAME_READY,       /**< Complete frame waiting for consumer */
} sxi_rx_state_t;

/* ------------------------------------------------------------------ */
/* Module state (file-scope, not thread-safe)                          */
/* ------------------------------------------------------------------ */

/* Receive path */
static uint8_t         rx_buffer[XCP_SXI_MAX_FRAME];
static size_t          rx_index;
static size_t          rx_expected_payload_len;
static uint16_t        rx_last_ctr;
static bool            rx_ctr_initialised;
static sxi_rx_state_t  rx_state;
static uint32_t        rx_last_byte_time_us;

/* Transmit path */
static uint16_t        tx_ctr;

/* Diagnostics */
static xcp_sxi_stats_t stats;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/**
 * Reset the receive state machine to a known idle state.
 * Does NOT clear statistics.
 */
static void rx_reset(void)
{
    rx_state = SXI_STATE_WAIT_HEADER;
    rx_index = 0;
    rx_expected_payload_len = 0;
}

/**
 * Extract a little-endian 16-bit value from the buffer.
 *
 * SxI uses little-endian byte order per ASAM XCP V1.5 Part 2.
 * RP2350 is also little-endian, but we avoid type punning to
 * stay portable and to make the byte order explicit at the
 * call site.
 */
static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/**
 * Parse the SxI header from rx_buffer[0..3] and decide whether
 * to advance to WAIT_PAYLOAD, mark FRAME_READY (zero-length
 * payload), or trigger error recovery.
 */
static void handle_header_complete(void)
{
    const uint16_t len = read_u16_le(&rx_buffer[0]);
    const uint16_t ctr = read_u16_le(&rx_buffer[2]);

    /* LEN validation: payload must fit within the maximum
     * configured CTO size. An oversized LEN almost certainly
     * means we are out of sync, not legitimate corruption,
     * because USB CDC is a reliable transport.
     */
    if (len > XCP_MAX_CTO) {
        stats.framing_errors++;
        stats.bytes_dropped += rx_index;
        rx_reset();
        return;
    }

    /* CTR gap detection. We log the gap but still accept the
     * frame. The upper protocol layer decides how to react
     * (retry, abort, ignore).
     *
     * Wraparound from 0xFFFF to 0x0000 is a legal sequential
     * transition; uint16_t arithmetic handles it transparently.
     */
    if (rx_ctr_initialised) {
        const uint16_t expected = (uint16_t)(rx_last_ctr + 1u);
        if (ctr != expected) {
            stats.ctr_errors++;
        }
    } else {
        rx_ctr_initialised = true;
    }
    rx_last_ctr = ctr;

    rx_expected_payload_len = len;

    if (len == 0u) {
        /* Header-only frame: nothing more to receive. */
        rx_state = SXI_STATE_FRAME_READY;
    } else {
        rx_state = SXI_STATE_WAIT_PAYLOAD;
    }
}

/**
 * Check the inter-byte idle timeout. If the parser is mid-frame
 * and no bytes have arrived recently, recover by resetting.
 */
static void check_idle_timeout(void)
{
    if (rx_state == SXI_STATE_WAIT_HEADER && rx_index == 0u) {
        /* Nothing in flight; no timeout to check. */
        return;
    }
    if (rx_state == SXI_STATE_FRAME_READY) {
        /* Waiting on the consumer, not on the wire. */
        return;
    }

    const uint32_t now = time_us_32();
    if ((now - rx_last_byte_time_us) > XCP_SXI_IDLE_TIMEOUT_US) {
        stats.idle_timeouts++;
        stats.bytes_dropped += rx_index;
        rx_reset();
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void xcp_transport_sxi_init(void)
{
    rx_reset();
    rx_last_ctr = 0;
    rx_ctr_initialised = false;
    rx_last_byte_time_us = 0;

    tx_ctr = 0;

    xcp_transport_sxi_reset_stats();
}

void xcp_transport_sxi_task(void)
{
    /* Always check the timeout first, even if no new bytes arrive.
     * This recovers from a stalled transmission where the master
     * abandoned the frame mid-stream.
     */
    check_idle_timeout();

    /* If a complete frame is already waiting for the consumer,
     * don't read more bytes from the USB layer. This is the
     * back-pressure mechanism: bytes accumulate in the tinyusb
     * RX buffer until the consumer drains the current frame.
     */
    if (rx_state == SXI_STATE_FRAME_READY) {
        return;
    }

    /* Decide how many bytes we need right now. We pull exactly
     * what's missing for the current state; this keeps the
     * parser logic trivial (no leftover-byte handling between
     * state transitions).
     */
    size_t bytes_needed;
    if (rx_state == SXI_STATE_WAIT_HEADER) {
        bytes_needed = SXI_HEADER_SIZE - rx_index;
    } else {
        /* WAIT_PAYLOAD: rx_index counts total bytes received
         * (header + payload so far). Remaining payload bytes
         * are (header + expected payload) minus what we have.
         */
        bytes_needed = (SXI_HEADER_SIZE + rx_expected_payload_len) - rx_index;
    }

    const size_t received = xcp_transport_usb_receive(&rx_buffer[rx_index],
                                                      bytes_needed);
    if (received == 0u) {
        return;
    }

    rx_index += received;
    rx_last_byte_time_us = time_us_32();

    /* State transition checks. */
    if (rx_state == SXI_STATE_WAIT_HEADER &&
        rx_index >= SXI_HEADER_SIZE) {
        handle_header_complete();
        /* handle_header_complete() may have transitioned us to
         * WAIT_PAYLOAD, FRAME_READY, or back to WAIT_HEADER on
         * error. We do NOT continue parsing in this task() call;
         * the next call will pick up where we left off.
         */
        return;
    }

    if (rx_state == SXI_STATE_WAIT_PAYLOAD &&
        rx_index >= (SXI_HEADER_SIZE + rx_expected_payload_len)) {
        rx_state = SXI_STATE_FRAME_READY;
    }
}

bool xcp_transport_sxi_packet_available(void)
{
    return (rx_state == SXI_STATE_FRAME_READY);
}

xcp_sxi_status_t xcp_transport_sxi_get_packet(uint8_t *buffer,
                                              size_t max_len,
                                              size_t *actual_len)
{
    if (buffer == NULL || actual_len == NULL) {
        return XCP_SXI_ERR_BUFFER_TOO_SMALL;
    }

    if (rx_state != SXI_STATE_FRAME_READY) {
        return XCP_SXI_ERR_NO_PACKET;
    }

    if (max_len < rx_expected_payload_len) {
        return XCP_SXI_ERR_BUFFER_TOO_SMALL;
    }

    /* Copy payload only, stripping the 4-byte SxI header. */
    memcpy(buffer, &rx_buffer[SXI_HEADER_SIZE], rx_expected_payload_len);
    *actual_len = rx_expected_payload_len;

    stats.frames_received++;

    /* Frame consumed; reset state machine for the next one. */
    rx_reset();

    return XCP_SXI_OK;
}

xcp_sxi_status_t xcp_transport_sxi_send_packet(const uint8_t *data, size_t len)
{
    if (data == NULL && len > 0u) {
        return XCP_SXI_ERR_PACKET_TOO_LARGE;
    }

    if (len > XCP_MAX_DTO) {
        return XCP_SXI_ERR_PACKET_TOO_LARGE;
    }

    if (!xcp_transport_usb_is_connected()) {
        return XCP_SXI_ERR_NOT_CONNECTED;
    }

    /* Assemble the frame in a local buffer so the SxI header and the
     * payload reach the USB layer as a single write. Splitting the
     * write would risk a partial frame on the wire if usb_send()
     * accepted the header but not the payload.
     */
    uint8_t tx_frame[XCP_SXI_MAX_FRAME];

    /* LEN (little-endian) */
    tx_frame[0] = (uint8_t)(len & 0xFFu);
    tx_frame[1] = (uint8_t)((len >> 8) & 0xFFu);

    /* CTR (little-endian) */
    tx_frame[2] = (uint8_t)(tx_ctr & 0xFFu);
    tx_frame[3] = (uint8_t)((tx_ctr >> 8) & 0xFFu);

    /* Payload */
    if (len > 0u) {
        memcpy(&tx_frame[SXI_HEADER_SIZE], data, len);
    }

    const size_t total = SXI_HEADER_SIZE + len;
    const size_t sent  = xcp_transport_usb_send(tx_frame, total);

    if (sent != total) {
        /* Partial write: USB ring buffer was full. We do not retry
         * here; the caller can decide whether to back off or drop.
         * tx_ctr is NOT incremented so the next call retries with
         * the same counter value (the master would have detected
         * the gap anyway).
         */
        return XCP_SXI_ERR_NOT_CONNECTED;
    }

    tx_ctr++;
    stats.frames_sent++;

    return XCP_SXI_OK;
}

void xcp_transport_sxi_get_stats(xcp_sxi_stats_t *stats_out)
{
    if (stats_out != NULL) {
        memcpy(stats_out, &stats, sizeof(xcp_sxi_stats_t));
    }
}

void xcp_transport_sxi_reset_stats(void)
{
    memset(&stats, 0, sizeof(stats));
}