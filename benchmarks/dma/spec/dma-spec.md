<!-- SPDX-License-Identifier: Apache-2.0 -->
# Multi-Channel DMA Controller - SystemC TLM-2.0 Specification

| Field     | Value                                                      |
|-----------|------------------------------------------------------------|
| Benchmark | LLM-VPBench                                                |
| Tier      | Complex                                                    |
| Reference | ARM DMA-330 TRM (DDI 0424): channel FSM, priority arbiter; ARM AMBA: interrupt conventions; custom extensions: SUSPENDED reprogramming, CH_WRAP_COUNT, linked mode |

---

## Terminology

| Term | Definition |
|------|-----------|
| WARL | Write Any, Read Legal. Writes to reserved fields are discarded; reads return the legal (reset) value. All reserved register fields in this specification use WARL semantics unless stated otherwise. |
| SC   | Self-clearing. Write-1-to-trigger; always reads as 0. |
| IMPLEMENTATION DEFINED | Behavior left to the implementation; not tested by the benchmark. |

---

## 1. Overview

A 4-channel DMA controller modeled as a SystemC TLM-2.0 IP. Derived from ARM DMA-330 channel state machine and priority arbitration concepts, adapted for a register-programmed (non-microcode) interface.

**Features:**
- 4 independent channels, each with a 6-state FSM
- Priority-based arbitration (2-bit priority, round-robin tie-break) with starvation prevention
- Burst transfers: 1, 4, 8, or 16 beats; widths: 1, 2, or 4 bytes
- Per-source/destination address mode: increment, decrement, or fixed
- Circular mode with hardware wrap counter
- Linked-list channel chaining
- Per-channel maskable interrupts: completion, half-transfer, error

---

## 2. SystemC/TLM-2.0 Interface

| Port / Socket      | Type                             | Dir   | Description |
|--------------------|----------------------------------|-------|-------------|
| `target_socket`    | `simple_target_socket`           | In    | Register access (b_transport, zero-delay). |
| `initiator_socket` | `simple_initiator_socket`        | Out   | Bus-mastered memory transfers (b_transport). |
| `irq`              | `sc_vector<sc_out<bool>>`        | Out×4 | Per-channel interrupt, active-high, level. |
| `irq_global`       | `sc_out<bool>`                   | Out   | OR of all unmasked channel interrupts. |

**Constructor:** `dma_controller(sc_module_name name)`

**Deliverable:** Single header file `dma_controller.h`.

> **Interface Contract**: The mandatory interface is defined in `interface/dma_controller.h`. LLM submissions must conform to this exact class name, socket names (`target_socket`, `initiator_socket`, `irq`, `irq_global`), and constructor signature.

All channel threads share a single `initiator_socket`. Access is serialized via an internal `sc_mutex`. Before issuing a burst, a channel thread acquires the mutex; after b_transport returns, it releases the mutex. The arbitration logic (§9) determines which waiting thread acquires the mutex next.

---

## 3. Register Map

Address space: 0x000-0x1FF. All registers are 32-bit, 4-byte aligned.
Access to undefined offsets returns TLM_ADDRESS_ERROR_RESPONSE.

### 3.1 Global Registers

| Offset | Name        | Access | Reset       |
|--------|-------------|--------|-------------|
| 0x000  | STATUS      | RO     | 0x0000_0000 |
| 0x004  | INT_STATUS  | RO     | 0x0000_0000 |
| 0x008  | INT_CLEAR   | WO     | -           |
| 0x00C  | GLOBAL_CTRL | RW     | 0x0000_0000 |
| 0x010  | VERSION     | RO     | 0x444D4102  |
| 0x014  | INT_MASK    | RW     | 0x0000_0FFF |

**STATUS[3:0]:** Bit N = 1 when channel N is RUNNING or SUSPENDED. Bits[31:4] zero.

**GLOBAL_CTRL:**

| Bits   | Field    | Type  | Description |
|--------|----------|-------|-------------|
| [0]    | ENABLE   | RW    | Gate for new START commands. In-progress transfers unaffected. |
| [1]    | RESET    | RW/SC | Global reset trigger. Self-clears. See §10. |
| [31:2] | -        | WARL  | Reserved. |

**VERSION:** ASCII `"DMA"` || 0x02. Read-only; writes ignored, TLM_OK_RESPONSE returned.

### 3.2 Per-Channel Registers

Base for channel N: `0x100 + N × 0x40`, N ∈ {0,1,2,3}.

| Offset | Name           | Access | Reset       |
|--------|----------------|--------|-------------|
| +0x00  | CH_SRC_ADDR    | RW     | 0x0000_0000 |
| +0x04  | CH_DST_ADDR    | RW     | 0x0000_0000 |
| +0x08  | CH_XFER_SIZE   | RW     | 0x0000_0000 |
| +0x0C  | CH_CTRL        | RW     | 0x0000_0000 |
| +0x10  | CH_CONFIG      | RW     | 0x0000_0000 |
| +0x14  | CH_STATUS      | RO     | 0x0000_0000 |
| +0x18  | CH_CURR_SRC    | RO     | 0x0000_0000 |
| +0x1C  | CH_CURR_DST    | RO     | 0x0000_0000 |
| +0x20  | CH_REMAINING   | RO     | 0x0000_0000 |
| +0x24  | CH_WRAP_COUNT  | RO     | 0x0000_0000 |
| +0x28  | CH_LINK        | RW     | 0x0000_0000 |
| +0x2C-+0x3C | Reserved  | RO     | 0x0000_0000 |

**CH_XFER_SIZE:** Bits[16:0] writable. Values exceeding 0x10000 are clamped to 0x10000 on write. Bits[31:17] always read zero.

### 3.3 CH_CTRL

| Bits    | Field    | Type  | Description |
|---------|----------|-------|-------------|
| [0]     | START    | RW/SC | Request transfer start. |
| [1]     | CIRCULAR | RW    | Enable circular mode. |
| [2]     | PAUSE    | RW/SC | Request pause. |
| [3]     | RESUME   | RW/SC | Request resume. |
| [4]     | ABORT    | RW/SC | Abort transfer → ERROR. |
| [5]     | CLR_ERR  | RW/SC | Clear ERROR state → IDLE. |
| [7:6]   | WIDTH    | RW    | 0=1B, 1=2B, 2=4B, 3=reserved (config error). |
| [9:8]   | BURST    | RW    | 0=1 beat, 1=4, 2=8, 3=16 beats. |
| [11:10] | PRIORITY | RW    | 0=lowest, 3=highest. Dynamic. |
| [31:12] | -        | WARL  | Reserved. |

**Write restrictions while RUNNING/SUSPENDED:** Only CIRCULAR, PAUSE, RESUME, ABORT, PRIORITY are writable. Writes to WIDTH, BURST, START, CLR_ERR are ignored.

### 3.4 CH_CONFIG

| Bits   | Field    | Type | Description |
|--------|----------|------|-------------|
| [1:0]  | SRC_MODE | RW   | 0=inc, 1=dec, 2=fixed. 3 treated as fixed. |
| [3:2]  | DST_MODE | RW   | 0=inc, 1=dec, 2=fixed. 3 treated as fixed. |
| [31:4] | -        | WARL | Reserved. |

CH_CONFIG is writable only in IDLE or CONFIGURED states. Writes in other states are ignored.

### 3.5 CH_STATUS

| Bits   | Field    | Description |
|--------|----------|-------------|
| [0]    | BUSY     | 1 if RUNNING or SUSPENDED. |
| [1]    | ERROR    | 1 if ERROR state. |
| [2]    | COMPLETE | 1 if COMPLETE state. |
| [4:3]  | STATE    | 0=IDLE, 1=CONFIGURED, 2=RUNNING, 3=SUSPENDED. |
| [31:5] | -        | Reserved, zero. |

### 3.6 CH_LINK

| Bits   | Field   | Description |
|--------|---------|-------------|
| [1:0]  | TARGET  | Target channel index (0-3). |
| [6:2]  | -       | Reserved, zero. |
| [7]    | ENABLE  | 1=linked-start enabled. |
| [31:8] | -       | Reserved, zero. |

Reset value: 0x00. ENABLE=0 (link disabled), TARGET=0.

---

## 4. Channel State Machine

Derived from ARM DMA-330 channel thread states, simplified to a register-programmed model.

### 4.1 States

| State      | Encoding | Description |
|------------|----------|-------------|
| IDLE       | 0        | No active transfer. All registers writable. |
| CONFIGURED | 1        | At least one data register written since IDLE. |
| RUNNING    | 2        | Actively transferring. |
| SUSPENDED  | 3        | Paused. Transfer state preserved. |
| COMPLETE   | -        | Transfer done. IDLE + COMPLETE bit set. |
| ERROR      | -        | Fault. IDLE + ERROR bit set. Requires CLR_ERR. |

### 4.2 Transitions

| From        | To         | Condition |
|-------------|------------|-----------|
| IDLE        | CONFIGURED | Write to any channel data register (SRC, DST, SIZE, CTRL non-SC bits, CONFIG). |
| IDLE        | RUNNING    | START ∧ ENABLE ∧ validation passes (§5). Writing START from IDLE implicitly transitions through CONFIGURED (validation occurs as if CONFIGURED). |
| CONFIGURED  | RUNNING    | START ∧ ENABLE ∧ validation passes (§5). |
| CONFIGURED  | ERROR      | START ∧ validation fails (§5). |
| RUNNING     | SUSPENDED  | PAUSE written. Effective after current burst completes. |
| RUNNING     | COMPLETE   | REMAINING=0 ∧ CIRCULAR=0. |
| RUNNING     | RUNNING    | REMAINING=0 ∧ CIRCULAR=1 (wrap). |
| RUNNING     | ERROR      | Bus error or ABORT. |
| SUSPENDED   | RUNNING    | RESUME written. |
| SUSPENDED   | ERROR      | ABORT written. |
| COMPLETE    | IDLE       | Any register write (implicit ack). The COMPLETE state is distinguished from IDLE by the COMPLETE status bit (CH_STATUS[5]=1, STATE=0). Any 4-byte register WRITE to this channel's address space (0x100 + ch×0x40 to 0x100 + ch×0x40 + 0x28) transitions COMPLETE → IDLE and clears the COMPLETE bit. |
| ERROR       | IDLE       | CLR_ERR written. |
| Any         | IDLE       | Global reset. |

### 4.3 Register Accessibility by State

| Register     | IDLE/CFG | RUNNING | SUSPENDED | COMPLETE | ERROR |
|--------------|----------|---------|-----------|----------|-------|
| SRC/DST/SIZE | RW       | Ignored | RW†       | RW       | Ignored |
| CH_CTRL      | Full RW  | Partial | Partial   | Full RW  | CLR_ERR only |
| CH_CONFIG    | RW       | Ignored | Ignored   | RW       | Ignored |

†**SUSPENDED reprogramming (custom):** When START is issued, the current CH_XFER_SIZE value is captured into an internal shadow register (`original_size`). This shadow is not software-visible. During SUSPENDED state, writing a new value to CH_XFER_SIZE triggers: `CH_REMAINING = new_xfer_size - (original_size - CH_REMAINING)`. The shadow register (`original_size`) is then updated to `new_xfer_size`. If result ≤ 0, REMAINING is set to 0 and completion triggers on RESUME. The resulting REMAINING value must remain a multiple of the configured WIDTH. If not, it is rounded down (truncated to the nearest lower multiple of WIDTH). This prevents buffer overruns. Writing SRC/DST updates the corresponding CURR register.

All ignored writes return TLM_OK_RESPONSE.

---

## 5. Transfer Validation

On START (channel in IDLE or CONFIGURED, GLOBAL_CTRL.ENABLE=1):

1. WIDTH ≠ 3 (reserved value → ERROR)
2. CH_XFER_SIZE > 0 (zero → ERROR)
3. CH_XFER_SIZE ≤ 65536 (overflow → ERROR)
4. CH_XFER_SIZE is a multiple of width bytes (misalign → ERROR)
5. CH_SRC_ADDR aligned to width (misalign → ERROR)
6. CH_DST_ADDR aligned to width (misalign → ERROR)

Checks evaluated in order; first failure aborts. On failure: state → ERROR, no interrupt generated. On success: state → RUNNING; CURR registers loaded from SRC/DST; REMAINING = SIZE; WRAP_COUNT = 0.

START is ignored when: channel is RUNNING/SUSPENDED, or GLOBAL_CTRL.ENABLE=0.

Writing START to a COMPLETE channel: implicit ack (→ IDLE → CONFIGURED), then validation runs.

---

## 6. Transfer Execution

### 6.1 Execution Model

One beat = one read transaction followed by one write transaction (blocking, serialized). At most one outstanding transaction at any time.

**Burst execution** for the selected channel:
- `beats = min(burst_length, REMAINING / width)`
- For each beat: read from CURR_SRC, write to CURR_DST, update addresses per mode, decrement REMAINING by width.
- Address update: increment adds width, decrement subtracts width (unsigned wrap on underflow), fixed holds constant.

**Inter-burst arbitration:** After each burst completes, the arbiter re-evaluates. An IMPLEMENTATION DEFINED inter-burst delay models bus turnaround.

### 6.2 Completion

When REMAINING reaches 0:
- **CIRCULAR=0:** State → COMPLETE. BUSY cleared. Completion interrupt set. If linked mode enabled, target channel receives implicit START (§7.2).
- **CIRCULAR=1:** WRAP_COUNT incremented. CURR registers reload from SRC/DST. REMAINING reloads from SIZE. Completion interrupt set. State remains RUNNING.

### 6.3 Half-Transfer Interrupt

Set when REMAINING first crosses below SIZE/2 (more than half transferred). Fires once per transfer iteration (resets on circular wrap).

### 6.4 Partial Burst

If REMAINING < width × burst_length, the burst is shortened to `REMAINING / width` beats. A zero-beat burst cannot occur (would imply REMAINING=0, caught as completion).

### 6.5 Pause Semantics

PAUSE takes effect after the current burst completes. If the final burst causes REMAINING=0, completion takes priority over pause.

---

## 7. Special Modes

### 7.1 Circular Mode

Enabled via CH_CTRL.CIRCULAR. On each wrap: addresses reset, REMAINING reloads, WRAP_COUNT increments.

**Stopping:** Clear CIRCULAR while RUNNING. Current iteration completes normally, then state → COMPLETE.

### 7.2 Linked Mode

When a channel completes (non-circular, CH_LINK.ENABLE=1):
- Target channel (CH_LINK.TARGET) receives implicit START.
- Normal validation applies to the target.
- If target is RUNNING/SUSPENDED: link is ignored.
- Self-linking (target = self): creates COMPLETE→CONFIGURED→RUNNING loop.
- Link fires **after** completion interrupt is generated.
- Link bypasses the GLOBAL_CTRL.ENABLE gate.
- Circular mode takes precedence: link only triggers on non-circular completion.
- No hardware cycle detection. Software responsibility.

---

## 8. Interrupt System

Follows ARM AMBA interrupt conventions: sticky status bits, explicit clear, mask register.

### 8.1 Sources

| INT_STATUS Bit | Source        | Trigger |
|----------------|---------------|---------|
| [N×3 + 0]     | Complete/Wrap | Transfer complete or circular wrap. |
| [N×3 + 1]     | Half-transfer | Half-transfer threshold crossed. |
| [N×3 + 2]     | Error         | Bus error (not config error). |

12 bits used (4 channels × 3 sources). Bits[31:12] zero.

### 8.2 Masking

INT_MASK layout mirrors INT_STATUS. 1 = masked (blocked from output). Reset: 0xFFF (all masked). Masking does not prevent INT_STATUS from being set.

### 8.3 Output Logic

```
irq[N] = |{INT_STATUS[N×3+k] & ~INT_MASK[N×3+k]} for k=0,1,2
irq_global = irq[0] | irq[1] | irq[2] | irq[3]
```

INT_STATUS bits are sticky; cleared by writing 1 to corresponding INT_CLEAR bit.

Outputs re-evaluate immediately on: status set, INT_CLEAR write, INT_MASK write, global reset.

---

## 9. Priority Arbitration

Derived from ARM DMA-330 fixed-priority with round-robin arbitration.

### 9.1 Algorithm

At each arbitration point (between bursts):
1. Collect channels in RUNNING state.
2. Select highest priority P among ready channels.
3. Among channels at priority P: round-robin selection (pointer advances per grant).
4. **Starvation prevention:** After 8 consecutive arbitration rounds where all grants go to channels at the same priority level (regardless of which specific channel), one burst is forced to the highest-priority waiting channel at a lower level.

### 9.2 Dynamic Priority

Changing PRIORITY while RUNNING takes effect at next arbitration. The channel enters the new priority group's round-robin at the tail.

---

## 10. Global Reset

Writing 1 to GLOBAL_CTRL.RESET:
1. All channels → IDLE, all channel registers → reset values.
2. In-progress transfers abort (no error interrupt). Writing GLOBAL_CTRL.RESET=1 while a channel thread is blocked waiting for b_transport to return: the reset takes effect after the pending transaction completes (the thread checks reset status after each burst). All channels transition to IDLE on the next burst boundary.
3. INT_STATUS → 0, INT_MASK → 0xFFF.
4. All IRQ outputs deasserted.
5. GLOBAL_CTRL → 0 (ENABLE cleared, RESET self-clears).
6. VERSION unchanged.

---

## 11. Target Socket Protocol

| Condition | Response |
|-----------|----------|
| Valid read/write, 4-byte, aligned | TLM_OK_RESPONSE |
| Offset > 0x1FF or undefined register | TLM_ADDRESS_ERROR_RESPONSE |
| Length ≠ 4, unaligned | TLM_GENERIC_ERROR_RESPONSE |
| `TLM_IGNORE_COMMAND` | TLM_GENERIC_ERROR_RESPONSE |
| Write to RO register | Ignored, TLM_OK_RESPONSE |
| Read from WO register | Returns 0, TLM_OK_RESPONSE |

`streaming_width` must equal `data_length` or be zero (unset); otherwise `TLM_GENERIC_ERROR_RESPONSE`. Byte enables ignored.

All register accesses are little-endian.

---

## 12. Initiator Socket Protocol

Each beat issues two b_transport calls:
- **Read:** address = CURR_SRC, length = width, TLM_READ_COMMAND.
- **Write:** address = CURR_DST, length = width, TLM_WRITE_COMMAND, data = bytes from read.

Non-TLM_OK_RESPONSE on either → bus error: transfer aborts immediately, state → ERROR, CURR registers reflect failed address, REMAINING includes failed beat's bytes.

---

## 13. Corner Cases

The following architecturally significant scenarios must be handled:

1. **Minimum transfer:** WIDTH=1B, BURST=1, SIZE=1. Single beat, immediate completion.

2. **Maximum transfer:** SIZE=65536 (0x10000). Must complete without overflow.

3. **SUSPENDED reprogramming:** Writing SIZE while SUSPENDED recalculates REMAINING. If new REMAINING ≤ 0, completion triggers on RESUME.

4. **Pause during final burst:** Completion takes priority. State → COMPLETE, not SUSPENDED.

5. **Circular + linked:** Circular takes precedence; link only fires on non-circular completion.

6. **Simultaneous START+ABORT in single write:** START processed first (channel starts), ABORT processed second (→ ERROR). No beats transferred. When multiple SC (self-clearing) bits are written simultaneously, they are processed in ascending bit-position order: START(0), PAUSE(2), RESUME(3), ABORT(4), CLR_ERR(5). Example: Writing START+CLR_ERR from ERROR state: START (bit 0) is processed first - ignored because channel is in ERROR. Then CLR_ERR (bit 5) transitions ERROR → IDLE. Net result: channel in IDLE. To clear error AND restart, software must write CLR_ERR first (one write), then write START separately (second write).

7. **Decrement underflow:** 32-bit unsigned wrap. Address 0x0 − 4 = 0xFFFF_FFFC. No error.

8. **Half-transfer with SIZE=1:** Half threshold is SIZE/2 = 0. Fires simultaneously with completion when REMAINING reaches 0.

---

## 14. Error Handling Summary

| Error Type | Trigger | State | Interrupt |
|------------|---------|-------|-----------|
| Config error | Validation fail at START | → ERROR | None |
| Bus error | Non-OK TLM response | → ERROR | INT_STATUS[N×3+2] |
| Abort | ABORT bit written | → ERROR | INT_STATUS[N×3+2] |

Recovery: CLR_ERR → IDLE. CURR and REMAINING preserved for debug until next START.

---

## 15. Global Enable Semantics

- GLOBAL_CTRL.ENABLE = 0: START commands are ignored. Channels already RUNNING continue. RESUME is still honored.
- GLOBAL_CTRL.ENABLE = 1: Normal operation.
- Transition 0→1 does NOT auto-start any channel; explicit START required.
- Transition 1→0 does NOT pause running channels.
- Linked-channel auto-start bypasses the ENABLE gate.

---

## 16. Post-Reset Register State

After power-on or global reset, the following state is guaranteed:

| Register / Signal       | Value       |
|-------------------------|-------------|
| STATUS                  | 0x0000_0000 |
| INT_STATUS              | 0x0000_0000 |
| GLOBAL_CTRL             | 0x0000_0000 |
| VERSION                 | 0x444D4102  |
| INT_MASK                | 0x0000_0FFF |
| CH_SRC_ADDR[0-3]        | 0x0000_0000 |
| CH_DST_ADDR[0-3]        | 0x0000_0000 |
| CH_XFER_SIZE[0-3]       | 0x0000_0000 |
| CH_CTRL[0-3]            | 0x0000_0000 |
| CH_CONFIG[0-3]          | 0x0000_0000 |
| CH_STATUS[0-3]          | 0x0000_0000 |
| CH_CURR_SRC/DST[0-3]    | 0x0000_0000 |
| CH_REMAINING[0-3]       | 0x0000_0000 |
| CH_WRAP_COUNT[0-3]      | 0x0000_0000 |
| CH_LINK[0-3]            | 0x0000_0000 |
| irq[0-3], irq_global    | deasserted  |
| All channel FSMs        | IDLE        |
