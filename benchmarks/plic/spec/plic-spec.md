<!-- SPDX-License-Identifier: Apache-2.0 -->
# RISC-V PLIC - SystemC TLM-2.0 Specification

| Field     | Value                                            |
|-----------|--------------------------------------------------|
| Benchmark | LLM-VPBench                                      |
| Tier      | Medium                                           |
| Reference | RISC-V PLIC Specification v1.0.0 (ratified)      |

**Brief:** The PLIC aggregates external interrupt sources, applies per-source priority and per-context enable filtering, and presents a single External Interrupt Pending (EIP) signal per hart context. This implementation supports configurable sources (IDs 1-S, S≤1023), a single hart context, and the standard PLIC memory map.

---

## 1. Architecture

The PLIC architecture (§2 of RISC-V PLIC Spec v1.0.0) comprises:

1. **Interrupt Gateways** - one per source. Convert external signals into internal pending requests. At most one request per source can be pending at any time.
2. **PLIC Core** - maintains pending bits, priority comparison, and enable filtering.
3. **Interrupt Notification** - generates the EIP signal to the hart context when a qualifying interrupt exists.

This implementation supports **1 hart context** (context 0). Source IDs are numbered 1 through S; source 0 is reserved and hardwired inactive.

---

## 2. Parameters

| Parameter | Symbol | Default | Range | Description |
|-----------|--------|---------|-------|-------------|
| Number of sources | S | 8 | 1-1023 | External interrupt sources (IDs 1..S) |
| Number of contexts | C | 1 | 1 (fixed) | Hart contexts. Only context 0. |
| Priority bits | P | 3 (fixed) | - | Fixed at 3 bits; priorities 0-7. Not runtime-configurable. |

---

## 3. Register Map

All registers are 32-bit, 32-bit-aligned. All register accesses are little-endian. The memory map follows the standard PLIC layout (§4-§8 of RISC-V PLIC Spec v1.0.0).

### 3.1 Address Layout

| Offset | Name | Description |
|--------|------|-------------|
| `0x000000` | PRIORITY[0] | Reserved (source 0 does not exist). Reads 0, writes ignored. Offset 0x000000 (source ID 0) is a valid address within the priority block. Reads return 0; writes are silently ignored. It does NOT return an error response. |
| `0x000004` | PRIORITY[1] | Priority for source 1 |
| `0x000008` | PRIORITY[2] | Priority for source 2 |
| ... | ... | ... |
| `S × 4` | PRIORITY[S] | Priority for source S |
| `0x001000` | PENDING[0] | Pending bits for sources 0-31 (bit-packed) |
| `0x001004` | PENDING[1] | Pending bits for sources 32-63 (if S > 31) |
| ... | ... | Up to ⌈(S+1)/32⌉ words |
| `0x002000` | ENABLE[0] | Enable bits for context 0, sources 0-31 |
| `0x002004` | ENABLE[1] | Enable bits for context 0, sources 32-63 (if S > 31) |
| ... | ... | Up to ⌈(S+1)/32⌉ words |
| `0x200000` | THRESHOLD | Priority threshold for context 0 |
| `0x200004` | CLAIM/COMPLETE | Claim (read) / Complete (write) for context 0 |
| `0x003000` | INT_TYPE[0] | **(Benchmark Extension)** Trigger type, sources 0-31 |
| `0x003004` | INT_TYPE[1] | **(Benchmark Extension)** Trigger type, sources 32-63 (if S > 31) |

### 3.2 Register Details

**PRIORITY[n]** (n = 1..S) - RW, WARL. Reset: 0.
- Bits [P−1:0] writable; upper bits read-as-zero/write-ignored.
- All values 0 to 2^P−1 are legal.
- Priority 0 means "never interrupt."

**PENDING[w]** - RO. Reset: 0.
- Bit N of word w corresponds to source (w×32 + N). Bit 0 of word 0 is hardwired 0.
- Reflects current gateway pending state. Writes are silently ignored (`TLM_OK_RESPONSE`).

**ENABLE[w]** - RW. Reset: 0.
- Bit N of word w enables source (w×32 + N) for context 0. Bit 0 of word 0 is hardwired 0.
- Changes take immediate effect on priority resolution.

**THRESHOLD** - RW, WARL. Reset: 0.
- Bits [P−1:0] writable; upper bits read-as-zero/write-ignored.
- Only sources with priority **strictly greater than** threshold qualify.

**CLAIM/COMPLETE** - Read: returns winning source ID (side-effect). Write: completes interrupt for written source ID. Reset read-value: 0.
- Per §6-§7 of RISC-V PLIC Spec v1.0.0, claim and complete share the same address.

**(Benchmark Extension) INT_TYPE[w]** - RW. Reset: 0.
- Bit N of word w: 0 = level-sensitive, 1 = edge-triggered for source (w×32 + N).
- Bit 0 of word 0 is hardwired 0.
- Changing INT_TYPE triggers re-evaluation of pending state for affected sources:
  - Edge→level while pending, input low: pending clears (gateway → IDLE).
  - Edge→level while pending, input high: pending remains.
  - Level→edge while pending: pending remains (already latched).
  - Any type change while CLAIMED: no effect until after complete.

### 3.3 WARL Semantics

For PRIORITY and THRESHOLD: only bits [P−1:0] are writable. Writes store the lower P bits; reads return zero in bits [31:P]. No illegal values exist in the legal range.

### 3.4 Access Errors

| Condition | Response |
|-----------|----------|
| Offset beyond valid register regions | `TLM_ADDRESS_ERROR_RESPONSE` |
| Offset not 4-byte aligned | `TLM_ADDRESS_ERROR_RESPONSE` |
| Data length ≠ 4 bytes | `TLM_GENERIC_ERROR_RESPONSE` |
| `TLM_IGNORE_COMMAND` | `TLM_GENERIC_ERROR_RESPONSE` |
| Any other unrecognized TLM command | `TLM_GENERIC_ERROR_RESPONSE` |

Error conditions are evaluated in table order; first match determines the response.

**(Benchmark Extension)** The split between `TLM_ADDRESS_ERROR_RESPONSE` (address/alignment issues) and `TLM_GENERIC_ERROR_RESPONSE` (protocol violation) is a benchmark-specific requirement for testability.

### 3.5 Valid Address Regions

The following offset ranges are valid (accesses outside these return `TLM_ADDRESS_ERROR_RESPONSE`):
- Priority block: valid offsets `0x000004`, `0x000008`, …, `S×4` (source IDs 1 through S). Offset `0x000000` is reserved (source 0 does not exist); reads return 0 and writes are ignored, but the address is within the accepted region.
- `0x001000` - `0x001000 + (⌈(S+1)/32⌉ − 1)×4` (pending block)
- `0x002000` - `0x002000 + (⌈(S+1)/32⌉ − 1)×4` (enable block)
- `0x003000` - `0x003000 + (⌈(S+1)/32⌉ − 1)×4` (INT_TYPE block)
- `0x200000` - `0x200004` (threshold + claim/complete)

---

## 4. Interrupt Gateways

Per §3 of RISC-V PLIC Spec v1.0.0: "The interrupt gateways are responsible for converting global interrupt signals into a common interrupt request format, and for controlling the flow of interrupt requests to the PLIC core."

Each source's gateway has three states:

- **IDLE** - Ready to accept a new interrupt. Pending bit = 0.
- **PENDING** - An interrupt request is active. Pending bit = 1. Source participates in arbitration.
- **CLAIMED** - Software has claimed this interrupt. Pending bit = 0. Source excluded from arbitration. Gateway blocks new requests until completion.

### 4.1 Level-Sensitive Gateway (INT_TYPE bit = 0)

1. **IDLE → PENDING**: `src_irq[N−1]` sampled high while IDLE. Pending bit set.
2. **While PENDING**: Pending bit tracks the input. If input goes low, pending clears and gateway returns to IDLE.
3. **PENDING → CLAIMED**: On claim read, pending bit cleared, gateway enters CLAIMED regardless of input state.
4. **While CLAIMED**: Input ignored. Pending remains 0.
5. **CLAIMED → IDLE**: On complete write. If input is still asserted, gateway immediately transitions to PENDING.

### 4.2 Edge-Triggered Gateway (INT_TYPE bit = 1)

1. **IDLE → PENDING**: Rising edge (0→1) on `src_irq[N−1]` while IDLE. Pending bit set.
2. **While PENDING**: Pending bit latched. Additional edges are lost. Input going low does not clear pending.
3. **PENDING → CLAIMED**: On claim read, pending bit cleared.
4. **While CLAIMED**: Rising edges are ignored (lost).
5. **CLAIMED → IDLE**: On complete write. Gateway is now receptive to new rising edges. Edges that occurred during CLAIMED are not retroactively captured.

> **Note:** Edge detection requires tracking the previous value of each `src_irq` input. An edge is the transition from 0 to 1, not merely a high level.

### 4.3 Gateway and Pending Visibility

The PENDING register always reflects actual gateway state:
- Gateway in PENDING → bit is 1
- Gateway in IDLE or CLAIMED → bit is 0

| Gateway State | Pending Bit | In Arbitration? | Responds to Input? |
|---|---|---|---|
| IDLE | 0 | No | Level: tracks; Edge: detects rising |
| PENDING | 1 | Yes (if enabled, priority > threshold) | Level: tracks; Edge: ignores |
| CLAIMED | 0 | No | No (both types ignore) |

---

## 5. Priority Arbitration

### 5.1 Qualification

A source N qualifies for interrupt delivery iff ALL conditions hold:
1. Gateway is in PENDING state (pending bit = 1).
2. Enable bit for source N is set.
3. PRIORITY[N] > 0.
4. PRIORITY[N] > THRESHOLD (strictly greater).

### 5.2 Winner Selection

Per §6 of RISC-V PLIC Spec v1.0.0:
1. Highest priority value wins.
2. Ties broken by lowest source ID.
3. No qualifying source → winner is ID 0 ("no interrupt").

### 5.3 Dynamic Changes

Priority, enable, and threshold changes take immediate effect on arbitration. A claimed source's priority change has no effect until after completion.

---

## 6. Claim/Complete Protocol

Per §6-§7 of RISC-V PLIC Spec v1.0.0.

### 6.1 Claim (Read of CLAIM/COMPLETE at 0x200004)

Atomically:
1. Determine winning source ID W via priority resolution.
2. If W = 0: return 0, no side effects.
3. If W ≠ 0: transition gateway W from PENDING → CLAIMED, clear pending bit, return W. Re-evaluate EIP.

Claiming when no interrupt is pending returns 0. This is not an error.

After a successful claim, irq_out is immediately re-evaluated. If other sources remain qualified (pending, enabled, priority > threshold), irq_out stays asserted. The claim only affects the claimed source's gateway state (→ CLAIMED).

### 6.2 Complete (Write to CLAIM/COMPLETE at 0x200004)

Write value V:
1. V = 0 or V > S: silently ignored.
2. 1 ≤ V ≤ S, gateway in CLAIMED: transition CLAIMED → IDLE. For level-sensitive: if input still asserted, immediately re-pend. Re-evaluate EIP.
3. 1 ≤ V ≤ S, gateway NOT in CLAIMED: silently ignored.

All cases return `TLM_OK_RESPONSE`.

### 6.3 Multiple Outstanding Claims

Multiple sources may be simultaneously in CLAIMED state. Each requires its own complete. Subsequent claims return the next highest-priority qualifying source.

> **Note:** Per the RISC-V PLIC spec, "The PLIC does not check whether the completion ID is the same as the last claim ID for that target." Completing an unclaimed or already-completed source is silently ignored.

---

## 7. Interrupt Notification (EIP)

### 7.1 Semantics

The `irq_out` output is the EIP signal for context 0:
- Level-sensitive, active-high.
- Asserted when at least one source qualifies (§5.1).
- Continuously reflects current arbitration state.

### 7.2 Re-evaluation Triggers

`irq_out` must be re-evaluated after:
1. Any `src_irq` input change.
2. Write to any PRIORITY register.
3. Write to any ENABLE register.
4. Write to THRESHOLD.
5. Claim read returning non-zero.
6. Complete write for a valid claimed source.
7. Write to INT_TYPE.

In the TLM b_transport model, re-evaluation is instantaneous. `irq_out` reflects post-transaction state when b_transport returns.

After any b_transport that modifies priority, enable, threshold, or performs a claim/complete, the handler must directly update `irq_out` before returning. The SC_METHOD handles only src_irq-driven re-evaluation; b_transport-driven state changes require explicit irq_out update within the b_transport call.

---

## 8. TLM-2.0 Interface

### 8.1 Transport

- Blocking transport (`b_transport`) with zero delay.
- All side effects complete before b_transport returns.
- `sc_time& delay` set to `SC_ZERO_TIME` on return.

### 8.2 Response Codes

| Scenario | Response |
|----------|----------|
| Valid read/write | `TLM_OK_RESPONSE` |
| Write to RO register (PENDING) | `TLM_OK_RESPONSE` (ignored) |
| Address out of range | `TLM_ADDRESS_ERROR_RESPONSE` |
| Misaligned (not 4-byte aligned) | `TLM_ADDRESS_ERROR_RESPONSE` |
| Wrong length (≠ 4 bytes) | `TLM_GENERIC_ERROR_RESPONSE` |
| `TLM_IGNORE_COMMAND` or unknown command | `TLM_GENERIC_ERROR_RESPONSE` |

### 8.3 Data Format

Little-endian byte order. Data pointer must reference at least 4 bytes.

---

## 9. Module Interface

### 9.1 Ports and Sockets

| Port / Socket | Type | Direction | Count | Description |
|---|---|---|---|---|
| `socket` | `tlm_utils::simple_target_socket<plic>` | Target | 1 | Memory-mapped register access |
| `src_irq` | `sc_core::sc_vector<sc_core::sc_in<bool>>` | Input | S | External interrupt sources (active-high). Index i → source ID i+1. |
| `irq_out` | `sc_core::sc_out<bool>` | Output | 1 | EIP to hart context (active-high, level) |

### 9.2 Sensitivity

The module must be sensitive to all `src_irq` input changes to re-evaluate pending state and `irq_out`.

---

## 10. Reset State

On construction, all state initializes to:

| State | Reset Value |
|---|---|
| PRIORITY registers | 0 |
| PENDING bits | 0 |
| ENABLE bits | 0 |
| THRESHOLD | 0 |
| INT_TYPE | 0 (all level-sensitive) |
| Gateway states | IDLE |
| Previous src_irq (edge detection) | 0 (low) |
| irq_out | false |

If a `src_irq` input is high at simulation start, the first evaluation detects this as a new assertion (level: sets pending; edge: rising edge from initial-low sets pending).

---

## 11. Boundary Conditions

- Source ID 0: Reserved. Bit 0 of PENDING, ENABLE, INT_TYPE is hardwired 0. CLAIM returning 0 means "no interrupt." COMPLETE with value 0 is silently ignored.
- Source IDs > S: ENABLE/PENDING/INT_TYPE bits beyond S are hardwired 0. COMPLETE with value > S is silently ignored.
- Disabling a source while CLAIMED: claim remains valid, complete still required. After complete, if source re-pends while disabled, it won't qualify.
- Write to PENDING: silently ignored, `TLM_OK_RESPONSE`.

---

## 12. Design Constraints

1. **(Benchmark Extension)** Header-only implementation (`plic.h`), no separate .cpp file.
2. Single context only; no multi-context routing.
3. Only single 32-bit transactions; no burst/DMA.
4. Deterministic: same inputs always produce same outputs.
5. Immediate consistency: outputs reflect new state when b_transport returns.
6. Source ID mapping: `src_irq` index i = source ID i+1.
7. Priority 0 = never interrupt, regardless of enable/threshold/pending.
8. Threshold comparison is strict: priority > threshold (not ≥).
9. C++14 or later. Must `#include` all necessary SystemC/TLM headers.

---

## 13. Test Scenarios

The following scenarios define architecturally critical behaviors:

**Scenario 1: Claim with no pending sources**
All sources idle. Read CLAIM → returns 0, no side effects.

**Scenario 2: Priority 0 blocks delivery**
Source 1 pending, enabled, priority = 0. CLAIM → returns 0. irq_out = 0.

**Scenario 3: Threshold equals priority (strict-greater semantics)**
Source 1 priority = 3, threshold = 3, pending and enabled. CLAIM → returns 0. irq_out = 0.

**Scenario 4: Tie-breaking by lowest ID**
Sources 3 and 5 both pending, enabled, same priority, above threshold. CLAIM → returns 3.

**Scenario 5: Level-sensitive re-assertion after complete**
Source 1 level-sensitive, input high, claimed. COMPLETE → gateway re-pends immediately. irq_out asserts.

**Scenario 6: Edge lost during claimed state**
Source 2 edge-triggered, claimed. Rising edge on src_irq[1] during CLAIMED. COMPLETE → gateway IDLE, no pending. irq_out stays deasserted.

**Scenario 7: Level input deasserted during pending**
Source 1 level-sensitive, pending (input high). Input goes low → pending clears, gateway returns to IDLE.

**Scenario 8: Multiple simultaneous claims**
Claim source 3, then claim source 5. Both CLAIMED. Complete each independently.

**Scenario 9: Priority change while pending**
Source 1 priority 5, threshold 3, pending, enabled (qualifies). Change priority to 2 → disqualifies. irq_out deasserts.

**Scenario 10: Threshold = PMAX blocks all**
Set THRESHOLD = 2^P−1. No source can satisfy strict-greater. irq_out = 0.

---

## 14. File Deliverables

| File | Module Class | Description |
|------|-------------|-------------|
| `plic.h` | `plic` | Header-only PLIC implementation. Self-contained. |

> **Interface Contract**: The mandatory interface is defined in `interface/plic.h`. LLM submissions must conform to this exact class name, socket names (`socket`, `src_irq`, `irq_out`), and constructor signature `plic(name, num_sources)`.
