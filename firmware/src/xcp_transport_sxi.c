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
    /* Stub: implementation in stage 2 */
    (void)0;
}

bool xcp_transport_sxi_packet_available(void)
{
    return (rx_state == SXI_STATE_FRAME_READY);
}

xcp_sxi_status_t xcp_transport_sxi_get_packet(uint8_t *buffer,
                                              size_t max_len,
                                              size_t *actual_len)
{
    /* Stub: implementation in stage 2 */
    (void)buffer;
    (void)max_len;
    (void)actual_len;
    return XCP_SXI_ERR_NO_PACKET;
}

xcp_sxi_status_t xcp_transport_sxi_send_packet(const uint8_t *data, size_t len)
{
    /* Stub: implementation in stage 2 */
    (void)data;
    (void)len;
    return XCP_SXI_ERR_NOT_CONNECTED;
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