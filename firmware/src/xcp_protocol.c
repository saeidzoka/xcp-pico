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
 * Public API
 * ------------------------------------------------------------------------- */

void xcp_protocol_init(void)
{
    memset(&s_session, 0, sizeof(s_session));
}

void xcp_protocol_task(void)
{
    /* TODO: dispatch */
}

bool xcp_protocol_is_connected(void)
{
    return s_session.connected;
}