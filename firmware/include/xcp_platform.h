/**
 * @file    xcp_platform.h
 * @brief   Platform-specific memory facts for the XCP protocol layer.
 *
 * This header is the single point of change when porting xcp-pico's
 * protocol layer to a different microcontroller. It contains no logic,
 * only facts about the target's memory map that the protocol layer needs
 * to enforce its access policy (currently: RAM-only read/write access for
 * UPLOAD, SHORT_UPLOAD, and DOWNLOAD).
 *
 * xcp_protocol.c contains the *policy* (what counts as a valid access);
 * this file contains the *facts* (where RAM actually is on this chip).
 * Porting to new hardware means editing only this file, not
 * xcp_protocol.c.
 *
 * Current target: Raspberry Pi Pico 2 (RP2350), 520 KB SRAM.
 * Ref: RP2350 Datasheet, Section 2.4.1 (SRAM layout).
 *
 * To port to a different MCU, replace the two defines below with the
 * target's SRAM base address and end address (base + size). If the
 * target has multiple non-contiguous RAM regions, is_valid_ram_region()
 * in xcp_protocol.c must be extended to check all of them; this header
 * would then export one BASE/END pair per region.
 */

#ifndef XCP_PLATFORM_H
#define XCP_PLATFORM_H

/** Start address of writable/readable SRAM on this target. */
#define XCP_PLATFORM_RAM_BASE   (0x20000000u)

/** One past the last valid SRAM address (base + size).
 *  RP2350: 520 KB SRAM -> 0x20000000 + 0x00082000.
 */
#define XCP_PLATFORM_RAM_END    (0x20082000u)

#endif /* XCP_PLATFORM_H */