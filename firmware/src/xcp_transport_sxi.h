/**
 * @file    xcp_transport_sxi.h
 * @brief   XCP on SxI transport layer for xcp-pico.
 *
 * This module implements the SxI framing layer as specified in
 * ASAM XCP V1.5 Part 2 (Transport Layer Specification, Section 3).
 *
 * Frame format: [LEN(2)][CTR(2)][packet(LEN bytes)]
 *   LEN: little-endian, payload length in bytes (excludes header)
 *   CTR: little-endian, monotonic counter for loss detection
 *
 * The module sits on top of xcp_transport_usb (byte stream) and
 * exposes a packet-oriented API to the upper protocol layer.
 *
 * Threading: this module is NOT thread-safe. All functions must be
 * called from the same context (typically the main loop).
 */

#ifndef XCP_TRANSPORT_SXI_H
#define XCP_TRANSPORT_SXI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Status codes returned by the SxI transport API.
 */
typedef enum {
    XCP_SXI_OK = 0,
    XCP_SXI_ERR_NO_PACKET,          /**< No complete packet available */
    XCP_SXI_ERR_BUFFER_TOO_SMALL,   /**< Caller buffer cannot hold packet */
    XCP_SXI_ERR_NOT_CONNECTED,      /**< USB CDC not connected */
    XCP_SXI_ERR_PACKET_TOO_LARGE,   /**< Packet exceeds XCP_MAX_CTO */
} xcp_sxi_status_t;

/**
 * @brief Diagnostic counters for the SxI transport layer.
 *
 * All counters are monotonic and only reset by xcp_transport_sxi_reset_stats().
 */
typedef struct {
    uint32_t frames_received;   /**< Complete frames delivered to upper layer */
    uint32_t frames_sent;       /**< Frames successfully written to USB */
    uint32_t framing_errors;    /**< Invalid LEN field detected */
    uint32_t ctr_errors;        /**< Non-sequential CTR detected on RX */
    uint32_t idle_timeouts;     /**< State machine reset due to inter-byte timeout */
    uint32_t bytes_dropped;     /**< Bytes discarded during error recovery */
} xcp_sxi_stats_t;

/**
 * @brief Initialise the SxI transport layer.
 *
 * Resets the receive state machine, clears statistics, and zeroes the
 * outgoing CTR. Must be called once at startup, after xcp_transport_usb_init().
 */
void xcp_transport_sxi_init(void);

/**
 * @brief Pump bytes from the USB layer through the SxI parser.
 *
 * Must be called periodically from the main loop. Reads available bytes
 * from xcp_transport_usb_receive() and feeds them into the receive state
 * machine. Also performs idle timeout detection.
 */
void xcp_transport_sxi_task(void);

/**
 * @brief Check whether a complete packet is available for retrieval.
 *
 * @return true if a packet is ready, false otherwise.
 */
bool xcp_transport_sxi_packet_available(void);

/**
 * @brief Retrieve the most recently received packet.
 *
 * Copies the packet payload (without SxI header) into the caller's buffer.
 * Once retrieved, the receive state machine is ready for the next frame.
 *
 * @param[out]  buffer      Destination buffer for packet payload.
 * @param[in]   max_len     Size of the destination buffer.
 * @param[out]  actual_len  Number of bytes written to buffer.
 *
 * @return XCP_SXI_OK on success.
 *         XCP_SXI_ERR_NO_PACKET if no packet is ready.
 *         XCP_SXI_ERR_BUFFER_TOO_SMALL if max_len is insufficient.
 */
xcp_sxi_status_t xcp_transport_sxi_get_packet(uint8_t *buffer,
                                              size_t max_len,
                                              size_t *actual_len);

/**
 * @brief Send an XCP packet wrapped in an SxI frame.
 *
 * Prepends [LEN][CTR] header (CTR auto-incremented) and writes the
 * complete frame via xcp_transport_usb_send().
 *
 * @param[in]  data  Packet payload to send.
 * @param[in]  len   Number of bytes to send.
 *
 * @return XCP_SXI_OK on success.
 *         XCP_SXI_ERR_NOT_CONNECTED if USB CDC is not connected.
 *         XCP_SXI_ERR_PACKET_TOO_LARGE if len > XCP_MAX_DTO.
 */
xcp_sxi_status_t xcp_transport_sxi_send_packet(const uint8_t *data, size_t len);

/**
 * @brief Copy diagnostic counters to a caller-provided struct.
 *
 * @param[out]  stats  Destination struct.
 */
void xcp_transport_sxi_get_stats(xcp_sxi_stats_t *stats);

/**
 * @brief Reset all diagnostic counters to zero.
 */
void xcp_transport_sxi_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* XCP_TRANSPORT_SXI_H */