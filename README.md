# STM32N6 Nucleo Learning (NUCLEO-N657X0-Q)

Hands-on embedded systems work on the **STMicroelectronics NUCLEO-N657X0-Q** board
(STM32N657X0H3Q, Arm Cortex-M55 @ 600 MHz, Neural-ART NPU, no internal flash).

## Day 1: GPIO, UART printf, button polling and EXTI interrupt (`Day1_n6/`)

Configured with **STM32CubeMX**, built and debugged in **STM32CubeIDE**, running
from internal RAM (AXISRAM2) in development boot mode.

### What it does
| Exercise | Behaviour | Concept |
|---|---|---|
| 1 | Red LED blinks, counter printed over UART | Blocking delay (`HAL_Delay`) |
| 2 | Button toggles green LED, red LED heartbeat | Polling, edge detection, software debounce, non-blocking timing |
| 3 | Same as 2, button handled by interrupt | EXTI + NVIC, ISR sets a `volatile` flag, main loop does the work |

Select the exercise with `#define DAY1_EXERCISE` in `FSBL/Core/Src/main.c`.

### Hardware mapping (from UM3417)
| Function | Pin | Notes |
|---|---|---|
| Red LED (LD5) | PG10 | Active low |
| Green LED (LD6) | PG0 | Active low |
| User button (B1) | PC13 | Active high, internal pull-down, EXTI13 rising edge |
| Virtual COM port | USART1 PE5 (TX) / PE6 (RX), AF7 | 115200 8N1 via ST-LINK |

### Clock
HSI 64 MHz → PLL1 (÷4 ×75) = 1200 MHz → IC1 ÷2 → **CPU 600 MHz**

### Output
```
=== NUCLEO-N657X0-Q Day 1 ===
SystemCoreClock = 600000000 Hz
Exercise 3 running
[irq] press 1, green=1
[irq] press 2, green=0
[irq] press 3, green=1
```

### How to run
1. Board unpowered: set **JP2 (BOOT1) to development boot**. On my board this was
   position **[1-2]**; the boot ROM shows it by turning the red LED on steady.
2. Open `Day1_n6/FSBL` in STM32CubeIDE, build (Ctrl+B).
3. Debug As → STM32 C/C++ Application → Resume (F8).
4. Open the ST-LINK Virtual COM port at 115200 8N1.

The program runs from RAM, so it must be reloaded after every power cycle.

### What I learned
- STM32N6 boot flow: boot ROM → development boot (debug port open) vs flash boot
  (debug port closed), and why the debugger reported "No device found on target".
- CubeMX on N6: assigning pins and peripherals to the **FSBL runtime context**,
  and checking CubeMX's automatic pin choices against the board manual.
- Interrupt chain: EXTI → NVIC → IRQ handler → HAL callback; keeping ISRs short;
  why shared ISR variables must be `volatile`.
- Retargeting `printf` to UART through `__io_putchar`.
- Systematic debug-probe troubleshooting (USB driver → ST-LINK firmware → target
  power → boot mode).

## Tools
STM32CubeMX, STM32CubeIDE, STM32CubeN6 HAL, ST-LINK V3EC
