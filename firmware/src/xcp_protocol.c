/**
 * @file xcp_protocol.c
 * @brief XCP slave protocol layer implementation.
 */

#include "xcp_protocol.h"
#include "xcp_config.h"
#include <string.h>

/* -------------------------------------------------------------------------
 * Transport interface
 * ------------------------------------------------------------------------- */
 
static const xcp_transport_ops_t *s_transport;

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
    (void)s_transport->send_packet(resp, len);
}

static void send_negative_response(uint8_t err_code)
{
    uint8_t resp[2];
    resp[0] = XCP_PID_NEGATIVE_RESPONSE;
    resp[1] = err_code;
    (void)s_transport->send_packet(resp, sizeof(resp));
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

    (void)s_transport->send_packet(resp, sizeof(resp));
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
#define CMD_TABLE_LAST  (XCP_CMD_DOWNLOAD)   /* 0xF0 */
#define CMD_TABLE_SIZE  (CMD_TABLE_FIRST - CMD_TABLE_LAST + 1u)  /* 12 */

/* DISCONNECT (0xFE) */
static void handle_disconnect(const uint8_t *cmd, size_t cmd_len)
{
    (void)cmd;
    (void)cmd_len;
    s_session.connected = false;
    uint8_t resp[1] = { XCP_PID_POSITIVE_RESPONSE };
    (void)s_transport->send_packet(resp, sizeof(resp));
}

/* GET_STATUS (0xFD)
 *
 * ASAM MCD-1 XCP V1.5 Part 1, Section 3.4
 */
static void handle_get_status(const uint8_t *cmd, size_t cmd_len)
{
    (void)cmd;
    (void)cmd_len;
    uint8_t resp[6];
    resp[0] = XCP_PID_POSITIVE_RESPONSE;
    resp[1] = s_session.session_status;
    resp[2] = 0x00u;  /* RESOURCE_PROTECTION: nothing locked */
    resp[3] = 0x00u;  /* reserved */
    resp[4] = 0x00u;  /* SESSION_CONFIGURATION_ID low */
    resp[5] = 0x00u;  /* SESSION_CONFIGURATION_ID high */
    (void)s_transport->send_packet(resp, sizeof(resp));
}

/* SYNCH (0xFC)
 *
 * Always returns ERR_CMD_SYNCH regardless of session state.
 * The negative response itself signals to the master that the slave
 * is alive and ready to receive the next command.
 *
 * ASAM MCD-1 XCP V1.5 Part 1, Section 3.4
 */
static void handle_synch(const uint8_t *cmd, size_t cmd_len)
{
    (void)cmd;
    (void)cmd_len;
    send_negative_response(XCP_ERR_CMD_SYNCH);
}

/* GET_COMM_MODE_INFO (0xFB)
 *
 * ASAM MCD-1 XCP V1.5 Part 1, Section 3.4
 */
static void handle_get_comm_mode_info(const uint8_t *cmd, size_t cmd_len)
{
    (void)cmd;
    (void)cmd_len;
    uint8_t resp[8];
    resp[0] = XCP_PID_POSITIVE_RESPONSE;
    resp[1] = 0x00u;  /* reserved */
    resp[2] = 0x00u;  /* COMM_MODE_OPTIONAL: no block mode, no interleaved */
    resp[3] = 0x00u;  /* reserved */
    resp[4] = 0x00u;  /* MAX_BS */
    resp[5] = 0x00u;  /* MIN_ST */
    resp[6] = 0x00u;  /* QUEUE_SIZE */
    resp[7] = 0x01u;  /* XCP_DRIVER_VERSION */
    (void)s_transport->send_packet(resp, sizeof(resp));
}

/* GET_ID (0xFA)
 *
 * Returns station identification string inline (Mode bit 0 = 1).
 * "xcp-pico" is 8 bytes, well within MAX_CTO, so no subsequent
 * UPLOAD is required by the master.
 *
 * Response layout: [PID][Mode][0x00][0x00][LEN_0][LEN_1][LEN_2][LEN_3][string...]
 *
 * ASAM MCD-1 XCP V1.5 Part 1, Section 3.5.1
 */
static void handle_get_id(const uint8_t *cmd, size_t cmd_len)
{
    (void)cmd;
    (void)cmd_len;

    static const char station_id[] = XCP_STATION_ID;
    const size_t id_len = sizeof(station_id) - 1u;

    uint8_t resp[8u + 32u];
    resp[0] = XCP_PID_POSITIVE_RESPONSE;
    resp[1] = 0x01u;   /* Mode: identification data inline in this response */
    resp[2] = 0x00u;   /* reserved */
    resp[3] = 0x00u;   /* reserved */
    resp[4] = (uint8_t)(id_len & 0xFFu);
    resp[5] = (uint8_t)((id_len >> 8u)  & 0xFFu);
    resp[6] = 0x00u;
    resp[7] = 0x00u;
    memcpy(&resp[8], station_id, id_len);

    (void)s_transport->send_packet(resp, 8u + id_len);
}

/* -------------------------------------------------------------------------
 * Memory access policy: SRAM only (RP2350)
 * ------------------------------------------------------------------------- */
#define SRAM_BASE   (0x20000000u)
#define SRAM_END    (0x20082000u)  /* 520 KB */

static bool is_valid_ram_region(uint32_t addr, uint8_t num_bytes)
{
    return (addr >= SRAM_BASE) &&
           (num_bytes > 0u) &&
           ((addr + (uint32_t)num_bytes) <= SRAM_END);
}

/* SET_MTA (0xF6)
 *
 * Request:  [0xF6][0x00][0x00][addr_ext][addr_0][addr_1][addr_2][addr_3]
 * Response: [0xFF]
 *
 * ASAM MCD-1 XCP V1.5 Part 1, Section 3.6
 */
static void handle_set_mta(const uint8_t *cmd, size_t cmd_len)
{
    (void)cmd_len;
    s_session.mta_extension = cmd[3];
    s_session.mta_address   = read_u32_le(cmd, 4u);

    uint8_t resp[1] = { XCP_PID_POSITIVE_RESPONSE };
    (void)s_transport->send_packet(resp, sizeof(resp));
}

/* UPLOAD (0xF5)
 *
 * Reads num_bytes from the current MTA address. Advances MTA by num_bytes.
 * Access restricted to SRAM only.
 *
 * Request:  [0xF5][num_bytes]
 * Response: [0xFF][data...]
 *
 * ASAM MCD-1 XCP V1.5 Part 1, Section 3.6
 */
static void handle_upload(const uint8_t *cmd, size_t cmd_len)
{
    (void)cmd_len;
    const uint8_t num_bytes = cmd[1];

    if (!is_valid_ram_region(s_session.mta_address, num_bytes)) {
        send_negative_response(XCP_ERR_OUT_OF_RANGE);
        return;
    }

    uint8_t resp[1u + XCP_MAX_DTO];
    resp[0] = XCP_PID_POSITIVE_RESPONSE;
    memcpy(&resp[1], (const void *)s_session.mta_address, num_bytes);
    s_session.mta_address += (uint32_t)num_bytes;

    (void)s_transport->send_packet(resp, 1u + num_bytes);
}

/* SHORT_UPLOAD (0xF4)
 *
 * Reads num_bytes from the address given in the command. Does NOT modify MTA.
 * Access restricted to SRAM only.
 *
 * Request:  [0xF4][num_bytes][0x00][addr_ext][addr_0][addr_1][addr_2][addr_3]
 * Response: [0xFF][data...]
 *
 * ASAM MCD-1 XCP V1.5 Part 1, Section 3.6
 */
static void handle_short_upload(const uint8_t *cmd, size_t cmd_len)
{
    (void)cmd_len;
    const uint8_t  num_bytes = cmd[1];
    const uint32_t address   = read_u32_le(cmd, 4u);

    if (!is_valid_ram_region(address, num_bytes)) {
        send_negative_response(XCP_ERR_OUT_OF_RANGE);
        return;
    }

    uint8_t resp[1u + XCP_MAX_DTO];
    resp[0] = XCP_PID_POSITIVE_RESPONSE;
    memcpy(&resp[1], (const void *)address, num_bytes);

    (void)s_transport->send_packet(resp, 1u + num_bytes);
}

/* DOWNLOAD (0xF0)
 *
 * Writes num_bytes to the current MTA address. Advances MTA by num_bytes.
 * Access restricted to SRAM only. Byte granularity (AG=1): no alignment
 * bytes between SIZE and data fields.
 *
 * Request:  [0xF0][num_bytes][data_0][data_1]...[data_n]
 * Response: [0xFF]
 *
 * ASAM MCD-1 XCP V1.5 Part 1, Section 3.6
 */
static void handle_download(const uint8_t *cmd, size_t cmd_len)
{
    const uint8_t num_bytes = cmd[1];
 
    if (num_bytes == 0u || cmd_len < (size_t)(2u + (size_t)num_bytes)) {
        send_negative_response(XCP_ERR_OUT_OF_RANGE);
        return;
    }
 
    if (!is_valid_ram_region(s_session.mta_address, num_bytes)) {
        send_negative_response(XCP_ERR_OUT_OF_RANGE);
        return;
    }
 
    memcpy((void *)s_session.mta_address, &cmd[2], num_bytes);
    s_session.mta_address += (uint32_t)num_bytes;
 
    uint8_t resp[1] = { XCP_PID_POSITIVE_RESPONSE };
    (void)s_transport->send_packet(resp, sizeof(resp));
}

static const xcp_cmd_handler_t s_cmd_table[CMD_TABLE_SIZE] = {
    handle_connect,            /* 0xFF CONNECT            index 0  */
    handle_disconnect,         /* 0xFE DISCONNECT         index 1  */
    handle_get_status,         /* 0xFD GET_STATUS         index 2  */
    handle_synch,              /* 0xFC SYNCH              index 3  */
    handle_get_comm_mode_info, /* 0xFB GET_COMM_MODE_INFO index 4  */
    handle_get_id,             /* 0xFA GET_ID             index 5  */
    handle_unknown,            /* 0xF9 (reserved)         index 6  */
    handle_unknown,            /* 0xF8 (reserved)         index 7  */
    handle_unknown,            /* 0xF7 (reserved)         index 8  */
    handle_set_mta,            /* 0xF6 SET_MTA            index 9  */
    handle_upload,             /* 0xF5 UPLOAD             index 10 */
    handle_short_upload,       /* 0xF4 SHORT_UPLOAD       index 11 */
    handle_unknown,            /* 0xF3 BUILD_CHECKSUM     index 12 */
    handle_unknown,            /* 0xF2 TRANSPORT_LAYER    index 13 */
    handle_unknown,            /* 0xF1 USER_CMD           index 14 */
    handle_download,           /* 0xF0 DOWNLOAD           index 15 */
};
/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void xcp_protocol_init(const xcp_transport_ops_t *ops)
{
    s_transport = ops;
    memset(&s_session, 0, sizeof(s_session));
}

void xcp_protocol_task(void)
{
    if (!s_transport->packet_available()) {
        return;
    }
 
    static uint8_t s_cmd_buf[XCP_MAX_CTO];
    size_t cmd_len = 0u;
 
    if (!s_transport->get_packet(s_cmd_buf, sizeof(s_cmd_buf), &cmd_len) ||
        cmd_len == 0u) {
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