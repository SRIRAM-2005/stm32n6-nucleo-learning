# STM32N6 Nucleo Learning (NUCLEO-N657X0-Q)

Hands-on embedded systems work on the **STMicroelectronics NUCLEO-N657X0-Q** board,
working up from HAL-based peripherals to register-level code, with real measurements at
each step.

| | |
|---|---|
| **MCU** | STM32N657X0H3Q, Arm **Cortex-M55 @ 600 MHz**, Neural-ART NPU |
| **Memory** | 4.2 MB internal RAM, **no internal flash** (boots from external xSPI NOR flash) |
| **Board** | ST-LINK V3EC on board, 3 user LEDs, user button, Ethernet (RMII PHY), USB Type-C |
| **Tools** | STM32CubeMX, STM32CubeIDE, STM32CubeN6 HAL, Git |

---

## Progress

| Day | Topic | Key results | Details |
|---|---|---|---|
| **1** | GPIO output, UART `printf`, button polling, **EXTI interrupt** | CPU confirmed at 600 MHz; interrupt-driven button with 50 ms debounce; debugged "No device found on target" down to boot mode | [Day1_n6/RESULTS.md](Day1_n6/RESULTS.md) |
| **2** | **Register-level GPIO**, DWT cycle benchmarks, clock tree, SysTick | BSRR write ≈ 0 cycles vs ODR read-modify-write 72 cycles at `-O2` (writes buffered, reads stall); HAL call 80–100 cycles; `HAL_Delay(1000)` measured as 1.00047 s and explained from HAL source | [Day2_n6/RESULTS.md](Day2_n6/RESULTS.md) |
| **3** | **Hardware timers**: TIM6 interrupt, TIM12 **PWM** (fixed and breathing LEDs), **I-cache / D-cache** benchmarks | Timer period exactly 600,000,000 cycles (same PLL as CPU); caches were off by default, and enabling both gave an **8.6× speed-up**; I-cache alone showed no gain because data stalls hid it | [Day3_n6/RESULTS.md](Day3_n6/RESULTS.md) |
| **4** | **Boot chain (theory)**: boot ROM, boot pins, image header, OTP fuses | Not done on hardware: flash boot may need a permanent OTP fuse, and the board is college property | [Day4_boot_theory/README.md](Day4_boot_theory/README.md) |
| **5** | **Ethernet MAC + PHY, raw frames**: MDIO PHY scan, auto-negotiation, TX/RX without a TCP/IP stack | LAN8742A found at MDIO address 0; 100 Mbit/s full duplex negotiated; raw frames (EtherType 0x88B5) seen in Wireshark with MAC-added padding; promiscuous RX matched the laptop's DHCP/mDNS traffic | [Day5_n6/RESULTS.md](Day5_n6/RESULTS.md) |

---

## Repository layout

```
Day1_n6/            Day 1 CubeMX + CubeIDE project
  Day1_n6.ioc       CubeMX configuration (pins, clocks)
  FSBL/Core/        application code (main.c etc.)
  Drivers/          STM32N6 HAL + CMSIS (included so the project builds as-is)
  RESULTS.md        what was done, why, measured results, lessons
Day2_n6/            Day 2 project (same layout)
Day3_n6/            Day 3 project (same layout)
Day4_boot_theory/   Day 4 notes (no project: theory only)
Day5_n6/            Day 5 project (same layout; linker script edited for ETH descriptors)
```

Each project is a single **FSBL** (First Stage Boot Loader) project, built to run from
internal RAM (AXISRAM2).

---

## Hardware notes (from UM3417 and testing)

**No permanent changes:** the board is college property. Everything runs from RAM; no
OTP fuses or flash were programmed.


| Function | Pin | Notes |
|---|---|---|
| Red LED (LD5) | PG10 | Active low |
| Green LED (LD6) | PG0 | Active low; TIM12_CH1 (AF2) for PWM |
| Blue LED (LD7) | PG8 | Active low; TIM12_CH2 (AF2) for PWM |
| User button (B1) | PC13 | Active high (needs pull-down) |
| Virtual COM port | USART1 PE5 (TX) / PE6 (RX) | 115200 8N1 via ST-LINK |
| Ethernet (RMII) | PF7 REF_CLK, PF4 MDIO, PG11 MDC, PF10 CRS_DV, PF14/15 RXD, PF11 TX_EN, PF12/13 TXD | LAN8742A PHY at MDIO address 0 |

**Boot mode matters for debugging.** The STM32N6 only opens its debug port in
**development boot** mode. On my board that is **JP2 (BOOT1) on pins [1-2]**, which the
boot ROM confirms by turning the red LED on steady. In flash boot mode, the debugger
reports `No device found on target`.

---

## How to run a project

1. Board unpowered: set **JP2 to development boot** (see above), then connect the
   ST-LINK USB-C port (CN10).
2. In STM32CubeIDE: **File → Import → Existing Projects into Workspace** → select the
   `DayN_n6` folder.
3. Build the `DayN_n6_FSBL` project (Ctrl+B).
4. Right-click the project → **Debug As → STM32 C/C++ Application** → Resume (F8).
5. Open the ST-LINK Virtual COM port at **115200 8N1** to see the output.

Code runs from RAM, so it must be reloaded after every power cycle.
