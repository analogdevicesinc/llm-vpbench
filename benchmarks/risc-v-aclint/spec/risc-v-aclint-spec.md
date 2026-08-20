<!-- SPDX-License-Identifier: Apache-2.0 -->
# RISC-V ACLINT - SystemC TLM-2.0 Specification

| Field     | Value                                           |
|-----------|-------------------------------------------------|
| Benchmark | LLM-VPBench                                     |
| Tier      | Simple                                          |
| Reference | RISC-V ACLINT Specification v1.0-rc4 (ratified) |

This IP implements the three ACLINT devices - MSWI, MTIMER, and SSWI - as independent SystemC TLM-2.0 blocking-transport modules. The implementation is parameterized to N harts (default N=8).

---

## 1. MSWI Device - Machine-level Software Interrupt

Per ACLINT spec §4: the MSWI device provides machine-level IPI functionality via per-hart MSIP registers. Each MSIP register is 32-bit WARL; upper 31 bits wired to zero, bit[0] reflected in the hart's machine software interrupt pending (per ACLINT spec §4.1).

### 1.1 Register Map

| Offset | Name | Width | Reset | Description |
|--------|------|-------|-------|-------------|
| 0x0000 + i×4 | MSIP[i] | 32-bit | 0x0 | Machine software interrupt pending for hart i. Bit[0] RW (WARL), bits[31:1] hardwired zero. |

Valid hart indices: 0 to N−1.

### 1.2 Address Space

Per ACLINT spec §4, the MSWI device occupies a 16 KiB (0x4000) region. Only offsets 0x0000 through (N−1)×4 are populated. Accesses to unpopulated offsets within the window return an address error.

### 1.3 Behavior

- **Write MSIP[i]**: Stores bit[0] only. Immediately drives `sw_irq[i]` to the new value.
- **Read MSIP[i]**: Returns 0x00000000 or 0x00000001.
- **Interrupt**: `sw_irq[i]` is level-sensitive, active-high, directly tracking MSIP[i] bit[0]. No masking.

### 1.4 Access Constraints

- Only 4-byte aligned, 4-byte transactions accepted.
- Unaligned or out-of-range offset → `TLM_ADDRESS_ERROR_RESPONSE`.
- Data length ≠ 4 → `TLM_GENERIC_ERROR_RESPONSE`.

---

## 2. MTIMER Device - Machine-level Timer

Per ACLINT spec §2: the MTIMER device provides a single shared 64-bit monotonic counter (MTIME) and per-hart 64-bit compare registers (MTIMECMP). A machine timer interrupt is pending for hart i when MTIME ≥ MTIMECMP[i] (per ACLINT spec §2.1).

### 2.1 Register Map

| Offset | Name | Width | Reset | Description |
|--------|------|-------|-------|-------------|
| 0x0000 + i×8 | MTIMECMP[i] | 64-bit | 0xFFFF_FFFF_FFFF_FFFF | Per-hart compare. IRQ asserts when MTIME ≥ MTIMECMP[i]. |
| 0x7FF8 | MTIME | 64-bit | 0x0 | Shared wall-clock counter. Auto-increments when enabled. Writable for calibration. |

Per ACLINT spec §2, MTIME is at fixed offset 0x7FF8 within the 32 KiB device region, and MTIMECMP registers begin at offset 0x0000.

#### Benchmark Extension Registers

The following registers are **not** part of the RISC-V ACLINT specification. They are benchmark-specific extensions placed in the gap between MTIMECMP and MTIME:

| Offset | Name | Width | Reset | Description |
|--------|------|-------|-------|-------------|
| 0x7FE0 | overflow_flag | 32-bit | 0x0 | Bit[0]: latched on MTIME wrap (0xFFFF…F → 0). Read-to-clear. Writes ignored. |
| 0x7FE8 | timer_enable | 32-bit | 0x1 | Bit[0]: 1=counting (default), 0=frozen. WARL, bits[31:1] zero. |
| 0x7FF0 | prescaler | 32-bit | 0x1 | Clock divider. MTIME increments every prescaler × tick_period. 32-bit register. Bits[15:0] writable; bits[31:16] hardwired to zero, always read as zero. WARL: writes of 0 are stored as 1; writes > 65535 are stored as 65535. |

### 2.2 Address Space

The device occupies 32 KiB (0x8000) per ACLINT spec §2. Regions between the end of MTIMECMP[N−1] and 0x7FE0, and gaps between extension control registers, are reserved - access returns `TLM_ADDRESS_ERROR_RESPONSE`.

### 2.3 64-bit Split Access

Per RISC-V convention, 64-bit registers (MTIMECMP, MTIME) support both 4-byte and 8-byte transactions:

- 4-byte at offset aligned to 8: accesses bits[31:0], preserving bits[63:32] on write.
- 4-byte at offset aligned to 8 + 4: accesses bits[63:32], preserving bits[31:0] on write.
- 8-byte at offset aligned to 8: atomic full-width access.

The 32-bit extension registers accept 4-byte transactions only.

### 2.4 Write Behavior

| Register | Effect |
|----------|--------|
| MTIMECMP[i] | Updates compare value. Re-evaluates `timer_irq[i]`. |
| MTIME | Overwrites counter. Re-evaluates all `timer_irq[0..N−1]`. |
| prescaler | WARL-clamps, stores. Next tick uses new divisor. |
| timer_enable | Stores bit[0]. 0 freezes MTIME; 1 resumes. When timer_enable transitions from 0 to 1, all timer_irq outputs are immediately re-evaluated against the current mtime and mtimecmp values. |
| overflow_flag | Write ignored; returns `TLM_OK_RESPONSE`. |

### 2.5 Read Behavior

All reads return current register value. `overflow_flag`: read returns the current value (0 or 1), then clears the flag to 0 within the same b_transport call. A subsequent read without an intervening overflow returns 0. Multiple overflows between reads still yield a single `1`.

### 2.6 Timer Increment Logic

- MTIME increments by 1 every (prescaler × tick_period) simulation time when `timer_enable` bit[0] = 1.
- On wrap from 0xFFFF_FFFF_FFFF_FFFF to 0x0: hardware sets `overflow_flag` bit[0].
- After each increment, all N `timer_irq` outputs are re-evaluated.
- MTIMER uses an SC_THREAD (`timer_thread`) that loops with `wait(prescaler * tick)`. On each iteration, MTIME increments by 1 and all timer_irq outputs are re-evaluated. Between b_transport calls, simulation time does not advance unless the testbench calls `sc_start()` or `wait()` - the testbench is responsible for advancing time.
- If `tick` is `SC_ZERO_TIME`, the timer thread does not run (MTIME is software-managed only via register writes).

### 2.7 Timer Interrupt Generation

Per ACLINT spec §2.1:

- `timer_irq[i]` asserted when MTIME ≥ MTIMECMP[i] (unsigned 64-bit comparison).
- `timer_irq[i]` deasserted when MTIME < MTIMECMP[i].
- Level-sensitive. Comparison is always active regardless of `timer_enable` state.
- Resetting MTIMECMP[i] to 0xFFFF_FFFF_FFFF_FFFF effectively masks the interrupt.

### 2.8 Access Constraints

- 64-bit registers: 4-byte (4-aligned) or 8-byte (8-aligned).
- 32-bit extension registers: 4-byte only, 4-aligned.
- Alignment/range violations → `TLM_ADDRESS_ERROR_RESPONSE`.
- Data length not in {4, 8} → `TLM_GENERIC_ERROR_RESPONSE`.

---

## 3. SSWI Device - Supervisor-level Software Interrupt

Per ACLINT spec §5: the SSWI device provides supervisor-level IPI functionality via per-hart SETSSIP registers.

> **Benchmark Deviation**: The official ACLINT spec defines SETSSIP as edge-triggered (write-1-to-set, always reads 0). This benchmark simplifies SSWI to behave identically to MSWI: the register retains written state and the interrupt is level-sensitive. This simplification enables deterministic testbench verification.

### 3.1 Register Map

| Offset | Name | Width | Reset | Description |
|--------|------|-------|-------|-------------|
| 0x0000 + i×4 | SETSSIP[i] | 32-bit | 0x0 | Supervisor software interrupt pending for hart i. Bit[0] RW (WARL), bits[31:1] hardwired zero. |

Valid hart indices: 0 to N−1.

### 3.2 Address Space

Per ACLINT spec §5, the SSWI device occupies 16 KiB (0x4000). Only offsets 0x0000 through (N−1)×4 populated.

### 3.3 Behavior

- **Write SETSSIP[i]**: Stores bit[0]. Immediately drives `ssw_irq[i]`.
- **Read SETSSIP[i]**: Returns 0x00000000 or 0x00000001.
- **Interrupt**: `ssw_irq[i]` is level-sensitive, active-high, directly tracking SETSSIP[i] bit[0].

### 3.4 Access Constraints

Same as MSWI (§1.4).

---

## 4. SystemC TLM-2.0 Interface

All three modules use `b_transport` (blocking transport). Zero simulation-time delay on all register accesses.

### 4.1 MSWI - `aclint_mswi`

| Port/Socket | Type | Count |
|-------------|------|-------|
| `socket` | `tlm_utils::simple_target_socket<aclint_mswi>` | 1 |
| `sw_irq` | `sc_core::sc_vector<sc_core::sc_out<bool>>` | N |

Constructor: `aclint_mswi(sc_module_name name, unsigned int num_harts = 8)`

### 4.2 MTIMER - `aclint_mtimer`

| Port/Socket | Type | Count |
|-------------|------|-------|
| `socket` | `tlm_utils::simple_target_socket<aclint_mtimer>` | 1 |
| `timer_irq` | `sc_core::sc_vector<sc_core::sc_out<bool>>` | N |

Constructor: `aclint_mtimer(sc_module_name name, sc_time tick = sc_time(1, SC_US), unsigned int num_harts = 8)`

The `tick` parameter defines the base MTIME increment period. Effective period = prescaler × tick.

### 4.3 SSWI - `aclint_sswi`

| Port/Socket | Type | Count |
|-------------|------|-------|
| `socket` | `tlm_utils::simple_target_socket<aclint_sswi>` | 1 |
| `ssw_irq` | `sc_core::sc_vector<sc_core::sc_out<bool>>` | N |

Constructor: `aclint_sswi(sc_module_name name, unsigned int num_harts = 8)`

---

## 5. Deliverables

| File | Module | Notes |
|------|--------|-------|
| `aclint_mswi.h` | `aclint_mswi` | Header-only, self-contained. |
| `aclint_mtimer.h` | `aclint_mtimer` | Header-only, self-contained. |
| `aclint_sswi.h` | `aclint_sswi` | Header-only, self-contained. |

No separate `.cpp` files. Each header includes all necessary SystemC/TLM headers.

> **Interface Contract**: The mandatory interface is defined in `interface/aclint_mswi.h`, `interface/aclint_mtimer.h`, and `interface/aclint_sswi.h`. LLM submissions must conform to these exact class names, socket names, port names, and constructor signatures. The testbench binds directly to these interfaces.

---

## 6. Device Independence

The three devices share no state. Writing to one device has no side-effect on another. Each has its own address space, register file, and interrupt outputs.

---

## 7. Error Response Summary

Error conditions are evaluated in the order listed; first match determines the response.

| Priority | Condition | Response |
|----------|-----------|----------|
| 1 | `TLM_IGNORE_COMMAND` | `TLM_GENERIC_ERROR_RESPONSE` |
| 2 | Unsupported data length | `TLM_GENERIC_ERROR_RESPONSE` |
| 3 | 8-byte access to a 32-bit extension register (timer_enable, prescaler, overflow_flag) | `TLM_GENERIC_ERROR_RESPONSE` |
| 4 | Address out of range, in reserved gap, or misaligned | `TLM_ADDRESS_ERROR_RESPONSE` |
| 5 | Valid read/write | `TLM_OK_RESPONSE` |

**Notes:**
- Misaligned accesses consistently return `TLM_ADDRESS_ERROR_RESPONSE`.
- Extension registers (timer_enable, prescaler, overflow_flag) accept **only 4-byte accesses**. An 8-byte access to an extension register offset returns `TLM_GENERIC_ERROR_RESPONSE`.
- All register accesses are **little-endian**.
