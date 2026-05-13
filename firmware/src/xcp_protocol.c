/**
 * @file xcp_protocol.c
 * @brief XCP slave protocol layer implementation.
 */

#include "xcp_protocol.h"
#include "xcp_transport_sxi.h"
#include "xcp_config.h"
#include <string.h>

/* -------------------------------------------------------------------------
 * Session State
 * ------------------------------------------------------------------------- */

typedef struct {
    bool     connected;
    uint8_t  session_status;
    uint32_t mta_address;
    uint8_t  mta_extension;
} xcp_session_t;

static xcp_session_t s_session;

/* -------------------------------------------------------------------------
 * Response helpers
 * ------------------------------------------------------------------------- */

static inline uint16_t read_u16_le(const uint8_t *buf, size_t offset)
{
    return (uint16_t)(buf[offset] | ((uint16_t)buf[offset + 1u] << 8u));
}

static inline uint32_t read_u32_le(const uint8_t *buf, size_t offset)
{
    return (uint32_t)( buf[offset]
                     | ((uint32_t)buf[offset + 1u] << 8u)
                     | ((uint32_t)buf[offset + 2u] << 16u)
                     | ((uint32_t)buf[offset + 3u] << 24u));
}

static void send_positive_response(uint8_t *resp, size_t len)
{
    resp[0] = XCP_PID_POSITIVE_RESPONSE;
    (void)xcp_transport_sxi_send_packet(resp, len);
}

static void send_negative_response(uint8_t err_code)
{
    uint8_t resp[2];
    resp[0] = XCP_PID_NEGATIVE_RESPONSE;
    resp[1] = err_code;
    (void)xcp_transport_sxi_send_packet(resp, sizeof(resp));
}

/* -------------------------------------------------------------------------
 * Command handlers
 * ------------------------------------------------------------------------- */

typedef void (*xcp_cmd_handler_t)(const uint8_t *cmd, size_t cmd_len);

/* CONNECT (0xFF)
 *
 * Request:  [0xFF][MODE]
 * Response: [0xFF][RESOURCE][COMM_MODE_BASIC][MAX_CTO][MAX_DTO_LO][MAX_DTO_HI]
 *           [PROTOCOL_VERSION][TRANSPORT_VERSION]
 *
 * ASAM MCD-1 XCP V1.5 Part 1, Section 3.4
 */
#define CONNECT_RESOURCE        (0x01u)  /* CAL/PAG only */
#define CONNECT_COMM_MODE_BASIC (0x00u)  /* little-endian, byte granularity, no block mode */
#define XCP_PROTOCOL_VERSION    (0x01u)
#define XCP_TRANSPORT_VERSION   (0x01u)

static void handle_connect(const uint8_t *cmd, size_t cmd_len)
{
    (void)cmd;
    (void)cmd_len;

    uint8_t resp[8];
    resp[0] = XCP_PID_POSITIVE_RESPONSE;
    resp[1] = CONNECT_RESOURCE;
    resp[2] = CONNECT_COMM_MODE_BASIC;
    resp[3] = (uint8_t)XCP_MAX_CTO;
    resp[4] = (uint8_t)(XCP_MAX_DTO & 0xFFu);
    resp[5] = (uint8_t)(XCP_MAX_DTO >> 8u);
    resp[6] = XCP_PROTOCOL_VERSION;
    resp[7] = XCP_TRANSPORT_VERSION;

    s_session.connected      = true;
    s_session.session_status = 0x00u;

    (void)xcp_transport_sxi_send_packet(resp, sizeof(resp));
}

static void handle_unknown(const uint8_t *cmd, size_t cmd_len)
{
    (void)cmd;
    (void)cmd_len;
    send_negative_response(XCP_ERR_CMD_UNKNOWN);
}

/* -------------------------------------------------------------------------
 * Dispatch table
 *
 * Command codes run from 0xFF downward. Table index = 0xFF - cmd_code.
 * Entries for unimplemented commands point to handle_unknown.
 *
 * ASAM MCD-1 XCP V1.5 Part 1, Table 3
 * ------------------------------------------------------------------------- */
#define CMD_TABLE_FIRST (0xFFu)
#define CMD_TABLE_LAST  (XCP_CMD_SHORT_UPLOAD)   /* 0xF4 */
#define CMD_TABLE_SIZE  (CMD_TABLE_FIRST - CMD_TABLE_LAST + 1u)  /* 12 */

static const xcp_cmd_handler_t s_cmd_table[CMD_TABLE_SIZE] = {
    handle_connect,     /* 0xFF CONNECT            index 0  */
    handle_unknown,     /* 0xFE DISCONNECT         index 1  (stub) */
    handle_unknown,     /* 0xFD GET_STATUS         index 2  (stub) */
    handle_unknown,     /* 0xFC SYNCH              index 3  (stub) */
    handle_unknown,     /* 0xFB GET_COMM_MODE_INFO index 4  (stub) */
    handle_unknown,     /* 0xFA GET_ID             index 5  (stub) */
    handle_unknown,     /* 0xF9 (reserved)         index 6  */
    handle_unknown,     /* 0xF8 (reserved)         index 7  */
    handle_unknown,     /* 0xF7 (reserved)         index 8  */
    handle_unknown,     /* 0xF6 SET_MTA            index 9  (stub) */
    handle_unknown,     /* 0xF5 UPLOAD             index 10 (stub) */
    handle_unknown,     /* 0xF4 SHORT_UPLOAD       index 11 (stub) */
};

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void xcp_protocol_init(void)
{
    memset(&s_session, 0, sizeof(s_session));
}

void xcp_protocol_task(void)
{
    if (!xcp_transport_sxi_packet_available()) {
        return;
    }

    static uint8_t s_cmd_buf[XCP_MAX_CTO];
    size_t cmd_len = 0u;

    const xcp_sxi_status_t status =
        xcp_transport_sxi_get_packet(s_cmd_buf, sizeof(s_cmd_buf), &cmd_len);

    if (status != XCP_SXI_OK || cmd_len == 0u) {
        return;
    }

    const uint8_t cmd_code = s_cmd_buf[0];

    if (cmd_code < CMD_TABLE_LAST) {
        send_negative_response(XCP_ERR_CMD_UNKNOWN);
        return;
    }

    const uint8_t index = CMD_TABLE_FIRST - cmd_code;
    s_cmd_table[index](s_cmd_buf, cmd_len);
}

bool xcp_protocol_is_connected(void)
{
    return s_session.connected;
}