/**
 * @file    xcp_config.h
 * @brief   Compile-time configuration for the xcp-pico XCP slave.
 *
 * This header centralises all build-time tunables for the XCP stack. It is
 * the single point of change for sizing, transport selection, and slave
 * identity. Runtime state lives elsewhere; only constants belong here.
 *
 * The split between "pre-compile time configuration" and runtime state
 * follows the convention used by production automotive stacks such as
 * AUTOSAR (where *_Cfg.h files serve the same role).
 *
 * Reference: ASAM XCP V1.5 Part 1 (Overview) and Part 2 (Protocol Layer
 * Specification).
 */

#ifndef XCP_CONFIG_H
#define XCP_CONFIG_H

#include <stdint.h>

/* ===========================================================================
 * Slave identity
 * =========================================================================*/

/** Human-readable identifier returned in response to GET_ID (mode 1).
 *  ASAM XCP V1.5 Part 2, Section 1.1.5: STATION_ID.
 *  Length is bounded; keep this string short.
 */
#define XCP_STATION_ID          "xcp-pico"
#define XCP_STATION_ID_LEN      (sizeof(XCP_STATION_ID) - 1U)

/* ===========================================================================
 * Protocol Layer sizing
 * =========================================================================*/

/** Maximum CTO (Command Transfer Object) size in bytes.
 *  This is the largest packet the slave will accept from the master.
 *  The standard minimum is 8 (CAN-style transports). Byte-stream transports
 *  can carry larger CTOs; we pick a value large enough for typical
 *  measurement and calibration commands without wasting RAM.
 *
 *  ASAM XCP V1.5 Part 2, Section 1.1.1.
 */
#define XCP_MAX_CTO             64U

/** Maximum DTO (Data Transfer Object) size in bytes.
 *  This is the largest packet the slave will emit in response to commands
 *  or as DAQ traffic. For Phase 1 (no DAQ), this matches MAX_CTO.
 *
 *  ASAM XCP V1.5 Part 2, Section 1.1.1.
 */
#define XCP_MAX_DTO             64U

/* ===========================================================================
 * Transport Layer selection
 * =========================================================================*/

/** Set exactly one of the XCP_TRANSPORT_* flags to 1.
 *  Phase 1 uses USB CDC carrying SxI framing (see ADR-001).
 *  Phase 2 will introduce CAN via MCP2515.
 */
#define XCP_TRANSPORT_USB_CDC   1
#define XCP_TRANSPORT_CAN       0

#if (XCP_TRANSPORT_USB_CDC + XCP_TRANSPORT_CAN) != 1
#  error "Exactly one XCP transport must be selected in xcp_config.h"
#endif

/* ===========================================================================
 * Transport Layer sizing (SxI framing over USB CDC)
 * =========================================================================*/

#if XCP_TRANSPORT_USB_CDC

/** SxI frame header: 2-byte LEN field + 2-byte CTR field.
 *  ASAM XCP V1.5 Part 3, Section 4 (XCP on SxI).
 */
#  define XCP_SXI_HEADER_SIZE   4U

/** Maximum SxI frame size: header plus the largest XCP packet we accept.
 *  The transport-layer receive buffer is sized to this.
 */
#  define XCP_SXI_MAX_FRAME     (XCP_SXI_HEADER_SIZE + XCP_MAX_CTO)

#endif /* XCP_TRANSPORT_USB_CDC */

#endif /* XCP_CONFIG_H */