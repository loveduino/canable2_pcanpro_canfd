# XCAN PCAN Pro / Pro FD firmware

This project has been ported from the **STM32F407** to the **CANable 2.0**
(STM32G431) hardware.

## Target hardware: CANable 2.0

- MCU: STM32G431CBTx (128KB Flash / 22KB SRAM + 10KB CCM, Cortex-M4 running at 160 MHz, rated up to 170 MHz)
- CAN: a single **FDCAN1** (native CAN-FD support)
- USB: USB 2.0 Full-Speed Device (crystal-less: HSI16 + HSI48 + CRS synchronization)

### Pin assignment

| Pin        | Function                                            |
| ---------- | --------------------------------------------------- |
| PB8        | FDCAN1 RX (AF9)                                     |
| PB9        | FDCAN1 TX (AF9)                                     |
| PA15       | Green LED — USB status (ACTIVE LOW)                 |
| PA0        | Blue LED — CAN activity (ACTIVE LOW, RX/TX shared)  |
| PA11 / PA12| USB DM / DP (FS device, internal PHY)               |
| PA13 / PA14| SWDIO / SWCLK (J4 header)                           |

Notes:
- The TJA1051T/3 transceiver `S` pin is hard-wired to GND, so there is **no
  MCU standby control**. Its `VIO` is fed from a 3.3V_CAN soft-start circuit
  (Q1) — hardware only.
- The red power LED (D1) is always on and is **not** wired to the MCU.
- PC13 is not connected.
- Both status LEDs are ACTIVE LOW (write the pin LOW to turn the LED on).

### Clock tree

- HSI16 → PLL (÷4 × 80 = 320 MHz VCO; PLLR ÷2 → **160 MHz SYSCLK**)
- HSI48 → USB FS (CRS synchronized from USB SOF)
- FDCAN kernel clock = 80 MHz (PLLQ ÷4) — used directly as the CAN timing reference

## Differences from the original F4 project

1. **MCU**: STM32F407 → STM32G431 (same Cortex-M4 core, different peripherals/memory)
2. **CAN**: bxCAN (dual CAN1/CAN2) → FDCAN1 (single channel, `pcanpro_can.c` fully rewritten)
3. **Clock**: HSE 8/25 MHz → HSI16 + HSI48 (crystal-less)
4. **USB**: OTG HS/FS → USB_DRD_FS device
5. **Timestamp**: TIM3+TIM9 cascade → TIM2+TIM3 cascade (G4 has no TIM9)
6. **LED**: 5 → 2 (green PA15 / blue PA0)

## Memory optimization (G431 has only 22KB SRAM)

The STM32G431 SRAM is small (SRAM1 16KB + SRAM2 6KB), so the original F4 TX FIFO
(`tx_fifo[256]` × 2 channels × 72B ≈ 36KB) does not fit. To fit single-channel
FDCAN1, the following simplifications were made:

- Removed the second CAN channel (`CAN_BUS_TOTAL` 2 → 1); only FDCAN1 remains
- TX FIFO depth 256 → 64 (ring buffer, still a power of two)
- Protocol layer no longer initializes the second CAN channel

Resulting footprint: text ≈ 24.6KB, data ≈ 5.4KB, bss ≈ 10.6KB, total RAM ≈ 16KB.

## Building

Requires the GNU Arm Embedded Toolchain (`arm-none-eabi-gcc`).

```bash
make              # equivalent to "make canable2", builds the CANable 2.0 firmware
make canable2     # build the CANable 2.0 target explicitly
make debug        # build with RTT logging enabled (-DRTT_ENABLE=1)
make release      # build without RTT (default, zero debug overhead)
make clean        # clean
```

Build artifacts (`.elf` / `.hex` / `.bin`) are placed in the `build/` directory.
The firmware image is named `build/canable2_pcanpro_canfd.bin` (with `.hex` / `.elf` siblings).

## Flashing firmware via DFU

CANable 2.0 updates firmware over USB DFU. To enter DFU mode: pull **BOOT0 high**
(or hold the BOOT button) and power on / reset; the chip boots the built-in STM32
System Memory DFU bootloader (VID 0x0483 / PID 0xDF11).

### Do you need a .dfu file?

**No.** STM32CubeProgrammer and dfu-util can both flash `.hex` / `.bin` directly
(`.hex` carries its own address; `.bin` needs `-s 0x08000000`). The `.dfu`
(DfuSe) format is only needed by some legacy tools or multi-target scenarios, and
is optional.

### Flash with dfu-util (.bin / .hex directly)

```bash
# List DFU devices
dfu-util -l

# Flash .bin (-s sets the Flash start address; :leave resets after flashing)
dfu-util -a 0 -s 0x08000000:leave -D build/canable2_pcanpro_canfd.bin

# Or use the .hex (address is built-in)
dfu-util -a 0 -D build/canable2_pcanpro_canfd.hex
```

### Flash with STM32CubeProgrammer (.bin / .hex / .dfu)

Both the GUI and CLI accept `.bin` / `.hex` / `.elf` / `.dfu` and auto-detect the format.

### Generate a .dfu file (optional)

If a DfuSe-format file is required:

```bash
python tools/make_dfu.py
```

This auto-detects the latest `*.bin` in `build/` and writes a sibling `.dfu`
file. You may also pass an explicit input/output path:

```bash
python tools/make_dfu.py path/to/firmware.bin path/to/firmware.dfu
```

## License

WTFPL
