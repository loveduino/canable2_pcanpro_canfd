#pragma once

/*
 * MKS CANable V2.0 hardware variant (STM32G431CBU6)
 *
 * Pinout verified against "MKS CANable V2.0_001 schematic.pdf" (2023-3-24):
 *   FDCAN1 RX  : PB8  (AF9)  <- TJA1051T/3 RXD, net "CAN_RX/BOOT0",
 *                               R6 10K pull-up to 3.3V (nBOOT0 option byte
 *                               MUST force flash boot, see pcan_option_bytes_config)
 *   FDCAN1 TX  : PB9  (AF9)  -> TJA1051T/3 TXD, net "CAN_TX"
 *   Status LED : PA15 -> physically the GREEN LED  (measured: PA15 toggled at
 *                        150 ms showed up as the green LED blinking fast)
 *   Work LED   : PA0  -> physically the BLUE LED    (measured: PA0 toggled at
 *                        800 ms showed up as the blue LED blinking slowly)
 *                Both are ACTIVE LOW (pull the pin low to light the LED).
 *
 *   NOTE: this is the OPPOSITE of the official canable2-fw documentation
 *         (which states D2/PA15 = blue, D3/PA0 = green). The 2026-10-09
 *         bare-metal GPIO test on this specific board proves the mapping
 *         above; do not "fix" it back.
 *
 *   PA15 is not blocked by JTAG (JTDI): writing MODER=01 + clearing AFRH[15]
 *   gives full software control. Verified by the same test.
 *   Power LED  : red D1 (3.3V -> R8 1K -> GND, always on, not wired to MCU)
 *   USB DM/DP  : PA11/PA12 (USB FS device)
 *   SWDIO/SWCLK: PA13/PA14 (J4 header)
 *   TJA1051 S  : hard-wired to GND (no MCU standby control)
 *   TJA1051 VIO: 3.3V_CAN from Q1 soft-start circuit (hardware only)
 *   PC13       : not connected
 */

#if (defined CANABLE2)

#define CAN1_RX       B, 8, MODE_AF_PP, NOPULL, SPEED_FREQ_VERY_HIGH, AF9_FDCAN1
#define CAN1_TX       B, 9, MODE_AF_PP, NOPULL, SPEED_FREQ_VERY_HIGH, AF9_FDCAN1

/* Status LED (PA15, green, ACTIVE LOW: pull pin low to light it) */
#define IO_LED_STAT   A, 15, MODE_OUTPUT_PP, NOPULL, SPEED_FREQ_LOW, NOAF
/* Work LED (PA0, blue, CAN activity, ACTIVE LOW), shared for RX/TX */
#define IO_LED_TX0    A, 0, MODE_OUTPUT_PP, NOPULL, SPEED_FREQ_LOW, NOAF
#define IO_LED_RX0    A, 0, MODE_OUTPUT_PP, NOPULL, SPEED_FREQ_LOW, NOAF

#define IO_LED_HI     PIN_HI
#define IO_LED_LOW    PIN_LOW

/*
 * LED semantics (aligned with PEAK PCAN-USB Pro FD):
 *   LED_STAT  (PA15, green) = USB status LED
 *     - fast blink during boot / USB enumeration
 *     - solid ON once the device is enumerated and ready
 *   LED_CH0_* (PA0, blue; RX and TX share the same physical LED) = CAN channel LED
 *     - fast blink on every received / transmitted CAN frame (bus traffic)
 *   Hardware limits vs the original PCAN-USB Pro FD:
 *     - only one CAN channel exists (FDCAN1), so there is no CH1 LED;
 *     - the MCU cannot drive the red power LED (D1) and there is no orange LED,
 *       so the USB "full-speed / orange" and CAN "error / red" states cannot be
 *       shown in colour and are simply not indicated on this board.
 *   Both LEDs are ACTIVE LOW (write the pin LOW to turn the LED on).
 */

/* No transceiver standby / power control on this board:
 * TJA1051 S pin is grounded, VIO fed by Q1 soft-start circuit. */
#define IO_HW_INIT()

#elif (defined DEVEBOX32F4)
#define CAN1_RX       B, 8, MODE_AF_PP, PULLUP, SPEED_FREQ_VERY_HIGH, AF9_CAN1
#define CAN1_TX       B, 9, MODE_AF_PP, NOPULL, SPEED_FREQ_VERY_HIGH, AF9_CAN1

#define CAN2_RX       B, 5, MODE_AF_PP, PULLUP, SPEED_FREQ_VERY_HIGH, AF9_CAN2
#define CAN2_TX       B, 6, MODE_AF_PP, NOPULL, SPEED_FREQ_VERY_HIGH, AF9_CAN2

#define IO_LED_STAT   C, 10, MODE_OUTPUT_PP, NOPULL, SPEED_FREQ_MEDIUM, NOAF
#define IO_LED_TX0    A, 2, MODE_OUTPUT_PP, NOPULL, SPEED_FREQ_MEDIUM, NOAF
#define IO_LED_RX0    A, 3, MODE_OUTPUT_PP, NOPULL, SPEED_FREQ_MEDIUM, NOAF
#define IO_LED_TX1    C, 6, MODE_OUTPUT_PP, NOPULL, SPEED_FREQ_MEDIUM, NOAF
#define IO_LED_RX1    C, 7, MODE_OUTPUT_PP, NOPULL, SPEED_FREQ_MEDIUM, NOAF

#define IO_LED_HI     PIN_HI
#define IO_LED_LOW    PIN_LOW

#define IO_HW_INIT()
#elif (defined HEXV2_CLONE)
#define CAN1_RX       D, 0, MODE_AF_PP, PULLUP, SPEED_FREQ_VERY_HIGH, AF9_CAN1
#define CAN1_TX       D, 1, MODE_AF_PP, NOPULL, SPEED_FREQ_VERY_HIGH, AF9_CAN1
/* red */
#define IO_LED_STAT   C, 10, MODE_OUTPUT_PP, NOPULL, SPEED_FREQ_MEDIUM, NOAF
/* blue */
#define IO_LED_TX0    E, 0, MODE_OUTPUT_PP, NOPULL, SPEED_FREQ_MEDIUM, NOAF
/* green */
#define IO_LED_RX0    B, 9, MODE_OUTPUT_PP, NOPULL, SPEED_FREQ_MEDIUM, NOAF

#define IO_LED_HI     PIN_LOW
#define IO_LED_LOW    PIN_HI

#define CAN1_S        E, 10, MODE_OUTPUT_OD, PULLUP, SPEED_FREQ_MEDIUM, NOAF
#define IO_HW_INIT()  PORT_ENABLE_CLOCK( PIN_PORT( CAN1_S ), PIN_PORT( CAN1_S ) );\
                      PIN_LOW( CAN1_S );\
                      PIN_INIT( CAN1_S )

#elif (defined EMPTY_STUB)
#define IO_HW_INIT()
#else
#error Invalid hardware
#endif
