/**
 * @file    xcp_transport.h
 * @brief   XCP transport layer interface (vtable).
 *
 * Defines the xcp_transport_ops_t interface struct that decouples the XCP
 * protocol layer from any specific transport implementation.
 *
 * The protocol layer holds a pointer to one of these structs and calls all
 * transport operations through it. The concrete implementation (USB/SxI,
 * CAN, etc.) is selected once at startup in main.c and registered via
 * xcp_protocol_init().
 *
 * Transport selection at build time is controlled by xcp_config.h:
 *   XCP_TRANSPORT_USB_CDC  1  ->  SxI framing over USB CDC (Phase 1)
 *   XCP_TRANSPORT_CAN      1  ->  XCP on CAN via MCP2515   (Phase 2)
 *
 * ADR-002: Transport Abstraction Layer
 * Refs: ASAM MCD-1 XCP V1.5 Part 2, Section 1 (Transport Layer interface)
 */

#ifndef XCP_TRANSPORT_H
#define XCP_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Transport layer operations interface.
 *
 * All functions must be non-blocking and safe to call from the main loop.
 * No function may sleep or busy-wait. State management (connection tracking,
 * buffer ownership) is the responsibility of the implementing module.
 */
typedef struct {
    /**
     * @brief Returns true if a complete packet is available for retrieval.
     */
    bool (*packet_available)(void);

    /**
     * @brief Retrieve the next available packet.
     *
     * Copies the XCP packet payload (transport framing stripped) into buf.
     * Advances the transport's receive state so the next packet can arrive.
     *
     * @param[out]  buf         Destination buffer.
     * @param[in]   max_len     Capacity of buf in bytes.
     * @param[out]  actual_len  Number of bytes written.
     * @return true on success, false if no packet ready or buffer too small.
     */
    bool (*get_packet)(uint8_t *buf, size_t max_len, size_t *actual_len);

    /**
     * @brief Transmit one XCP packet.
     *
     * The transport is responsible for any framing (e.g. SxI header for
     * USB CDC, direct CAN frame for CAN). The caller passes raw XCP payload.
     *
     * @param[in]  data  Packet payload to transmit.
     * @param[in]  len   Length of payload in bytes.
     * @return true if the packet was fully accepted by the transport.
     */
    bool (*send_packet)(const uint8_t *data, size_t len);

    /**
     * @brief Returns true if the transport has an active connection.
     *
     * For USB CDC: true when the host has the port open.
     * For CAN: always true once the controller is initialised.
     */
    bool (*is_connected)(void);
} xcp_transport_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* XCP_TRANSPORT_H */