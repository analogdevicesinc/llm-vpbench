<!-- SPDX-License-Identifier: Apache-2.0 -->
# AMBA AXI4 Interconnect (2×2) - SystemC TLM-2.0 Specification

| Field     | Value                                            |
|-----------|--------------------------------------------------|
| Benchmark | LLM-VPBench                                      |
| Tier      | Simple                                           |
| Reference | ARM IHI 0022 (AMBA AXI4); Accellera TLM-2.0 LRM; PULP axi_xbar |

**Brief:** A configurable N-master, M-slave TLM-2.0 interconnect (default 2×2) that routes transactions based on a configurable address map, provides per-slave arbitration via `sc_mutex`, translates addresses to slave-local offsets, supports DMI passthrough with broadcast invalidation, and debug transport. Header-only `b_transport` (LT mode), no clock, no internal processes. Single `.h` file deliverable; no `nb_transport_fw`/`nb_transport_bw`.

---

## 1. Overview

### 1.1 Purpose

The AXI4 Interconnect is a pure routing fabric that connects multiple TLM-2.0 initiators (masters) to multiple TLM-2.0 targets (slaves) using address-based decode logic. It models the routing and arbitration behavior of an AXI4 interconnect at the transaction level, abstracting away signal-level details (VALID/READY handshakes, channels) into TLM-2.0 blocking transport calls.

### 1.2 Topology

Default configuration: 2 masters × 2 slaves (full crossbar connectivity). Any master can reach any slave. No partial connectivity masks.

### 1.3 Operating Mode

- **TLM-2.0 Loosely-Timed (LT) mode** using `b_transport`.
- Each master operates in its own `SC_THREAD`; the interconnect is entered concurrently by multiple threads.
- No temporal decoupling quantum management - delay passthrough only.
- No internal buffering, FIFOs, or pipelining.
- Purely callback-driven: no `SC_THREAD`, `SC_METHOD`, or sensitivity lists registered by the interconnect.

### 1.4 Relationship to AXI4

This interconnect models the following AXI4 behaviors at TLM abstraction:
- Address decode and routing (AXI4 decoder)
- Slave-port arbitration (AXI4 arbiter)
- Address translation (AXI4 address remap)
- Error response for unmapped regions (AXI4 default slave)

It does NOT model: burst splitting, narrow transfers, exclusive access (AXLOCK), cache maintenance, barriers, QoS, or user signals.

---

## 2. Address Map & Decode

### 2.1 Region Table

The address map is defined as an ordered vector of regions:

```
struct address_region {
    uint64_t base_addr;   // Inclusive start address (must be aligned to size)
    uint64_t size;        // Region size in bytes (must be power of 2)
    unsigned slave_index; // Target slave port index
};
```

**Default configuration (2×2, 256MB per slave):**

| Slave | Base Address   | Size        | End Address (inclusive) | Slave Index |
|-------|---------------|-------------|------------------------|-------------|
| 0     | 0x0000_0000   | 0x1000_0000 (256MB) | 0x0FFF_FFFF | 0 |
| 1     | 0x1000_0000   | 0x1000_0000 (256MB) | 0x1FFF_FFFF | 1 |

### 2.2 Address Decode Algorithm

For a transaction with address `A`:

1. Iterate through the region table in order.
2. For each region `R`: if `R.base_addr <= A < R.base_addr + R.size`, the target is `R.slave_index`.
3. First match wins (overlaps are prohibited - see §2.5).
4. If no region matches → unmapped (error).

For the default configuration, this simplifies to: bit [28] selects the slave (0 or 1). Addresses ≥ 0x2000_0000 are unmapped.

> **Testbench Address Map (Issue #38):** The testbench exercises the default address map exactly as defined in §2.1. Slave 0 owns `[0x00000000, 0x10000000)` and slave 1 owns `[0x10000000, 0x20000000)`. Addresses at or above `0x20000000` (e.g., `0x20000000`, `0x30000000`, `0xFFFFFFFF`) are unmapped and must return `TLM_ADDRESS_ERROR_RESPONSE`. No custom address map is configured by the testbench; the default 256MB-per-slave split is used throughout.

### 2.3 Address Translation Formula

```
slave_local_address = global_address - region.base_addr
```

For the default configuration: `slave_local_address = global_address & 0x0FFF_FFFF`

**Examples:**
- Global 0x0000_1000 → Slave 0, local 0x0000_1000
- Global 0x1000_0000 → Slave 1, local 0x0000_0000
- Global 0x1234_5678 → Slave 1, local 0x0234_5678
- Global 0x2000_0000 → UNMAPPED (error)

### 2.4 64-bit Address Support

All address comparisons use `uint64_t` / `sc_dt::uint64`. The interconnect is transparent to 32-bit vs. 64-bit addresses.

### 2.5 Overlapping Region Detection

At construction time, the interconnect validates that no two regions overlap. For all region pairs (i, j) where i ≠ j:
```
assert( R[i].base_addr + R[i].size <= R[j].base_addr ||
        R[j].base_addr + R[j].size <= R[i].base_addr );
```
Violation triggers `SC_REPORT_FATAL`.

### 2.6 Default Slave Behavior

Addresses that do not fall within any configured region are handled inline (not as a separate module):
- Sets `response_status = TLM_ADDRESS_ERROR_RESPONSE` on the transaction.
- Does NOT forward the transaction to any physical slave.
- Returns immediately (zero additional delay).

---

## 3. Transaction Routing

### 3.1 Canonical Routing Pattern

All three transport paths (`b_transport`, `get_direct_mem_ptr`, `transport_dbg`) share the same save/decode/translate/forward/restore pattern:

```
1. Save:      orig_addr = trans.get_address()
2. Decode:    slave_id = lookup(orig_addr)  - return error/false/0 if unmapped
3. Translate: trans.set_address(orig_addr - region[slave_id].base_addr)
4. Forward:   call downstream socket method on init_socket[slave_id]
5. Restore:   trans.set_address(orig_addr)
6. Return:    propagate result to master
```

> **Critical:** Address restoration (step 5) MUST occur on all paths - including error returns from the slave. Without it the master sees a modified address on its transaction object, violating TLM-2.0 interconnect rules (Accellera TLM-2.0 LRM §11.1.2).

### 3.2 b_transport Routing Algorithm

When master `M` calls `b_transport(trans, delay)` on the interconnect's target socket:

```
1. Save original address: orig_addr = trans.get_address()
2. Decode target slave from orig_addr → slave_id
3. If unmapped:
   a. trans.set_response_status(TLM_ADDRESS_ERROR_RESPONSE)
   b. Return (do not modify delay)
4. Translate address: trans.set_address(orig_addr - region[slave_id].base_addr)
5. Acquire per-slave mutex[slave_id] (blocking if contended)
6. Update round-robin tracker: last_served[slave_id] = M
7. Forward: init_socket[slave_id]->b_transport(trans, delay)
8. Release per-slave mutex[slave_id]
9. Restore address: trans.set_address(orig_addr)
10. Return (response_status set by slave propagates to master)
```

### 3.3 Passthrough Semantics

The interconnect is a zero-copy, zero-allocation, zero-delay forwarder:

- **Transaction object:** NOT allocated, copied, or pooled. Same `tlm_generic_payload*` forwarded. No `acquire()`/`release()` calls.
- **Data pointer:** `data_ptr` passed through; interconnect NEVER dereferences it. Slave reads/writes directly to master's buffer.
- **Byte enables:** `byte_enable_ptr` and `byte_enable_length` forwarded unmodified, unvalidated.
- **Streaming width:** Forwarded unmodified; streaming semantics are between master and slave.
- **Commands:** All (`TLM_READ_COMMAND`, `TLM_WRITE_COMMAND`, `TLM_IGNORE_COMMAND`) forwarded without inspection. `TLM_IGNORE_COMMAND` is forwarded to the decoded slave without modification.
- **Extensions:** Remain attached; same object means automatic visibility to slave.
- **Response:** Slave-set `response_status` propagated unchanged to master.
- **Delay:** `sc_time& delay` passed by reference. Interconnect adds **zero** latency. Slave modifications visible to master.
- **Lifetime:** After `b_transport` returns, the interconnect holds no reference to the transaction.

---

## 4. Arbitration

### 4.1 Per-Slave Arbitration

Each slave port has **independent** arbitration. Contention on Slave 0 does not affect access to Slave 1.

### 4.2 sc_mutex-Based Serialization

Each slave port is protected by an `sc_core::sc_mutex`:
```
sc_core::sc_mutex slave_mutex[NUM_SLAVES];
```

- `lock()` is called before forwarding to the slave.
- `unlock()` is called after `b_transport` returns from the slave.
- If the mutex is already held, the calling SC_THREAD yields (blocks) until it is released.

### 4.3 Round-Robin Fairness

Per-slave state:
```
int last_served[NUM_SLAVES];  // Initialized to (NUM_MASTERS - 1)
```

Initialization to `NUM_MASTERS - 1` means Master 0 has priority on first contention.

**Note:** In SystemC's cooperative threading model with `sc_mutex`, the order in which waiting threads acquire the mutex after `unlock()` is implementation-defined (typically FIFO based on wait order). The `last_served` tracker is updated on each access but the actual scheduling depends on `sc_mutex` semantics. True round-robin requires checking `last_served` when multiple threads are runnable, which `sc_mutex` alone cannot enforce. In practice, the `last_served` variable documents intent; the `sc_mutex` FIFO behavior provides fairness in most simulators.

**Portability note:** IEEE 1666 does not mandate FIFO ordering for sc_mutex acquisition. This benchmark targets the Accellera reference SystemC implementation (SystemC 2.3.4+) where sc_mutex uses FIFO ordering. Results may differ on proprietary simulators.

### 4.4 Priority Scheme

All masters have equal priority. Arbitration uses round-robin with `last_served[]` tracking only.

### 4.5 Starvation Prevention

- With equal priorities and `sc_mutex` FIFO ordering, starvation cannot occur.
- With unequal priorities, starvation of low-priority masters is possible (matches AXI4 QoS behavior).

### 4.6 Deadlock Freedom Analysis

The design is **deadlock-free** because:
1. Each master thread acquires at most ONE mutex at a time (the mutex for its target slave).
2. There is no circular dependency: Master → acquires slave_mutex[X] → calls b_transport on slave → returns → releases mutex.
3. A master never holds slave_mutex[X] while trying to acquire slave_mutex[Y].
4. Slaves do not call back into the interconnect's b_transport during their b_transport handling.

If a slave were to re-enter the interconnect (e.g., a slave that is also a master, creating a bridge), deadlock could occur. This topology is outside the scope of this spec.

### 4.7 Contention Scenarios

**Scenario A: Both masters target the same slave (contention)**
- First to arrive acquires `slave_mutex[0]`, second blocks.
- Effective serialization: one transaction at a time per slave.

**Scenario B: Masters target different slaves (no contention)**
- Both proceed in parallel (different mutexes). Full crossbar bandwidth.

**Scenario C: Alternating contention**
- `last_served` alternates between 0 and 1. Fair interleaving.

---

## 5. DMI (Direct Memory Interface)

### 5.1 Forward Path: get_direct_mem_ptr

```
bool get_direct_mem_ptr(int id, tlm::tlm_generic_payload& trans,
                        tlm::tlm_dmi& dmi_data)
```

Follows the canonical routing pattern (§3.1) with one addition - on success, translate the DMI range back to global:
```
dmi_data.set_start_address(dmi_data.get_start_address() + region[slave_id].base_addr)
dmi_data.set_end_address(dmi_data.get_end_address() + region[slave_id].base_addr)
```

If the transaction address does not match any configured region, `get_direct_mem_ptr` returns `false` without modifying `dmi_data`.

If the slave returns `false`, the interconnect returns `false` with no DMI descriptor translation.

### 5.2 DMI Caching

The interconnect does not cache DMI descriptors. Each `get_direct_mem_ptr` call is forwarded fresh. Masters are responsible for caching DMI pointers.

### 5.3 Backward Path: invalidate_direct_mem_ptr

Called by slave through initiator socket's backward interface:

```
void invalidate_direct_mem_ptr(int id, sc_dt::uint64 start, sc_dt::uint64 end)
```

Algorithm:
1. Translate to global: `global_start = start + region_for_slave[id].base_addr`, same for end.
2. Broadcast to ALL masters: `master_socket[m]->invalidate_direct_mem_ptr(global_start, global_end)` for each `m`.

**Note:** Invalidation is broadcast to ALL masters, not just the one that originally requested DMI. Any master might have cached a DMI pointer to the invalidated region.

### 5.4 Partial Invalidation

If a slave invalidates a sub-range, the interconnect translates and forwards only that sub-range. Masters must invalidate any cached DMI pointer that overlaps the range.

### 5.5 Invalidation During Active DMI Access

If a slave invalidates a region while a master is actively using a DMI pointer, behavior is undefined (race condition). The interconnect's responsibility is limited to forwarding the invalidation.

### 5.6 DMI and Arbitration Interaction

DMI `get_direct_mem_ptr` does NOT acquire the per-slave mutex. Rationale:
- DMI negotiation is a setup operation, not a data transfer.
- The actual DMI access (direct pointer dereference) bypasses the interconnect entirely.
- Acquiring the mutex would unnecessarily serialize DMI setup with ongoing `b_transport` calls.

---

## 6. Debug Transport

### 6.1 transport_dbg

```
unsigned int transport_dbg(int id, tlm::tlm_generic_payload& trans)
```

Follows the canonical routing pattern (§3.1). Returns 0 for unmapped addresses, otherwise the byte count from the slave.

Debug transport follows the same address decode and translate steps as b_transport. The slave receives the translated (local) address. No mutex is acquired.

### 6.2 Key Differences from b_transport

- **No arbitration:** Does NOT acquire any mutex. Must not block (intended for debugger access).
- **No timing:** No `sc_time` delay parameter.
- **No side effects:** Debug transport should not affect functional simulation state (slave's responsibility).

---

## 7. Error Handling

### 7.1 Error Conditions

| Condition | Behavior | Response Status |
|-----------|----------|-----------------|
| Address unmapped (no region match) | No forwarding, immediate return | `TLM_ADDRESS_ERROR_RESPONSE` |
| Slave returns error | Propagate unchanged | Whatever slave set |
| Transaction spans two regions | Forwarded to first matching region (no split) | Slave determines validity |

### 7.2 Interconnect-Generated Errors

The interconnect generates ONLY `TLM_ADDRESS_ERROR_RESPONSE`, and ONLY for unmapped addresses. It never generates `TLM_GENERIC_ERROR_RESPONSE`, `TLM_BURST_ERROR_RESPONSE`, or `TLM_INCOMPLETE_RESPONSE`. All other payload fields (null `data_ptr`, zero `data_length`, invalid `streaming_width`) are forwarded without validation - the slave determines behavior.

### 7.3 Transaction Spanning Multiple Regions

If a transaction's address falls in Region A but `address + data_length - 1` falls in Region B, the interconnect routes based on the BASE address only. The transaction is forwarded entirely to the slave for Region A.

Per AXI4 (ARM IHI 0022 §A3.4.1), bursts may not cross a 4KB boundary; this constraint is enforced by masters, not by the interconnect.

### 7.4 Timeout Handling

In LT mode with blocking transport, there is no timeout mechanism. If a slave's `b_transport` never returns, the master's thread is permanently blocked. This is a simulation modeling error, not something the interconnect handles.

---

## 8. Ordering & Consistency

**Policy:** Transactions from the same master are strictly ordered (inherent property of blocking transport - N+1 cannot begin until N returns). There is NO ordering guarantee between different masters. If Master 0 writes to address X and Master 1 reads from address X, the result depends on arbitration timing. Cross-master coherence requires external synchronization.

**Byte Order:** All transactions use little-endian byte order. The interconnect is transparent to byte ordering and does not perform any byte-swapping; endianness is a convention between masters and slaves.

---

## 9. Configuration & Parameterization

### 9.1 Constructor Parameters

```cpp
axi4_interconnect(sc_core::sc_module_name name,
                  unsigned int num_masters = 2,
                  unsigned int num_slaves = 2);
```

### 9.2 Default Address Map Generation

General formula for N slaves: `region[i].base = i * 0x10000000`, `region[i].size = 0x10000000` (256MB each, contiguous from 0x0).

### 9.3 Validation at Construction

- Region overlap check (fatal error if overlapping).
- `slave_index` must be < `num_slaves` for all regions (fatal error otherwise).
- Full coverage is NOT required - gaps become unmapped regions handled by the default slave.
- `num_masters` and `num_slaves` must be ≥ 1.

---

## 10. TLM-2.0 Protocol Compliance

### 10.1 Interconnect Role

The interconnect is a **bridge/router** in TLM-2.0 terminology. It:
- Has target sockets (facing masters) and initiator sockets (facing slaves).
- Forwards transactions without creating new ones.
- Performs address translation (permitted per TLM-2.0 LRM §11.1.2).
- Restores original address after forwarding (required for interconnects).

### 10.2 Tagged Sockets

The interconnect uses `tlm_utils::simple_target_socket_tagged` and `tlm_utils::simple_initiator_socket_tagged` to identify which socket a callback originated from.

Callback signatures:
```cpp
void b_transport(int id, tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
bool get_direct_mem_ptr(int id, tlm::tlm_generic_payload& trans, tlm::tlm_dmi& dmi_data);
unsigned int transport_dbg(int id, tlm::tlm_generic_payload& trans);
void invalidate_direct_mem_ptr(int id, sc_dt::uint64 start, sc_dt::uint64 end);
```

The `id` parameter identifies:
- For target socket callbacks: which master initiated (0 or 1)
- For initiator socket backward callbacks: which slave initiated (0 or 1)

### 10.3 Socket Naming & Declaration

```cpp
sc_core::sc_vector<tlm_utils::simple_target_socket_tagged<axi4_interconnect>> master_socket;
sc_core::sc_vector<tlm_utils::simple_initiator_socket_tagged<axi4_interconnect>> slave_socket;
```

Vector element names: `master_socket_0`, `master_socket_1`, `slave_socket_0`, `slave_socket_1`.

### 10.4 Transaction Memory Management

The interconnect does not participate in TLM-2.0 memory management:
- No `acquire()`/`release()` calls.
- No transaction pool.
- Transaction lifetime entirely managed by the master.

---

## 11. Module Interface

### 11.1 Socket Summary

| Port / Socket | Type | Direction | Count | Description |
|---------------|------|-----------|-------|-------------|
| `master_socket` | `sc_core::sc_vector<tlm_utils::simple_target_socket_tagged<axi4_interconnect>>` | Target | NUM_MASTERS (2) | Masters bind here |
| `slave_socket` | `sc_core::sc_vector<tlm_utils::simple_initiator_socket_tagged<axi4_interconnect>>` | Initiator | NUM_SLAVES (2) | Binds to slaves |

### 11.2 Internal State

| State | Type | Count | Purpose |
|-------|------|-------|---------|
| `slave_mutex` | `sc_core::sc_mutex` | NUM_SLAVES | Per-slave arbitration |
| `last_served` | `int` | NUM_SLAVES | Round-robin tracker |

### 11.3 File Deliverables

| File | Module Class | Description |
|------|-------------|-------------|
| `axi4_interconnect.h` | `axi4_interconnect` | Header-only, self-contained interconnect module |

> **Interface Contract**: The mandatory interface is defined in `interface/axi4_interconnect.h`. LLM submissions must conform to this exact class name, socket names (`master_socket[]`, `slave_socket[]`), and constructor signature `axi4_interconnect(name, num_masters, num_slaves)`.

The file must be fully self-contained - no separate .cpp files. All includes at the top. Zero dynamic allocation on the routing path (stack-only address save/restore).

---

## 12. Initialization

### 12.1 Initial State

- `last_served[i] = NUM_MASTERS - 1` for all slaves (Master 0 has priority on first contention).
- All mutexes start unlocked.
- No threads spawned - purely reactive via socket callback registration (`register_b_transport`, `register_get_direct_mem_ptr`, `register_invalidate_direct_mem_ptr`, `register_transport_dbg`).

### 12.2 No Reset Port

The interconnect is a stateless router with no reset port and no clock port. The only mutable state is `sc_mutex` (kernel-managed) and `last_served[]`.

---

## 13. Corner Cases & Boundary Conditions

### 13.1 Address at Exact Region Boundary

- Address `0x0FFF_FFFF` → last byte of Slave 0 region → routes to Slave 0, local addr `0x0FFF_FFFF`.
- Address `0x1000_0000` → first byte of Slave 1 region → routes to Slave 1, local addr `0x0000_0000`.

### 13.2 Transaction Spanning Two Regions

A write to address `0x0FFF_FFF0` with `data_length = 32` spans from Slave 0's region into Slave 1's region. The interconnect routes based on the START address only (Slave 0). The slave receives the full 32-byte transaction at local address `0x0FFF_FFF0`. Whether this succeeds depends on the slave's memory size.

The interconnect does NOT split transactions. Transaction splitting would require creating new transaction objects, which violates the zero-allocation passthrough design.

### 13.3 Simultaneous Access to Same Slave

When both masters target the same slave at the same simulation time:
1. Both SC_THREADs reach `slave_mutex[X].lock()`.
2. One succeeds (determined by SystemC scheduler - typically the first to execute in that delta cycle).
3. The other blocks until the first completes.
4. `last_served` is updated when each transaction completes.

### 13.4 Simultaneous Access to Different Slaves

Master 0 targets Slave 0, Master 1 targets Slave 1: different mutexes, no blocking, full crossbar bandwidth.

### 13.5 DMI Invalidation During b_transport

If Slave 0 calls `invalidate_direct_mem_ptr` while a `b_transport` to Slave 0 is in progress:
- Cannot happen in LT mode within a single thread (slave's b_transport is synchronous).
- CAN happen if another slave (Slave 1) invalidates during Master 0's transaction to Slave 0. The invalidation broadcast proceeds normally - it's a separate callback path.

### 13.6 Unbound Sockets

If any socket is not bound during elaboration, SystemC reports a fatal binding error during `end_of_elaboration()`. The interconnect does not add additional checks.

### 13.7 Single Master Active

Mutex is always uncontended. No performance penalty beyond lock/unlock overhead.

### 13.8 Zero-Length and TLM_IGNORE_COMMAND Transactions

Both are forwarded as-is. The interconnect does not validate payload contents or inspect the command field.
