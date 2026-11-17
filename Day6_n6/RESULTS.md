# Day 6 Results: TinyML on the Cortex-M55 CPU (plain C vs CMSIS-NN Helium)

**Board:** NUCLEO-N657X0-Q (STM32N657X0H3Q, Cortex-M55 @ 600 MHz)
**Project:** `Day6_n6` (created from Day 1 with CubeMX "Save Project As")
**Runs from:** internal RAM, development boot. No permanent change to the board.

---

## Why the CPU and not the NPU

The STM32N6 has a Neural-ART NPU, but ST's documented flow for this board stores the
model weights in **external flash** and boots from flash. The board is college
property and its external flash must not be overwritten, so Day 6 measures what the
**Cortex-M55 CPU** can do instead. The M55 has **Helium (MVE)**, Arm's vector
extension for DSP and ML. TinyML libraries such as TensorFlow Lite Micro use it
through **CMSIS-NN** on chips without an NPU.

---

## What was measured

One **int8 Conv1D layer**, the kind used in 1-D signal models:

| | |
|---|---|
| Input | 128 samples × 8 channels, int8 (zero point −3) |
| Weights | 16 filters × kernel 5 × 8 channels, int8, per-channel scale |
| Bias | 16 × int32 |
| Output | 128 × 16, int8 (zero point 5), "same" padding, stride 1 |
| Work | **81,920 multiply-accumulates (MACs)** |

Computed two ways:
1. **Plain C reference**: four nested loops, one MAC at a time.
2. **CMSIS-NN `arm_convolve_s8`**: Helium vector kernels (im2col + int8 matrix multiply).

Both use the same TFLite-style requantization (`arm_nn_requantize`), so the outputs
must be **bit-identical**. Each was timed with the DWT cycle counter (fastest of
5 runs, interrupts off), with caches off and then on, in a `-O0` and a `-O2` build.

### Project setup
- CMSIS-NN cloned from GitHub (outside the repo), then only what the layer needs copied
  into `FSBL/CMSIS-NN/`: the `Include` folder, `LICENSE` (Apache-2.0), and three
  sources, found with a link test:
  `arm_convolve_s8.c`, `arm_convolve_get_buffer_sizes_s8.c`, `arm_nn_mat_mult_nt_t_s8.c`.
  (Copying the whole library also pulls in a file that does not compile at `-O0`.)
- Include path `../CMSIS-NN/Include`, and `CMSIS-NN` added as a **source location**
  (CubeMX projects only compile listed folders; the first build failed with
  `undefined reference to arm_convolve_s8` until it was added).
- Helium is enabled by CubeIDE's normal flags (`-mcpu=cortex-m55`): the program
  confirms `ARM_MATH_MVEI` is defined.

### Correctness check
- On a PC (CMSIS-NN's portable C path): reference vs CMSIS-NN, **0 of 2048** outputs differ.
- On the board (Helium path), in all four configurations: **0 of 2048** differ.
- The first outputs (`-18, 10, 35, -16, -51, 1`) are the same on PC and board.

---

## Results

### `-O0` (Debug)
| | Caches OFF | Caches ON |
|---|---|---|
| Plain C | 38,597,573 cycles (64.3 ms) | 3,022,622 cycles (5.04 ms) |
| CMSIS-NN (Helium) | 3,336,955 cycles (5.56 ms) | 281,000 cycles (0.468 ms) |

### `-O2`
| | Caches OFF | Caches ON |
|---|---|---|
| Plain C | 17,444,422 cycles (29.1 ms) | 1,319,929 cycles (2.20 ms) |
| CMSIS-NN (Helium) | 1,187,672 cycles (1.98 ms) | **85,079 cycles (0.141 ms), 0.96 MAC/cycle** |

### Speed-ups
| Comparison | `-O0` | `-O2` |
|---|---|---|
| CMSIS-NN vs plain C, caches off | 11.5× | 14.6× |
| CMSIS-NN vs plain C, caches on | 10.7× | 15.5× |
| Caches on vs off, plain C | 12.7× | 13.2× |
| Caches on vs off, CMSIS-NN | 11.8× | 13.9× |
| Best vs worst in the same build | 137× | 205× |
| `-O2` vs `-O0`, plain C, caches on | | 2.3× |
| `-O2` vs `-O0`, CMSIS-NN, caches on | | 3.3× |

**Overall: from 38.6 M cycles (plain C, `-O0`, no caches) to 85 k cycles
(CMSIS-NN, `-O2`, caches) = about 450× faster for identical results.**

---

## What the numbers show

1. **Correct first, then fast.** The Helium path matched the reference bit for bit
   in every configuration. A speed-up only counts after that check.

2. **Helium gives about 15×** at `-O2`. CMSIS-NN rearranges the input (im2col) so one
   vector instruction does many 8-bit MACs. Its scratch buffer was **192 bytes** on the
   board vs 160 on the PC, because the Helium path uses a different layout.

3. **Caches give about 13×** for both versions, consistent with Day 3's 8.6× on a
   different benchmark. Without the D-cache every load and store waits on RAM.

4. **The effects multiply.** Vector code + caches + optimisation together turned a
   64 ms layer into a 0.14 ms one.

5. **Vector code depends more on the optimiser** (3.3× from `-O2`) than scalar code
   (2.3×). At `-O0` the compiler keeps spilling vector registers to memory.

6. **Room left.** 0.96 MAC/cycle is well below the M55's peak for int8 (roughly
   8 MACs/cycle for its 128-bit Helium unit). This layer is small: 8 input channels
   × kernel 5 gives dot products only 40 long, so the im2col copying, requantization
   and loop overhead are large compared with the multiply work. Wider layers use the
   vector unit better.

7. **Plain C is memory-bound without caches.** At `-O0` with caches off it needs about
   470 cycles per MAC; most of that is loading and storing loop variables in RAM.

---

## Interview questions answered by this exercise

**What is int8 quantization, and why is it used on microcontrollers?**
Weights and activations are stored as 8-bit integers with a scale and zero point
instead of 32-bit floats: 4× less memory, and integer MACs are cheap and can be
vectorised. Each output is requantized back to int8 with a fixed-point multiplier and
shift.

**What is Helium (MVE)?**
Arm's M-Profile Vector Extension: 128-bit vector registers and instructions on
Cortex-M55/M85, so one instruction processes 16 int8 values.

**What does CMSIS-NN do?**
Optimised neural-network kernels (convolution, fully connected, pooling, activations)
for Cortex-M, using DSP or Helium instructions. TensorFlow Lite Micro calls it
underneath. Its results are bit-exact with the TFLite reference kernels.

**How do you check that an optimised kernel is correct?**
Run it against a simple reference implementation on the same data and compare every
output exactly, on the target, in every build configuration.

**What is im2col?**
Copy the input patch each output needs into a contiguous column, so the convolution
becomes a matrix multiply, which vectorises well. It costs extra memory (the scratch
buffer) and copying time.

**Why are CPU caches important for ML inference?**
Inference repeatedly reads weights and activations. Without a data cache each access
waits on RAM; here caches alone gave about 13×.

---

## Key takeaways
1. Same int8 maths, verified bit-exact, about 450× faster from naive to optimised.
2. Helium ×15, caches ×13, `-O2` ×2–3: separate effects that multiply.
3. A small layer leaves the vector unit underused; layer shape affects efficiency.
4. Adding a third-party library to a CubeMX project means include paths **and**
   source locations.
5. The NPU would be next, but needs external flash, which this board must not have
   written.
