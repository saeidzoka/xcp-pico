/**
 * @file xcp_protocol.h
 * @brief XCP slave protocol layer.
 *
 * Implements the XCP command processor on top of the SxI transport layer.
 * Receives complete XCP packets via xcp_transport_sxi, dispatches based on
 * the command code (first byte), and sends responses back via
 * xcp_transport_sxi_send_packet().
 *
 * Supported commands (ASAM MCD-1 XCP V1.5, Part 1):
 *   CONNECT         (0xFF)
 *   DISCONNECT      (0xFE)
 *   GET_STATUS      (0xFD)
 *   SYNCH           (0xFC)
 *   GET_COMM_MODE_INFO (0xFB)
 *   GET_ID          (0xFA)
 *   SET_MTA         (0xF6)
 *   UPLOAD          (0xF5)
 *   SHORT_UPLOAD    (0xF4)
 *
 * Memory access policy: READ access is restricted to SRAM only
 * (0x20000000 - 0x20082000 on RP2350). Peripheral and Flash addresses
 * are rejected with ERR_OUT_OF_RANGE.
 */

#ifndef XCP_PROTOCOL_H
#define XCP_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * XCP Command Codes (ASAM MCD-1 XCP V1.5, Part 1, Table 3)
 * ------------------------------------------------------------------------- */
#define XCP_CMD_CONNECT             (0xFFu)
#define XCP_CMD_DISCONNECT          (0xFEu)
#define XCP_CMD_GET_STATUS          (0xFDu)
#define XCP_CMD_SYNCH               (0xFCu)
#define XCP_CMD_GET_COMM_MODE_INFO  (0xFBu)
#define XCP_CMD_GET_ID              (0xFAu)
#define XCP_CMD_SET_MTA             (0xF6u)
#define XCP_CMD_UPLOAD              (0xF5u)
#define XCP_CMD_SHORT_UPLOAD        (0xF4u)

/* -------------------------------------------------------------------------
 * XCP Response Packet Identification Bytes (ASAM MCD-1 XCP V1.5, Part 1, Section 3.3)
 * ------------------------------------------------------------------------- */
#define XCP_PID_POSITIVE_RESPONSE   (0xFFu)
#define XCP_PID_NEGATIVE_RESPONSE   (0xFEu)

/* -------------------------------------------------------------------------
 * XCP Error Codes (ASAM MCD-1 XCP V1.5, Part 1, Table 4)
 * ------------------------------------------------------------------------- */
#define XCP_ERR_CMD_SYNCH           (0x00u)
#define XCP_ERR_CMD_BUSY            (0x10u)
#define XCP_ERR_CMD_UNKNOWN         (0x20u)
#define XCP_ERR_OUT_OF_RANGE        (0x22u)
#define XCP_ERR_ACCESS_DENIED       (0x24u)

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * @brief Initialise the XCP protocol layer.
 *
 * Resets session state to disconnected. Must be called once after
 * xcp_transport_sxi_init().
 */
void xcp_protocol_init(void);

/**
 * @brief Process pending XCP commands.
 *
 * Must be called repeatedly from the main loop. Drains one packet per call
 * from the SxI layer, dispatches the command, and sends the response.
 */
void xcp_protocol_task(void);

/**
 * @brief Returns true if an XCP session is currently active.
 */
bool xcp_protocol_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* XCP_PROTOCOL_H */