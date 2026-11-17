# Day 5 Results: Ethernet at the MAC/PHY level (raw frames, no TCP/IP stack)

**Board:** NUCLEO-N657X0-Q (STM32N657X0H3Q, Cortex-M55 @ 600 MHz)
**Project:** `Day5_n6` (created from Day 1 with CubeMX "Save Project As")
**Runs from:** internal RAM, development boot. No permanent change to the board:
no fuses, no flash writes. D-cache left **off** on purpose (see "Cache coherence").

---

## Goal

Drive the on-chip Ethernet MAC directly, without a TCP/IP stack, to see each layer:

```
my code → DMA descriptors → Ethernet MAC → RMII → PHY (LAN8742A) → RJ45 → laptop (Wireshark)
                                   ↑
                               MDIO / MDC (PHY management)
```

This is the same structure as my SmartFusion2 + DP83848 VHDL MAC project, but with a
hardware MAC and ST's HAL instead of my own RTL.

---

## Hardware (from UM3417)

- PHY connected over **RMII**; a 25 MHz oscillator (X4) clocks the PHY, and the PHY
  supplies the **50 MHz RMII reference clock** to the MCU.

| Signal | Pin | Note |
|---|---|---|
| REF_CLK | PF7 | 50 MHz from PHY |
| MDIO | PF4 | **CubeMX first chose PD12: wrong, corrected** |
| MDC | PG11 | **CubeMX first chose PD1: wrong, corrected** |
| CRS_DV | PF10 | |
| RXD0 / RXD1 | PF14 / PF15 | |
| TX_EN | PF11 | |
| TXD0 / TXD1 | PF12 / PF13 | |
| ETH1_CLK | PF5 | as in ST's own examples for this board |

**Lesson (again):** CubeMX's automatic pin choice was wrong for this board. With MDC/MDIO
on PD1/PD12, the PHY would never have answered.

## Configuration choices, and why

| Setting | Value | Why |
|---|---|---|
| MAC address | `02:00:00:4E:36:01` | First byte `02` = **locally administered** address, correct for a test device |
| RX buffer length | 1536 | Holds any standard frame (max 1518/1522 bytes) in one buffer |
| ETH interrupt | off | Polling keeps the flow easy to follow |
| D-cache / MPU | off | Avoid DMA cache-coherence problems (below) |
| Linker script | added `.eth_desc` section, 32-byte aligned, in RAM | GCC places the DMA descriptors in named sections; the stock script does not place them |
| RIF (security) | ETH1 DMA master + ETH1 registers set **secure, privileged** | The FSBL and its RAM are secure. The Ethernet DMA is a separate bus master and must be allowed to reach that RAM. Same settings as ST's examples. These registers reset at power-off |

---

## Exercise 1: MDIO, PHY registers, auto-negotiation

The code scans MDIO addresses 0–31 and reads the standard IEEE 802.3 **Clause 22**
registers.

### Before the cable is connected
```
PHY at MDIO address 0
  reg0 BMCR   = 0x3000  auto-neg enabled
  reg1 BMSR   = 0x7809  link down, auto-neg not complete
  reg2 ID1    = 0x0007
  reg3 ID2    = 0xC131  (model 0x13, revision 1)
  reg4 ANAR   = 0x01E1  we advertise
  reg5 ANLPAR = 0x0001  partner advertises
  reg6 ANER   = 0x0064
```

| Register | Decoded |
|---|---|
| Address 0 | PHY address straps set to 0 |
| ID 0x0007 / 0xC131 | **Microchip LAN8742A**, revision 1 (model 0x13) |
| BMCR 0x3000 | bit 13 speed 100, **bit 12 auto-negotiation enabled** |
| BMSR 0x7809 | bits 14–11: supports 100FD/100HD/10FD/10HD; bit 3: can auto-negotiate; **bit 2 = 0: link down** |
| ANAR 0x01E1 | advertises all four modes; selector `00001` = IEEE 802.3 |
| ANLPAR 0x0001 | no partner yet |

BMSR's link bit is **latched low**: the first read reports any drop since the last read,
so the code reads it twice to get the current state.

### Plugging and unplugging the cable
```
[    44 ms] LINK DOWN
[353605 ms] LINK UP   100 Mbit/s full duplex  (ANLPAR=0xC1E1)
[362254 ms] LINK DOWN
[363864 ms] LINK UP   100 Mbit/s full duplex  (ANLPAR=0xC1E1)
[396231 ms] LINK DOWN
```

**Laptop's ANLPAR = 0xC1E1:** bits 8–5 all four modes, bit 14 ACK (handshake complete),
bit 15 next page, **bit 10 = 0 (no pause/flow control)**. I had predicted `0x45E1` or
`0xC5E1` (assuming pause support); the real value showed otherwise.

**Auto-negotiation result:** `ANAR & ANLPAR = 0x01E1`, highest common bit 8 →
**100 Mbit/s full duplex**. The MAC is then configured to the same speed and duplex
before starting; a mismatch would corrupt frames.

---

## Exercise 2: transmitting raw frames

A broadcast frame with EtherType **0x88B5** (IEEE "local experimental") is sent every
second through `HAL_ETH_Transmit`, with hardware CRC and padding enabled and checksum
insertion disabled (the frame has no IP header).

### Console
```
Link UP: 100 Mbit/s full duplex
MAC + DMA started
frame 1 sent (55 bytes before pad/CRC), ok=1, err=0x0
  bytes: FF FF FF FF FF FF 02 00 00 4E 36 01 88 B5 48 65 6C 6C 6F 20 66 72 6F 6D ...
frame 2 sent (57 bytes before pad/CRC), ok=2, err=0x0
...
frame 18 sent (59 bytes before pad/CRC), ok=18, err=0x0
```

### Wireshark (laptop, filter `eth.type == 0x88b5`)
```
Frame 12: 60 bytes on wire
Ethernet II, Src: 02:00:00:4e:36:01, Dst: Broadcast (ff:ff:ff:ff:ff:ff)
Type 0x88b5  Local Experimental Ethertype 1

ff ff ff ff ff ff 02 00 00 4e 36 01 88 b5 48 65   ...........N6..He
6c 6c 6f 20 66 72 6f 6d 20 53 54 4d 33 32 4e 36   llo from STM32N6
20 66 72 61 6d 65 20 23 31 2c 20 75 70 74 69 6d    frame #1, uptim
65 20 33 33 20 6d 73 00 00 00 00 00               e 33 ms.....
```

**What this proves:**
- **Hardware padding:** the frame was 55 bytes (14 header + 41 text). Ethernet's
  minimum is 60 bytes + 4-byte CRC. Wireshark shows 60 bytes ending in **5 zero bytes**
  added by the MAC.
- **Hardware CRC:** the laptop's network card checks and strips the FCS, so Wireshark
  shows 60 bytes, not 64. A wrong CRC would have been dropped silently; receiving the
  frames proves the CRC was correct.
- **Frame length grows** (55 → 57 → 58 → 59) as the frame number and uptime gain
  digits. All are under 60, so all are padded.
- **Spacing ≈ 1.016 s**, not 1.000 s: `HAL_Delay(1000)` waits up to 1 ms extra (Day 2)
  and the `printf` lines take about 10 ms at 115200 baud. Software timing drifts; the
  Day 3 hardware timer did not.

The first test showed no frames, because the board was still running Exercise 1
(the exercise number had not been rebuilt). The console banner identified this.

---

## Exercise 3: receiving in promiscuous mode

```
Promiscuous mode ON. Printing every received frame:
#1     112 B  C8:5B:76:B7:D8:6E -> 01:00:5E:00:00:FB  type 0x0800 IPv4
#2     157 B  C8:5B:76:B7:D8:6E -> 33:33:00:01:00:02  type 0x86DD IPv6
#3     344 B  C8:5B:76:B7:D8:6E -> FF:FF:FF:FF:FF:FF  type 0x0800 IPv4
```

Source `C8:5B:76:B7:D8:6E` is the laptop. The lengths match Wireshark's capture of
the laptop's own traffic exactly:

| # | Destination MAC | Matching Wireshark packet | Why that address |
|---|---|---|---|
| 1 | `01:00:5E:00:00:FB` | **mDNS** to 224.0.0.251, 112 bytes | IPv4 multicast maps to `01:00:5E` + low 23 bits of the IP (`0.0.251` → `00:00:FB`) |
| 2 | `33:33:00:01:00:02` | **DHCPv6 Solicit** to ff02::1:2, 157 bytes | IPv6 multicast maps to `33:33` + last 32 bits of the IP |
| 3 | `FF:FF:FF:FF:FF:FF` | **DHCP Discover**, 344 bytes | Broadcast: the laptop is looking for a DHCP server |

- The received lengths equal Wireshark's (no FCS), so the MAC **strips the CRC on
  receive** as well.
- Frames 1 and 2 are multicast. Without promiscuous mode (or "pass all multicast"),
  the MAC's address filter would have dropped them; the broadcast frame would still
  pass.
- Since nothing answers its DHCP request, Windows falls back to a **link-local**
  address (169.254.x.x), seen in the Wireshark capture.

### RX buffer handling
The HAL asks the application for receive buffers through `HAL_ETH_RxAllocateCallback`
and returns filled ones through `HAL_ETH_RxLinkCallback`. A pool of 12 × 1536-byte
buffers is used (2 DMA channels × 4 descriptors, plus spares); each buffer is freed
after the frame is printed.

---

## Cache coherence (why the D-cache was off)

The Ethernet DMA reads and writes RAM **directly, bypassing the CPU's data cache**.
With the D-cache on:
- **TX:** the CPU writes a frame into the cache; the DMA reads stale RAM and sends old
  data, unless the buffer is **cleaned** (`SCB_CleanDCache_by_Addr`) first.
- **RX:** the DMA writes a new frame to RAM; the CPU reads old cached data, unless the
  buffer is **invalidated** (`SCB_InvalidateDCache_by_Addr`) first.
- **Descriptors** are shared continuously, so ST's examples place them in a region
  marked **non-cacheable** with the MPU.

Day 3 showed the caches give an 8.6× speed-up, so a real product keeps them on and
handles coherence explicitly. Here they were left off to remove that variable.

---

## Interview questions answered by this exercise

**What do the MAC and the PHY each do?**
The MAC handles frames: addresses, padding, CRC, filtering, DMA to memory. The PHY
handles the physical signal: line coding, auto-negotiation, link detection. They talk
over MII/RMII for data and MDIO/MDC for management.

**What is RMII?**
Reduced Media Independent Interface: 2 data bits each way at a 50 MHz reference clock
(= 100 Mbit/s), plus TX_EN and CRS_DV. Fewer pins than MII (4 bits at 25 MHz).

**What is MDIO, and how do you find a PHY?**
A 2-wire management bus (MDC clock, MDIO data) with 32 addresses and 32 registers per
PHY. Scan the addresses and read ID1/ID2; a PHY answers with a value that is not
0x0000 or 0xFFFF.

**How does auto-negotiation work?**
Each side advertises its abilities (ANAR); the other stores them (ANLPAR). Both choose
the highest mode in common. The MAC must then be configured to that speed/duplex.

**Why does Ethernet have a 64-byte minimum frame?**
So that a collision on a maximum-length shared segment is detected while the sender is
still transmitting (CSMA/CD). Short frames are padded.

**What is a locally administered MAC address?**
One with bit 1 of the first byte set (e.g. `02:...`). It is not from an IEEE-assigned
vendor range, so it is the right choice for test devices.

**Why must DMA buffers be handled carefully with a data cache?**
DMA bypasses the cache, so the CPU and DMA can see different data. Clean before DMA
reads, invalidate before CPU reads, or use non-cacheable memory.

---

## Key takeaways
1. A full Ethernet path worked end to end: TX seen in Wireshark, RX matching the
   laptop's traffic byte-for-byte in length.
2. Padding and CRC are done by the MAC hardware and were verified on the wire.
3. Standard PHY registers (Clause 22) are the same on every PHY, so this code applies
   to the DP83848 in my SmartFusion2 project.
4. Check CubeMX's pin choices: MDC/MDIO were wrong again.
5. DMA plus data cache means coherence work. Know clean vs invalidate.
