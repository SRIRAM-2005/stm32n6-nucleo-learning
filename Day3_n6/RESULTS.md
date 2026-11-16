# Day 3 Results: hardware timers, PWM, and the effect of CPU caches

**Board:** NUCLEO-N657X0-Q (STM32N657X0H3Q, Cortex-M55 @ 600 MHz)
**Project:** `Day3_n6` (created from Day 2 with CubeMX "Save Project As")
**Build:** Debug (`-O0`), running from internal RAM (AXISRAM2), development boot mode

---

## Goal

- Use a **hardware timer interrupt** for timing instead of software delays
- Dim LEDs with **hardware PWM**, without using the CPU
- Answer the open question from Day 2: why is a HAL call so slow on a 600 MHz CPU?

---

## Hardware choices, and why

The LED pins were checked against the STM32N657 pin multiplexing data before choosing
timers:

| LED | Pin | Timer channel | AF |
|---|---|---|---|
| Green (LD6) | PG0 | **TIM12_CH1** | AF2 |
| Blue (LD7) | PG8 | **TIM12_CH2** | AF2 |
| Red (LD5) | PG10 | stays GPIO output | — |

- **TIM12** drives both PWM LEDs, because both pins have a TIM12 channel.
- **TIM6** is a basic timer with no pins, used only for a periodic interrupt.
- **Red stays GPIO**, toggled from the TIM6 interrupt (heartbeat) and used by `Error_Handler()`.

### Timer clock
CubeMX generated `PeriphCommonClock_Config()` with `TIMPRES = DIV1`:
```
TIMG (timer kernel clock) = SYSB 400 MHz / 1 = 400 MHz
```

### Timer maths
```
update rate = f_TIMG / ((PSC + 1) × (ARR + 1))

TIM6 : 400 MHz / (40000 × 10000) = 1 Hz      PSC = 39999, ARR = 9999
TIM12: 400 MHz / (  400 ×  1000) = 1 kHz     PSC = 399,   ARR = 999  → 1000 brightness steps
```

### PWM polarity
The LEDs are **active low**. Channel polarity was set to **Low** in CubeMX, so the pin
is LOW (LED on) while the counter is below CCR. That makes **CCR = on-time**: a larger
value means a brighter LED.

**Output compare preload = Enable:** a new CCR value only takes effect at the start of
the next PWM period, so a period is never cut short. This keeps fades smooth.

---

## Exercises

| Ex | What it does | Concept |
|---|---|---|
| 1 | TIM6 interrupt every 1 s toggles the red LED; each period measured with the DWT cycle counter | Timer interrupts, precision |
| 2 | Green 5 %, blue 50 %; the CPU runs an empty loop | Hardware PWM, CCR register |
| 3 | Green and blue "breathe" in opposite phase | Changing duty at runtime, gamma correction |
| 4 | Benchmarks with instruction cache OFF, then ON | Effect of I-cache |
| 5 | Benchmarks with no cache, D-cache only, then I + D | Effect of D-cache, interaction of both |

---

## Results

### Configuration read back from the registers (every exercise)
```
SystemCoreClock = 600000000 Hz
Timer kernel clock (TIMG) = 400000000 Hz
TIM6 : PSC=39999 ARR=9999 -> counter 10000 Hz, update 1.000 Hz
TIM12: PSC=399 ARR=999 -> PWM frequency 1000 Hz, 1000 steps
```
Every value matches the calculation.

### Exercise 1: timer interrupt period
```
tick   2  period = 600000000 cycles = 1.000000 s
tick   3  period = 600000000 cycles = 1.000000 s
...
tick 115  period = 600000000 cycles = 1.000000 s
```
**Every period was exactly 600,000,000 CPU cycles** (except ticks 46–47, below).

**Why exactly, with zero variation:** the CPU clock (PLL1 1200 MHz ÷ 2) and the timer
clock (PLL1 ÷ 3) come from the **same PLL**, so the two counters are locked together.

**Consistent is not the same as accurate.** This proves the timer is *consistent* with
the CPU, not that 1 s is exactly 1 real second. Both clocks come from the HSI internal
RC oscillator, which has a tolerance of roughly ±1 %. Accurate timekeeping needs a
crystal: the board's 48 MHz HSE, or the 32.768 kHz LSE for an RTC.

**Compared with Day 2:** `HAL_Delay(1000)` measured 1.00047 s and depends on when it
is called. The hardware timer period does not depend on the software at all.

**Anomaly at ticks 46–47:**
```
tick 46  period = 587299274 cycles = 0.978832 s
tick 47  period = 184448375 cycles = 0.307413 s
```
The two periods add up to **1.286 s instead of 2.000 s**.
- If the interrupt had simply arrived late, one period would be longer and the next
  shorter, but they would still add up to 2.000 s.
- They add up to less, so the **cycle counter lost about 0.71 s of counting**. The DWT
  counter stops while the CPU is halted by the debugger, but TIM6 keeps running.
- The cause was a **debugger interaction** in CubeIDE during the run (a click that
  briefly halted the core). Checking the sum is what separated "late interrupt" from
  "counter stopped".

### Exercise 2: fixed PWM
```
Green (PG0, TIM12_CH1): CCR1=50 of 1000 -> 5% on
Blue  (PG8, TIM12_CH2): CCR2=500 of 1000 -> 50% on
The CPU now does nothing: the timer drives the pins by itself.
```
Green visibly dim, blue at medium brightness, with no CPU involvement.

### Exercise 3: breathing LEDs
Green and blue fade in opposite phase. Brightness is set from a **squared ramp**
(`duty = x² / 1000`) because the eye perceives brightness roughly logarithmically; a
linear ramp spends most of the fade looking almost fully bright.

### Exercises 4 and 5: the effect of CPU caches
Average cycles, 1000 runs each, `-O0` build, interrupts disabled during measurement:

| Caches | `HAL_GPIO_WritePin` (cycles/call) | Compute loop (cycles/iteration) | Compute speed-up |
|---|---|---|---|
| None | 321 | 231 | 1× |
| I-cache only (Ex 4) | 309 | 228 | 1.01× |
| D-cache only (Ex 5) | 207 | 87 | 2.7× |
| **I-cache + D-cache (Ex 5)** | **36** | **27** | **8.6×** |

(The compute loop is a multiply, add, shift and XOR per iteration, with no peripheral
access.)

**What the numbers show:**

1. **Both caches are OFF after reset.** The generated FSBL code never enables them, so
   every Day 1–3 benchmark ran with caches off.

2. **The data cache alone gave 2.7×.** In a `-O0` build, local variables live on the
   stack in RAM, so every iteration does several loads and stores. Without the D-cache,
   each one waits for RAM.

3. **The instruction cache alone did almost nothing (Ex 4), but with the D-cache on it
   gave another 3.2×** (87 → 27 cycles). The data stalls were so large that they hid
   the instruction-fetch stalls. Once the D-cache removed the biggest bottleneck,
   instruction fetch became the next one.

4. **Correction to my own conclusion.** After Exercise 4, I concluded that the I-cache
   doesn't matter when running from internal RAM. Exercise 5 proved that wrong: the
   effect was masked, not absent. **Fixing one bottleneck exposes the next, so measuring
   one change in isolation can mislead when two problems overlap.**

5. **Effect on Day 2's numbers.** Day 2's benchmarks were about 9× slower than this chip
   runs with caches on. The Day 2 conclusions still hold (BSRR is atomic, peripheral
   writes are buffered, reads stall), but the absolute cycle counts were inflated.

**From now on:** enable both caches (CubeMX: System Core → CORTEX_M55_FSBL). The D-cache
needs care with DMA. A DMA engine reads and writes RAM directly, bypassing the cache,
so cached buffers must be cleaned or invalidated, or placed in non-cacheable memory.
Otherwise the CPU and DMA see different data (**cache coherence**). This matters for
Ethernet, which uses DMA.

---

## Interview questions answered by this exercise

**How do you calculate a timer's prescaler and period for a given rate?**
`f_update = f_timer / ((PSC + 1) × (ARR + 1))`. Choose PSC for a convenient counter
frequency (e.g. 10 kHz), then ARR for the rate. Both are 16-bit on these timers, so
each must be ≤ 65535.

**Why use a hardware timer instead of `HAL_Delay()`?**
The timer counts in hardware regardless of what the CPU is doing, so the period is
fixed. The CPU is free to do other work, or sleep, between interrupts.

**What is PWM, and how is brightness controlled?**
A fixed-frequency square wave whose ON fraction (duty cycle) sets the average power.
On STM32: ARR sets the period, CCR sets the ON time, duty = CCR / (ARR + 1). At 1 kHz,
the eye sees steady brightness, not flicker.

**What's the difference between a consistent and an accurate clock?**
Consistent: two counters from the same source agree perfectly. Accurate: the source
matches real time. An RC oscillator can be consistent but ±1 % inaccurate; a crystal is
needed for accuracy.

**What do the I-cache and D-cache do, and when are they a problem?**
They keep recently used instructions and data in fast memory near the CPU. They can
give large speed-ups (8.6× measured here). The D-cache causes coherence problems with
DMA, which bypasses the cache.

---

## Key takeaways
1. Hardware timers give exact, CPU-independent timing; PWM needs no CPU at all.
2. Check pin multiplexing before choosing a timer: the LED pins decided TIM12.
3. Active-low outputs: set PWM polarity Low so CCR means on-time.
4. Caches gave an 8.6× speed-up and were off by default; enable them, and handle DMA
   coherence.
5. When two bottlenecks overlap, removing one can show no improvement; test combinations.
