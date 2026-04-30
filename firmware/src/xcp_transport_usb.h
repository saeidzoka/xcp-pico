/**
 * @file    xcp_transport_usb.h
 * @brief   USB CDC byte-stream transport for the xcp-pico stack.
 *
 * This module owns the USB CDC interface and exposes a transport-agnostic
 * byte-stream API to the layer above. It does NOT implement XCP framing;
 * SxI framing is the responsibility of a separate module that sits between
 * this transport and the protocol layer (see ADR-001).
 *
 * Layering:
 *
 *      Protocol Layer
 *           |
 *           v
 *      xcp_transport_sxi   (SxI framing, added in Milestone 2)
 *           |
 *           v
 *      xcp_transport_usb   (this module: byte stream over USB CDC)
 *           |
 *           v
 *      tinyusb / RP2350 USB hardware
 *
 * The transport runs on the same core as the protocol layer (Core 0 in the
 * planned concurrency model). All functions in this API are intended to be
 * called from a single thread of execution.
 */

#ifndef XCP_TRANSPORT_USB_H
#define XCP_TRANSPORT_USB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the USB CDC transport.
 *
 * Must be called once at startup, before any other transport function.
 * After this call returns, the device begins enumerating on the USB bus,
 * but the host may not yet be connected. Use xcp_transport_usb_is_connected()
 * to check the link state before sending.
 */
void xcp_transport_usb_init(void);

/**
 * @brief Service the USB stack.
 *
 * tinyusb is a cooperative stack: it requires periodic servicing to handle
 * enumeration, IN/OUT transfers, and CDC buffering. Call this from the main
 * loop as often as practical (typical: every iteration).
 */
void xcp_transport_usb_task(void);

/**
 * @brief Check whether a USB CDC host is currently attached.
 *
 * @return true if a host has opened the CDC interface, false otherwise.
 */
bool xcp_transport_usb_is_connected(void);

/**
 * @brief Send a block of bytes to the host.
 *
 * The bytes are queued in the CDC TX FIFO. Actual transmission happens
 * asynchronously inside xcp_transport_usb_task().
 *
 * @param data  Pointer to the bytes to send. Must not be NULL.
 * @param len   Number of bytes to send.
 * @return Number of bytes accepted into the TX FIFO. May be less than len
 *         if the FIFO is full; the caller should retry on subsequent calls.
 */
size_t xcp_transport_usb_send(const uint8_t *data, size_t len);

/**
 * @brief Receive available bytes from the host.
 *
 * Non-blocking. Reads up to max_len bytes from the CDC RX FIFO into buffer.
 *
 * @param buffer    Destination buffer. Must not be NULL.
 * @param max_len   Maximum number of bytes to read.
 * @return Number of bytes actually read. Zero if no bytes are available.
 */
size_t xcp_transport_usb_receive(uint8_t *buffer, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* XCP_TRANSPORT_USB_H */