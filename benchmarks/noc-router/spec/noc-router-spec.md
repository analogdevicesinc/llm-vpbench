<!-- SPDX-License-Identifier: Apache-2.0 -->
# NoC Crossbar Router - SystemC TLM-2.0 Specification

| Field     | Value                                                         |
|-----------|---------------------------------------------------------------|
| Benchmark | LLM-VPBench                                                   |
| Tier      | Medium                                                        |
| Reference | PULP axi_xbar (address decode model), Dally & Towles (arbitration), IEEE 1666-2023 TLM-2.0 LRM |

---

## 1. Purpose

A 4×4 fully-connected crossbar routing transactions from initiator ports to target ports based on a software-programmed address map. The design exercises configurable arbitration, a route-lookup cache, and a direct-mapped data cache with per-byte validity - the last being the primary verification challenge.

### Non-Goals

- Approximately-timed (AT) or non-blocking transport. This IP uses `b_transport` exclusively.
- QoS priority levels or weighted round-robin arbitration.
- Cache flush commands or coherence protocols.
- Pipeline stages or cycle-accurate timing modeling.
- Multi-beat burst decomposition - transactions are atomic.

### Architectural Notes

The crossbar topology (fully connected, no internal buffering beyond the data cache) follows the PULP axi_xbar model: address-decode at the input, per-output arbitration, single-cycle grant in the absence of contention. The data cache uses per-byte validity tracking to handle partial-line fills correctly. The route cache tests associative lookup and LRU eviction logic.

---

## 2. Module Interface

### 2.1 TLM-2.0 Sockets

| Name          | Type (TLM-2.0)                                      | Count  | Purpose                        |
|---------------|-----------------------------------------------------|--------|--------------------------------|
| `init_socket` | `simple_target_socket_tagged<noc_router>`           | N_INIT=4 | Upstream initiator connections |
| `tgt_socket`  | `simple_initiator_socket_tagged<noc_router>`        | N_TGT=4 | Downstream target connections  |
| `cfg_socket`  | `simple_target_socket<noc_router>`                  | 1      | Register access                |

All data-path sockets are **tagged**; the tag equals the port index and identifies the originating initiator or responding target in callbacks.

### 2.2 Ports

| Name  | Type           | Description                            |
|-------|----------------|----------------------------------------|
| `irq` | `sc_out<bool>` | Level-sensitive, active-high interrupt |

### 2.3 Constructor Parameters

```
noc_router(sc_module_name name,
           unsigned n_init = 4,
           unsigned n_tgt  = 4,
           sc_time  tick   = sc_time(1, SC_US));
```

`tick` sets the granularity for timeout monitoring.

### 2.4 Transport Interface Contract

Only `b_transport` is registered on data-path sockets. The router does not implement `nb_transport_fw`, `nb_transport_bw`, `get_direct_mem_ptr`, or `transport_dbg`. Initiators must not call these methods. The configuration socket likewise supports only `b_transport`.

Transactions may carry arbitrary `data_length` on data-path sockets (the router does not enforce a bus width). Configuration transactions must be exactly 4 bytes.

---

## 3. Register Access Policy

All registers are 32-bit, 4-byte-aligned, little-endian. The configuration address space is `0x000`-`0x3FF`.

**General rules (stated once, apply everywhere):**

- **WARL fields:** Writes outside the legal range are silently clamped or masked. The stored value is always legal. Reads return the stored legal value.
- **Read-only registers:** Writes are ignored; response is `TLM_OK_RESPONSE`.
- **W1C fields:** Writing 1 clears the corresponding bit; writing 0 has no effect.
- **Reserved bits** in any register: writes ignored, reads zero.
- **Reserved address gaps:** Return `TLM_ADDRESS_ERROR_RESPONSE`.

**Error priority (first match wins):**

| Condition                          | Response                       |
|------------------------------------|--------------------------------|
| Null data pointer                  | `TLM_GENERIC_ERROR_RESPONSE`   |
| Data length ≠ 4                    | `TLM_GENERIC_ERROR_RESPONSE`   |
| Misaligned address                 | `TLM_ADDRESS_ERROR_RESPONSE`   |
| Address outside `0x000`-`0x3FF`    | `TLM_ADDRESS_ERROR_RESPONSE`   |
| Address in reserved gap            | `TLM_ADDRESS_ERROR_RESPONSE`   |
| Command not READ or WRITE          | `TLM_GENERIC_ERROR_RESPONSE`   |

Error conditions are evaluated in listed order; first match determines the response.

All configuration accesses complete in zero simulation time.

---

## 4. Register Map

### 4.1 Global Control (0x000-0x01F)

| Offset | Name        | Access | Reset       | Description              |
|--------|-------------|--------|-------------|--------------------------|
| 0x000  | GLOBAL_CTRL | WARL   | 0x0000_0001 | Feature enables          |
| 0x004  | ARB_MODE    | WARL   | 0x0000_0000 | Arbitration policy       |
| 0x008  | TIMEOUT_CFG | WARL   | 0x0000_FFFF | Timeout threshold (ticks)|
| 0x00C  | STATUS      | R/W1C  | 0x0000_0000 | Event status flags       |
| 0x010  | IRQ_EN      | WARL   | 0x0000_0000 | Interrupt enables        |
| 0x014  | VERSION     | RO     | 0x0003_0000 | Version 3.0              |
| 0x018  | N_INIT_RO   | RO     | N_INIT      | Initiator port count     |
| 0x01C  | N_TGT_RO    | RO     | N_TGT       | Target port count        |

**GLOBAL_CTRL bit fields:**

| Bits   | Name           | Reset | Meaning when set                         |
|--------|----------------|-------|------------------------------------------|
| [0]    | router_en      | 1     | Data path active                         |
| [1]    | arb_en         | 0     | Arbitration active (else first-come)     |
| [2]    | route_cache_en | 0     | Route cache active                       |
| [3]    | data_cache_en  | 0     | Data cache active                        |
| [4]    | stats_en       | 0     | Statistics counters increment            |
| [5]    | timeout_en     | 0     | Timeout monitoring active                |
| [31:6] | -              | 0     | Reserved                                 |

Side effects: Clearing `route_cache_en` (1→0) flushes the route cache. Clearing `data_cache_en` (1→0) flushes the data cache.

**ARB_MODE[1:0]:** 0 = round-robin, 1 = fixed-priority. Values ≥ 2 clamp to 0.

**TIMEOUT_CFG[15:0]:** Threshold in `tick` periods. 0xFFFF = maximum window.

**STATUS:**

| Bits  | Name             | Type | Description                               |
|-------|------------------|------|-------------------------------------------|
| [0]   | timeout_flag     | W1C  | HW sets on timeout                        |
| [1]   | decode_err_flag  | W1C  | HW sets on address decode failure         |
| [2]   | cache_evict_flag | W1C  | HW sets when valid data-cache line evicted|
| [3]   | -                | RO   | Reserved, reads 0                         |
| [7:4] | last_err_init_id | RO   | Port index of last decode error           |

**IRQ_EN[2:0]:** Per-source interrupt enable, matching STATUS[2:0].

### 4.2 Address Map Rules (0x100-0x1FF)

Up to 16 routing rules. Rule `i` occupies offset `0x100 + i×0x10`:

| +Offset | Name           | Access | Reset | Description               |
|---------|----------------|--------|-------|---------------------------|
| +0x00   | ROUTE_START[i] | RW     | 0     | Start address (inclusive) |
| +0x04   | ROUTE_END[i]   | RW     | 0     | End address (exclusive)   |
| +0x08   | ROUTE_TGT[i]   | WARL   | 0     | Target port (clamped to N_TGT−1) |
| +0x0C   | ROUTE_CTRL[i]  | WARL   | 0     | [0]=valid, [1]=cacheable  |

**Decode order:** Rules are evaluated from index 15 down to 0; first valid match wins. Higher index = higher priority - mirroring PULP axi_xbar's rule-priority model. No match → decode error.

Route rules with `target_port >= N_TGT` are treated as invalid (ROUTE_CTRL valid bit ignored). Writing a target_port value >= N_TGT is clamped to N_TGT-1 on write (WARL).

**Coherence:** Any write to any route register invalidates the entire route cache.

### 4.3 Statistics Counters (0x200-0x21F)

| Offset | Name            | Access | Description                  |
|--------|-----------------|--------|------------------------------|
| 0x200  | STAT_TOTAL_TXN  | RO     | Total routed transactions    |
| 0x204  | STAT_ROUTE_HIT  | RO     | Route cache hits             |
| 0x208  | STAT_ROUTE_MISS | RO     | Route cache misses           |
| 0x20C  | STAT_DATA_HIT   | RO     | Data cache read hits         |
| 0x210  | STAT_DATA_MISS  | RO     | Data cache read misses       |
| 0x214  | STAT_DECODE_ERR | RO     | Decode errors                |
| 0x218  | STAT_TIMEOUT    | RO     | Timeout events               |
| 0x21C  | STAT_CTRL       | WO     | Write 1 → clear all counters |

All counters saturate at 0xFFFF_FFFF. Counters increment only when `stats_en=1`. Reading STAT_CTRL returns 0. Writing the 32-bit value 0x00000001 clears all counters. Any other value is ignored.

### 4.4 Cache Info (0x300-0x310)

| Offset | Name               | Access | Reset | Description                  |
|--------|--------------------|--------|-------|------------------------------|
| 0x300  | ROUTE_CACHE_ENTRIES| RO     | 8     | Route cache capacity         |
| 0x304  | DATA_CACHE_LINES   | RO     | 16    | Data cache line count        |
| 0x308  | DATA_CACHE_LINE_SZ | RO     | 32    | Bytes per line               |
| 0x30C  | DATA_CACHE_VALID   | RO     | 0     | Current valid-line count [7:0]|

---

## 5. Data Path (Blocking Transport)

All data-path transactions use `b_transport`. The router executes the following logical steps for each transaction on `init_socket[init_id]`:

1. **Validate** - Null pointer or `router_en=0` → `TLM_GENERIC_ERROR_RESPONSE`, return. Error checks (null pointer, zero data_length) are evaluated before `router_en` check. A disabled router still rejects malformed transactions.
2. **Address decode** - Consult route cache (if enabled) then fall back to rule walk. On miss, record in cache. No match → set `decode_err_flag`, update `last_err_init_id`, return `TLM_ADDRESS_ERROR_RESPONSE`.
3. **Arbitrate** - If `arb_en=1`, invoke the arbiter to advance internal state. In blocking mode only one initiator is active per call, so the grant is immediate - the purpose is to maintain round-robin pointer fairness.
4. **Data cache (reads)** - If `data_cache_en=1` and the route is cacheable and the command is READ: check cache. Hit → return cached data, increment `STAT_DATA_HIT` and `STAT_TOTAL_TXN`, done. Miss → increment `STAT_DATA_MISS`, continue.
5. **Data cache (writes)** - If the addressed line is resident, update it in place (write-through). No allocation on write miss.
6. **Forward** - `b_transport` to the resolved `tgt_socket[port]`.
7. **Post-forward** - Increment `STAT_TOTAL_TXN`. For successful reads to cacheable regions: allocate line in data cache. If evicting a valid line, set `cache_evict_flag`.

Error responses from the downstream target are forwarded unchanged to the initiator.

### 5.1 Decode Error Detail

On decode failure:
1. Set `STATUS[1]` (decode_err_flag).
2. Write `init_id` (clamped to 4 bits) into `STATUS[7:4]` (last_err_init_id).
3. Increment `STAT_DECODE_ERR` (if `stats_en=1`).
4. Set transaction response to `TLM_ADDRESS_ERROR_RESPONSE`.
5. Re-evaluate IRQ.
6. Return immediately - no forwarding occurs.

### 5.2 Statistics Accounting

The following table summarizes when each counter increments:

| Counter         | Increments when…                                           |
|-----------------|------------------------------------------------------------|
| STAT_TOTAL_TXN  | A transaction is successfully forwarded to a target and returns |
| STAT_ROUTE_HIT  | Route cache returns a hit                                  |
| STAT_ROUTE_MISS | Route cache misses or is disabled (rule walk performed). When `route_cache_en=0`, every transaction increments this counter (all decodes perform a rule walk). |
| STAT_DATA_HIT   | Data cache hit serves a read without target access         |
| STAT_DATA_MISS  | Data cache miss on a cacheable read                        |
| STAT_DECODE_ERR | No routing rule matches                                    |
| STAT_TIMEOUT    | In-flight transaction exceeds timeout threshold            |

A data-cache hit that serves a read directly still increments `STAT_TOTAL_TXN` - the transaction is considered routed even though it did not reach the downstream target.

---

## 6. Arbitration

Two policies, selected by `ARB_MODE`:

### 6.1 Round-Robin (mode 0)

Per-target pointer, initialized to 0. On each grant decision the scan starts one past the current pointer, wraps, and grants the first requesting initiator. Pointer advances to the winner. Provides long-term fairness per Dally & Towles §16.2.

### 6.2 Fixed Priority (mode 1)

Lowest-indexed requesting initiator wins unconditionally. Deterministic but can starve higher-indexed ports.

In blocking transport the arbiter sees only one requester per invocation; the grant is trivial but the pointer update ensures correct state if traffic patterns change.

In blocking transport, only one thread executes at a time; the arbiter never sees simultaneous requests. Arbitration is exercised when the testbench issues rapid back-to-back transactions from multiple initiator ports in sequence - the round-robin pointer advances on each grant regardless of actual contention. This tests correct state-machine maintenance, not concurrent contention resolution.

---

## 7. Route Cache

A small fully-associative cache accelerating address decode. 8 entries (fixed) with LRU replacement.

**Behavior:**
- Lookup: match when `ROUTE_START ≤ addr < ROUTE_END` in a valid entry. Hit bumps to MRU.
- Fill on miss: store the decoded rule's address range, target port, and cacheable flag. Evict LRU if full.
- Invalidation: entire cache is flushed on any route-register write or when `route_cache_en` transitions 1→0.

The cache is a performance optimization; the testbench verifies functional correctness by comparing hit/miss counters against expected decode patterns.

The route cache stores the rule INDEX (not the decoded result). On a cache hit, the stored rule index is used directly. If a route rule is modified after being cached, the cache entry becomes stale (no coherence). Overlapping rules in the cache cannot occur because only the winning rule's index is stored.

---

## 8. Data Cache

Direct-mapped, write-through, no-write-allocate. 16 lines × 32 bytes. This is the primary verification feature of the benchmark.

### 8.1 Indexing

The cache uses direct mapping with a 32-byte line size:
- **Index** selects one of 16 lines from the address.
- **Tag** identifies which address block occupies the line.
- **Offset** locates bytes within the line.

The exact formulas are:
- **Index** = (address / 32) % 16 - i.e., address bits [8:5]
- **Tag** = address / 512 - i.e., address bits [31:9]
- **Offset** = address % 32 - i.e., address bits [4:0]

### 8.2 Per-Byte Validity

Each cache line tracks validity at byte granularity (32 valid bits per line). A partial read hits only if all requested bytes are valid. Allocation marks only the bytes actually returned by the target as valid. Subsequent allocations to the same line accumulate valid bytes without disturbing previously cached data.

### 8.3 Read Path

1. Compute index and tag.
2. If line is valid, tag matches, and all requested byte positions are marked valid → **hit**. Return cached bytes.
3. Otherwise → **miss**. After target responds successfully, allocate: write returned data into the line at the appropriate offset, set corresponding byte-valid bits, update tag. If the previous tag was different and any byte was valid, this is an eviction → set `cache_evict_flag`.

### 8.4 Write Path

- If line is resident (valid, tag match): update cached data and byte-valid bits at the written offsets.
- Otherwise: no allocation (write-through, no-write-allocate).
- The write is always forwarded to the downstream target.

### 8.5 Flush

All lines invalidated. Triggered when `data_cache_en` transitions 1→0.

### 8.6 Observable State

`DATA_CACHE_VALID` (register 0x30C) reports the number of lines with at least one valid byte.

---

## 9. Timeout Monitoring

When `timeout_en=1`, the router monitors elapsed time for in-flight transactions (those between forward-call and return in blocking mode). A transaction whose duration exceeds `TIMEOUT_CFG × tick` triggers:

- `STATUS[0]` (timeout_flag) set.
- `STAT_TIMEOUT` incremented.
- IRQ re-evaluated.

Implementation note: in pure blocking mode, timeout only fires if the downstream `b_transport` call blocks for an extended `sc_time` delay. The mechanism exists to catch unresponsive targets.

Timeout is measured by the `delay` annotation returned by the downstream target. If `delay` exceeds `TIMEOUT_CFG × tick` after b_transport returns, the STATUS timeout bit is set and an interrupt is generated. This is a post-hoc check, not a preemption mechanism.

---

## 10. Interrupt Logic

```
irq = (STATUS[0] & IRQ_EN[0]) | (STATUS[1] & IRQ_EN[1]) | (STATUS[2] & IRQ_EN[2])
```

Re-evaluated after any hardware event that modifies STATUS, and after any software write to STATUS, IRQ_EN, or GLOBAL_CTRL.

---

## 11. Reset State

All state is established at construction:

- `GLOBAL_CTRL = 0x01` (router enabled, all features off).
- All route rules invalid (ROUTE_CTRL = 0).
- All counters zero. Caches empty. Arbiter pointers at 0.
- `irq` deasserted.

No runtime reset mechanism is specified; the testbench may reconstruct the module.

---

## 12. File Deliverables

| File           | Contents                                      |
|----------------|-----------------------------------------------|
| noc_router.h   | Top-level `SC_MODULE`, sockets, data path     |
| noc_arbiter.h  | Arbiter (round-robin + fixed-priority)        |
| noc_cache.h    | Route cache and data cache classes            |

All files are header-only. `noc_router.h` includes the other two.

> **Interface Contract**: The mandatory interface is defined in `interface/noc_router.h`. LLM submissions must conform to this exact class name, socket names, port names, and constructor signature.

---

## 13. Verification Guidance (Informative)

The testbench exercises:

1. **Address decode correctness** - overlapping rules with different priorities, decode errors for unmapped regions, ROUTE_TGT clamping.
2. **Data cache byte-validity** - partial writes followed by partial reads that must miss until the relevant bytes are cached; eviction flag accuracy when a different address maps to the same index.
3. **Route cache behavior** - verify hit/miss counter increments match expected patterns after repeated accesses; confirm flush on route-register write.
4. **Arbitration state advance** - round-robin pointer fairness observable via interleaved transactions from different initiator ports.
5. **Interrupt assertion/deassertion** - W1C clearing drops IRQ; enable masking prevents assertion; multiple simultaneous sources.
6. **Configuration error handling** - misaligned access, reserved offsets, null data pointer, wrong data length, unknown command.
7. **Feature gating** - each GLOBAL_CTRL bit independently enables/disables its subsystem; disabled subsystems have no side effects on counters or flags.

### Key Correctness Properties

- A read to address A returns data-cache content only if `data_cache_en=1`, the route is marked cacheable, and all bytes in the requested range are valid in the cache. Any partially-valid overlap must result in a miss.
- Statistics counters are monotonically non-decreasing (saturation, no wrap). They freeze at their current value when `stats_en` is cleared.
- The IRQ output is purely combinational with respect to STATUS and IRQ_EN - it reflects the current state without latching.
