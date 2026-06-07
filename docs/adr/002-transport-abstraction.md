# ADR-002: Transport Abstraction Layer

**Status:** Accepted
**Date:** 2026-06-06
**Refs:** ASAM MCD-1 XCP V1.5 Part 2, Section 1; ADR-001

---

## Context

Phase 1 implemented the XCP protocol layer (`xcp_protocol.c`) directly on top
of the SxI framing module. All transport calls were hardcoded:

```c
xcp_transport_sxi_packet_available();
xcp_transport_sxi_get_packet(...);
xcp_transport_sxi_send_packet(...);
```

ADR-001 claimed a transport-agnostic architecture. With direct coupling to
`xcp_transport_sxi`, that claim was not realised in the code. Adding the CAN
transport in Phase 2 would have required modifying `xcp_protocol.c`, either
by scattering `#ifdef XCP_TRANSPORT_CAN` guards through the command dispatcher
or by duplicating send/receive logic.

The problem was not the `#ifdef` mechanism itself; compile-time transport
selection is standard practice in production embedded stacks (AUTOSAR uses
exactly this pattern in `*_Cfg.h` files). The problem was where the
conditionals would live: inside `xcp_protocol.c`, a module that should be
concerned only with XCP semantics.

---

## Decision

Introduce a transport interface struct (`xcp_transport_ops_t`) in
`include/xcp_transport.h`. The struct holds four function pointers:

```c
typedef struct {
    bool (*packet_available)(void);
    bool (*get_packet)(uint8_t *buf, size_t max_len, size_t *actual_len);
    bool (*send_packet)(const uint8_t *data, size_t len);
    bool (*is_connected)(void);
} xcp_transport_ops_t;
```

`xcp_protocol_init()` accepts a `const xcp_transport_ops_t *` and stores it
in a module-static pointer. All transport calls inside `xcp_protocol.c` go
through this pointer. `xcp_protocol.c` includes no transport-specific header.

Transport selection remains compile-time, controlled by `xcp_config.h`
(`XCP_TRANSPORT_USB_CDC` / `XCP_TRANSPORT_CAN`). The `#ifdef` logic lives
exclusively in `main.c`, which owns the concrete ops struct population and
passes the result to `xcp_protocol_init()`.

This gives compile-time selection (no runtime overhead, dead code
eliminated by the linker) while keeping `xcp_protocol.c` free of transport
knowledge.

---

## Alternatives Considered

**Option A: `#ifdef` inside `xcp_protocol.c`**
Simpler. No vtable, no function pointer indirection. Direct calls guarded by
`#ifdef XCP_TRANSPORT_CAN / #else`. Rejected because `xcp_protocol.c` would
need to include every transport header and grow a conditional branch for each
new transport. The separation-of-concerns claim in ADR-001 would remain
unimplemented in the code.

**Option B: Runtime vtable with no `#ifdef` anywhere**
The ops pointer could be registered by any caller at any time. Rejected as
over-engineering: this is a single-MCU application with one transport active
per build. Runtime swapping has no use case here and would complicate
initialisation ordering.

---

## Consequences

**Positive:**

- `xcp_protocol.c` includes only `xcp_protocol.h`, `xcp_config.h`, and
  `<string.h>`. It has no knowledge of USB, SxI, or CAN.
- Adding a new transport (Ethernet, UART) requires no changes to
  `xcp_protocol.c`. Only `main.c` and `xcp_config.h` are touched.
- Unit testing `xcp_protocol` without hardware is possible: pass a mock
  `xcp_transport_ops_t` populated with stub functions.
- ADR-001's transport-agnostic claim is now realised structurally, not just
  documented.

**Negative / trade-offs:**

- `main.c` requires two thin adapter functions (`sxi_get_packet`,
  `sxi_send_packet`) to reconcile the `xcp_sxi_status_t` return type of the
  SxI API with the `bool`-returning vtable interface. This is a consequence
  of not changing `xcp_transport_sxi`'s API, which was an explicit choice to
  keep this commit a pure refactor with no functional change.
- One level of function pointer indirection is added to every packet send and
  receive. On Cortex-M33 at typical clock rates this is negligible, but it
  is not zero.

---

## Transport Implementations

| Transport | `packet_available` | `get_packet` | `send_packet` | `is_connected` |
|---|---|---|---|---|
| USB/SxI (Phase 1) | `xcp_transport_sxi_packet_available` | `sxi_get_packet` (adapter) | `sxi_send_packet` (adapter) | `xcp_transport_usb_is_connected` |
| CAN/MCP2515 (Phase 2) | `xcp_transport_can_packet_available` | `xcp_transport_can_get_packet` | `xcp_transport_can_send_packet` | `xcp_transport_can_is_connected` |

Note: `is_connected` for the CAN transport will always return `true` after
initialisation. CAN has no session-layer connection concept equivalent to
USB CDC's host-port-open state.