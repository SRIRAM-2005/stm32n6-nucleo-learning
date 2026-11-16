# Day 2 Results: register-level GPIO, cycle benchmarks, clock tree and SysTick

**Board:** NUCLEO-N657X0-Q (STM32N657X0H3Q, Cortex-M55 @ 600 MHz)
**Project:** `Day2_n6` (same CubeMX setup as Day 1, created with CubeMX "Save Project As")
**Runs from:** internal RAM (AXISRAM2), development boot mode

---

## Goal

Day 1 used HAL functions. Day 2 looks underneath them:

- configure and drive a GPIO **using registers only**, with no HAL
- read and decode the real register values in hex
- **measure** the cost of HAL calls against direct register access, in CPU cycles
- confirm the clock tree and SysTick configuration with measurements

---

## What was done, and why

### Exercise 1: blue LED (PG8) configured entirely with registers
PG8 was deliberately **not** configured in CubeMX, so every step had to be done by hand:

| Step | Register | Code | Why |
|---|---|---|---|
| 1 | `RCC->AHB4ENSR` | `= RCC_AHB4ENSR_GPIOGENS` | A peripheral does nothing until its clock is on. On the N6 this is a **set register**: writing 1 enables, writing 0 changes nothing, so no read-modify-write is needed |
| 2 | `GPIOG->BSRR` | `= 1 << 8` | Drive the pin HIGH (LED off) **before** making it an output, so it never flashes |
| 3 | `GPIOG->MODER` | bits[17:16] = `01` | Output mode (2 bits per pin: 00 in, 01 out, 10 AF, 11 analog) |
| 4 | `GPIOG->OTYPER` | bit 8 = `0` | Push-pull |
| 5 | `GPIOG->OSPEEDR` | bits[17:16] = `00` | Low speed is enough for an LED and reduces noise |
| 6 | `GPIOG->PUPDR` | bits[17:16] = `00` | No pull resistor on an output |
| On/off | `GPIOG->BSRR` | `1 << (8+16)` = LOW = ON, `1 << 8` = HIGH = OFF | One atomic write, no read-modify-write |

The delay uses the **DWT cycle counter** (`DWT->CYCCNT`), a hardware counter in the
Cortex-M55 that increases by 1 every CPU clock cycle.

### Exercise 2: button read from the input register
`pressed = (GPIOC->IDR >> 13) & 1`, mirrored on the blue LED through BSRR.
**Why:** practise reading an input register and extracting a single bit.

### Exercise 3: cycle benchmark, HAL vs registers
Each operation was run 1000 times between two `DWT->CYCCNT` reads, with interrupts
disabled. The empty-loop cost was subtracted.
**Why:** turn "HAL is slower" and "BSRR is better than ODR" into measured facts.

### Exercise 4: clock tree and SysTick
Printed every bus frequency and the SysTick registers, then timed `HAL_Delay(1000)` with
the cycle counter.
**Why:** prove the clock configuration is correct, and understand where the 1 ms tick
comes from.

---

## Results

### Register dump (printed at start-up)
```
GPIOG->MODER   = 0xFFDDFFFD  (PG8 bits[17:16] = 1, expect 1 = output)
GPIOG->OTYPER  = 0x00000000
GPIOG->OSPEEDR = 0x00000000
GPIOG->PUPDR   = 0x00000000
GPIOG->ODR     = 0x00000501
GPIOC->MODER   = 0xF3FFFFFB  (PC13 bits[27:26] = 0, expect 0 = input)
GPIOC->PUPDR   = 0x08000000  (PC13 bits[27:26] = 2, expect 2 = pull-down)
```

**Decoding `GPIOG->MODER = 0xFFDDFFFD` (2 bits per pin):**
```
pin:  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
bits: 11 11 11 11 11 01 11 01 11 11 11 11 11 11 11 01
                     ↑     ↑                       ↑
                   PG10   PG8                     PG0
                   red    blue (my registers)     green
```
- PG0, PG8, PG10 = `01` output. PG0 and PG10 were set by CubeMX, PG8 by my register code.
- All other pins = `11` **analog**. On the STM32N6, unused pins reset to analog mode
  (lowest power). This differs from older STM32 families such as the F4, where most
  pins reset to input.

**`GPIOG->ODR = 0x501`:** bits 0, 8, 10 HIGH → all three active-low LEDs OFF.

**`GPIOC->PUPDR = 0x08000000`:** bits[27:26] = `10` → pull-down on PC13, as set in CubeMX.

### Exercise 1
Blue LED blinked (200 ms on / 800 ms off). Every print shows `ODR bit8=1`, because the
print happens just after the LED is switched off (pin HIGH).

### Exercise 2
```
GPIOC->IDR = 0x0002  -> button released
GPIOC->IDR = 0x2002  -> button PRESSED
```
- **Bit 13** follows the button.
- **Bit 1 (PC1) is always 1.** PC1 is the I2C1 SDA line from the board preset. I2C lines
  have pull-up resistors, so an idle I2C bus reads HIGH.

### Exercise 3: average CPU cycles per operation (600 MHz, 1 cycle = 1.67 ns)

| Operation | `-O0` (Debug) | `-O2` | Time at `-O2` |
|---|---|---|---|
| Loop overhead only | 78 | **5** | 8 ns |
| `HAL_GPIO_WritePin` | 273 | **80** | 133 ns |
| `HAL_GPIO_TogglePin` | 389 | **102** | 170 ns |
| `GPIOG->BSRR = mask` | 39 | **≈ 0** (< 1) | — |
| `GPIOG->ODR ^= mask` (read-modify-write) | 102 | **72** | 120 ns |

**What the numbers show:**

1. **Writes to a peripheral are almost free; reads are expensive.**
   At `-O2`, a BSRR write adds less than one cycle on top of the loop. The Cortex-M55
   **buffers the write** (a "posted write") and continues immediately. A **read** cannot be
   buffered: the CPU must **stall** until the value comes back across the 100 MHz bus.
   That is why `ODR ^=` still costs about 70 cycles (≈117 ns) even when optimised.

2. **`ODR ^=` is both slower and unsafe.** It is three steps (read, modify, write). If an
   interrupt changes the same port between the read and the write, that change is lost.
   BSRR is a single write, so it is **atomic**.

3. **HAL costs 80 to 100 cycles per call** at `-O2`. TogglePin costs more than WritePin
   because it **reads** ODR first, so it includes a read stall.

4. **Never judge performance from a `-O0` build.** At `-O0`, BSRR appeared to cost
   39 cycles. That was instruction overhead from unoptimised code, not hardware. My first
   interpretation blamed the bus, and the `-O2` run proved it wrong.

**Open question for Day 3:** the generated code never enables the instruction cache.
Instruction fetches from RAM over the bus may explain part of the HAL call cost. Test by
enabling the I-cache and rerunning this benchmark.

### Exercise 4: clock tree, SysTick and HAL_Delay
```
--- Clock tree (from RCC registers via HAL) ---
  CPU   (SYSA) : 600000000 Hz
  SYSCLK(SYSB) : 400000000 Hz
  HCLK  (AHB)  : 100000000 Hz
  PCLK1 (APB1) : 100000000 Hz
  PCLK2 (APB2) : 100000000 Hz  <- USART1 runs from this bus
  PCLK4 (APB4) : 100000000 Hz
  PCLK5 (APB5) : 100000000 Hz
--- SysTick registers ---
  SysTick->LOAD = 599999  -> interrupt every 600000 cycles
  SysTick->CTRL = 0x10007  (ENABLE=1 TICKINT=1 CLKSOURCE=1)
  tick rate = CPU / (LOAD+1) = 1000 Hz
--- Measuring HAL_Delay(1000) with the cycle counter ---
  cycles = 600282566  (expect ~600000000)
```

**Clock tree (matches the CubeMX configuration):**
```
HSI 64 MHz → PLL1 1200 MHz ─┬─ IC1 ÷2 → CPU (SYSA)  600 MHz
                            └─ IC2 ÷3 → SYSB        400 MHz ─ HPRE ÷4 → HCLK 100 MHz
                                                                └─ APB1/2/4/5 100 MHz
```
The CPU runs **6× faster** than the bus the peripherals sit on, which is why peripheral
reads stall the CPU for so many cycles (Exercise 3).

**SysTick:**
- `LOAD = 599999`: counts 599999 → 0 = 600,000 cycles = **1 ms** at 600 MHz.
- `CTRL = 0x10007`: ENABLE = 1, TICKINT = 1 (interrupt on zero), CLKSOURCE = 1 (CPU clock),
  bit 16 COUNTFLAG = 1 (has reached zero at least once).
- This 1 ms interrupt increments `uwTick`, which `HAL_GetTick()` and `HAL_Delay()` use.

**`HAL_Delay(1000)` took 600,282,566 cycles = 1.00047 s.**
The extra ≈0.47 ms is intentional. The STM32N6 HAL source adds one tick:
```c
wait += (uint32_t)(uwTickFreq);   /* "Add a freq to guarantee minimum wait" */
```
`HAL_Delay(N)` is called at a random point inside a 1 ms tick. Without the extra tick,
`HAL_Delay(1)` could return almost immediately. So `HAL_Delay(N)` waits **between N and
N+1 ms**, guaranteeing at least N. This run started about halfway through a tick.

---

## Interview questions answered by this exercise

**How do you configure a GPIO pin as an output without HAL?**
Enable the port clock in RCC, set the output level, set MODER to `01`, OTYPER to
push-pull, choose OSPEEDR and PUPDR, then drive the pin through BSRR.

**Why use BSRR instead of `ODR |=` / `ODR ^=`?**
BSRR is a single write, so it is atomic and interrupt-safe. ODR operations are
read-modify-write: slower (the read stalls the CPU) and an interrupt can corrupt them.

**Why are peripheral reads slower than writes?**
Writes are buffered and the CPU continues. Reads must wait for the data to return from a
slower bus (here 100 MHz vs a 600 MHz CPU).

**How does `HAL_Delay()` work, and how accurate is it?**
SysTick interrupts every 1 ms (LOAD = CPU clock / 1000 − 1) and increments a counter.
`HAL_Delay(N)` busy-waits until N+1 ticks have passed, so it lasts between N and N+1 ms.

**How do you measure execution time precisely on a Cortex-M?**
Enable the DWT cycle counter (`DCB->DEMCR` TRCENA, then `DWT->CTRL` CYCCNTENA), and read
`DWT->CYCCNT` before and after. Use unsigned subtraction so counter wrap-around is
handled.

---

## Key takeaways
1. HAL is a layer over registers. Every HAL GPIO call ends in writes to MODER, BSRR and
   the others.
2. Measure, don't assume. The `-O2` benchmark corrected a wrong explanation.
3. Peripheral writes are cheap, reads are expensive, BSRR is atomic.
4. The clock tree decides both CPU speed and peripheral access latency.
