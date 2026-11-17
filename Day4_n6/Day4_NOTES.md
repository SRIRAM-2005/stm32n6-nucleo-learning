# Day 4 Notes: how the STM32N6 boots (theory, no hardware changes)

**Board:** NUCLEO-N657X0-Q (STM32N657X0H3Q)
**Why theory only:** the board is college property and no permanent change is
allowed. Flash boot on this board can involve programming a one-time OTP fuse, so
Day 4 was studied without touching the hardware. Days 1–3 and 5 run from RAM in
development boot mode and change nothing permanently.

---

## 1. The STM32N6 has no internal flash

Most STM32s (e.g. the F446RE) store the program in internal flash and run it from
there. The STM32N6 has **4.2 MB of internal RAM but no internal flash**. The program
is stored in an **external NOR flash** chip (on the Nucleo: Macronix MX25UM51245G,
connected to the XSPI2 port), and must be brought into the chip at every power-up.

---

## 2. Boot sequence

```
Power on / reset
   │
   ▼
Boot ROM  (fixed code inside the chip, written by ST, cannot be changed)
   │  reads BOOT0 / BOOT1 pins
   ├── Development boot ──► opens the debug port, waits for the debugger
   │                         (the debugger loads code into RAM)
   ├── Serial boot ───────► waits for an image over UART/USB
   └── Flash boot ────────► checks the image header in external flash,
                             copies the FSBL into internal RAM (AXISRAM2),
                             jumps to it
                                 │
                                 ▼
                     FSBL (First Stage Boot Loader) = your code
                                 │  optionally loads a bigger application:
                                 ├── Load & Run (LRUN): copy Appli from flash to RAM, run from RAM
                                 └── Execute In Place (XIP): run Appli directly from memory-mapped flash
```

| BOOT0 (JP1) | BOOT1 (JP2) | Mode |
|---|---|---|
| any | 1 | Development boot |
| 0 | 0 | Flash boot |
| 1 | 0 | Serial boot |

On the board used here, **JP2 on pins [1-2] gave development boot** (confirmed by the
boot ROM turning the red LED on steady), so the jumper had been moved from the factory
default position.

**Key point:** in flash boot, a simple FSBL still **runs from internal RAM**. The boot
ROM copies it there. Flash only stores it. The FSBL area copied by the boot ROM is
limited (about 512 KB).

---

## 3. Why the debugger only connects in development boot

The STM32N6 is a security-focused part. The boot ROM controls the **debug port**:

- **Development boot:** debug port opened, so tools can load code, debug, program
  external flash, or program OTP.
- **Flash boot:** the boot ROM hands control to the FSBL with the debug port **closed**
  by default.

This explained the Day 1 error `No device found on target`: the ST-LINK was working,
but the chip was not answering because it was in flash boot.

---

## 4. The image header

The boot ROM does not run raw binary data from flash. The FSBL must have a **header**
(1 KB, `0x400` bytes) in front of it. The header tells the boot ROM the image size,
where to load it, the entry point, and (in secure products) the signature.

ST's tool adds it: `STM32_SigningTool_CLI` (part of STM32CubeProgrammer). A typical
development command:

```
STM32_SigningTool_CLI -bin FSBL.bin -nk -of 0x80000000 -t fsbl -hv 2.3 -align -o FSBL-trusted.bin
```

| Option | Meaning |
|---|---|
| `-bin` | input binary built by CubeIDE |
| `-nk` | **no keys**: add a header only, no cryptographic signature |
| `-t fsbl` | image type: first stage boot loader |
| `-hv 2.3` | header version |
| `-align` | pad so the header is exactly 0x400 bytes (needed with newer tool versions) |

The result is then written to the start of the external flash (`0x70000000`) with
STM32CubeProgrammer, using the board's external loader
`MX25UM51245G_STM32N6570-NUCLEO.stldr`.

**Linker consequence:** the CubeIDE FSBL linker script starts code at `0x34180400`,
not `0x34180000`. The first `0x400` bytes of AXISRAM2 are where the header lands when
the boot ROM copies the image.

---

## 5. OTP fuses, and why OTP124 was not touched

**OTP (one-time programmable) memory** holds permanent chip settings and keys. A bit
can be changed from 0 to 1 **once, and never back**.

**OTP word 124 (HCONF1)** contains the I/O voltage settings:

- The Nucleo's external flash runs at **1.8 V** on the **VDDIO3** supply.
- STM32 I/Os default to the 3.3 V range. To run XSPI at full speed at 1.8 V, the
  **HSLV_VDDIO3** bit (bit 15) must be set. On this board the correct value is
  `OTP124 = 0x00008000` (bit 15 only).
- Some guides use `0x00018000`, which also sets **HSLV_VDDIO2** (bit 16). On this
  Nucleo, VDDIO2 is powered at **3.3 V**, and ST warns that setting the high-speed
  low-voltage bit while that supply is at 3.3 V **damages the device**.

**Decision:** not programmed. It is irreversible, the board is not ours, and a wrong
value can destroy the chip. The safe experiment (read OTP124, back up the flash, write
a headed image, test, restore the backup) was also skipped unless the lab in-charge
approves the temporary flash write.

---

## 6. Interview questions

**How does a flashless MCU like the STM32N6 boot?**
A boot ROM reads the boot pins, then in flash boot verifies the header of the first
image in external flash, copies it into internal RAM, and jumps to it. That first
image (FSBL) can then load a larger application.

**What is the difference between Load & Run and Execute In Place?**
LRUN copies the application into RAM before running it (fast execution, uses RAM).
XIP runs it directly from memory-mapped external flash (saves RAM, slower, relies
heavily on the instruction cache).

**Why does a secure MCU close the debug port in normal boot?**
An open debug port lets anyone read memory and keys or inject code. It is opened only
in a controlled development mode, or after authentication.

**What is OTP, and what are the risks of programming it?**
One-time programmable fuses for permanent configuration and keys. Mistakes cannot be
undone: a wrong I/O voltage fuse can damage the chip, and a wrong security fuse can
lock the device permanently.

**What does a boot image header contain?**
Magic number, version, image length, load address, entry point, and optionally a
signature and encryption information, so the boot ROM can check the image before
running it.
