# AXI4 Interconnect - Functional Coverage Report

| Field       | Value                                          |
|-------------|------------------------------------------------|
| Benchmark   | LLM-VPBench / AXI4 Interconnect (2×2)         |
| Spec        | axi4-interconnect-spec.md                      |
| Testbench   | axi4_interconnect_testbench.cpp                |
| Golden DUT  | golden/axi4_interconnect.h                     |
| Generated   | 2025-07-15                                     |
| Result      | **139 passed, 0 failed - ALL PASS ✓**          |

---

## 1. Overview

The AXI4 Interconnect benchmark verifies a 2-master × 2-slave TLM-2.0 blocking-transport interconnect. The golden implementation was compiled and executed against the full testbench. All 139 assertions across 7 test functions passed with zero failures.

**Files under test:**
- `golden/axi4_interconnect.h` - header-only DUT
- `testbench/axi4_interconnect_testbench.cpp` - self-checking testbench

**Verified counts:**
- Test functions: 7
- Total assertions: 139
- Passed: 139
- Failed: 0

---

## 2. Test Function Summary

| # | Test Function | Description | Assertions | Category |
|---|---------------|-------------|:---:|----------|
| 1 | `test_fn_axi4_01` | Address decode and basic read/write | 42 | Address routing, payload passthrough, commands, ordering, boundaries |
| 2 | `test_fn_axi4_02` | Address translation | 7 | Offset translation, address restore |
| 3 | `test_ec_axi4_01` | Error handling | 5 | Unmapped detection, slave error propagation |
| 4 | `test_fn_axi4_03` | Multi-master arbitration | 23 | Contention, round-robin fairness, starvation prevention |
| 5 | `test_tlm_axi4_01` | DMI forward and backward path | 29 | DMI routing, translation, invalidation broadcast |
| 6 | `test_tlm_axi4_02` | Debug transport | 10 | transport_dbg routing, non-blocking, unmapped |
| 7 | `test_fn_axi4_04` | Coverage gap closure | 23 | Byte enables, DMI caching, extensions, delay, spanning, zero-copy, zero-length |
| | | **TOTAL** | **139** | |

---

## 3. Spec Feature Traceability

| Spec Feature | Spec § | Description | Test Function(s) | Assertion IDs | Status |
|:---:|:---:|---|---|---|:---:|
| SF-01 | §2.1-2.2 | Address decode: route to correct slave | test_fn_axi4_01 | FN-RW-01-06, FN-RW-39-42, FN-RW-04-06, FN-RW-32-33 | ✅ COVERED |
| SF-02 | §2.2 | First-match-wins decode | test_fn_axi4_01 | FN-RW-32, FN-RW-33, FN-RW-31 | ✅ COVERED |
| SF-03 | §2.3 | Address translation: slave_local = global − base | test_fn_axi4_02 | FN-XLAT-01-04 | ✅ COVERED |
| SF-04 | §3.1 | Address restoration after forwarding | test_fn_axi4_02, test_tlm_axi4_01, test_tlm_axi4_02 | FN-XLAT-05-06, TLM-DMI-08-09, TLM-DBG-04, TLM-DBG-07, TLM-DBG-09 | ✅ COVERED |
| SF-05 | §2.6 | Unmapped → TLM_ADDRESS_ERROR_RESPONSE | test_fn_axi4_01, test_ec_axi4_01 | EC-AXI4-01, EC-AXI4-02, EC-AXI4-06, FN-RW-22-25, FN-RW-37-38 | ✅ COVERED |
| SF-06 | §2.4 | 64-bit address support | test_fn_axi4_01 | FN-RW-31 | ✅ COVERED |
| SF-07 | §3.2 | b_transport routing: decode→translate→forward→restore | test_fn_axi4_01 | FN-RW-01-42 | ✅ COVERED |
| SF-08 | §3.3 | Passthrough: command, data_ptr, data_length, streaming_width, byte enables | test_fn_axi4_01 | FN-RW-07-18, FN-RW-28 | ✅ COVERED |
| SF-09 | §3.3 | Zero latency added by interconnect | test_fn_axi4_01, test_fn_axi4_04 | FN-RW-26, FN-RW-54, FN-RW-55 | ✅ COVERED |
| SF-10 | §3.3 | Response status propagation from slave | test_fn_axi4_01, test_ec_axi4_01, test_fn_axi4_04 | EC-AXI4-03, EC-AXI4-05, FN-RW-34-35, FN-RW-51-52 | ✅ COVERED |
| SF-11 | §4.1-4.2 | Per-slave arbitration via sc_mutex | test_fn_axi4_03 | FN-ARB-01-11 | ✅ COVERED |
| SF-12 | §4.3 | Round-robin fairness / last_served tracking | test_fn_axi4_03 | FN-ARB-12-17 | ✅ COVERED |
| SF-13 | §4.1 | Independent per-slave arbitration (no cross-blocking) | test_fn_axi4_03 | FN-ARB-04-06, FN-ARB-18-19 | ✅ COVERED |
| SF-14 | §4.6 | Deadlock freedom | test_fn_axi4_03 | FN-ARB-06, FN-ARB-09 | ✅ COVERED |
| SF-15 | §4.7 | Contention scenarios (same slave / different slaves) | test_fn_axi4_03 | FN-ARB-01-21 | ✅ COVERED |
| SF-16 | §5.1 | DMI forward: decode, translate, forward, translate range | test_tlm_axi4_01 | TLM-DMI-01, TLM-DMI-02, TLM-DMI-04-06, TLM-DMI-10 | ✅ COVERED |
| SF-17 | §5.1 | DMI forward: address restoration | test_tlm_axi4_01 | TLM-DMI-08, TLM-DMI-09 | ✅ COVERED |
| SF-18 | §5.1 | DMI forward: returns false for unmapped | test_tlm_axi4_01 | TLM-DMI-03 | ✅ COVERED |
| SF-19 | §5.1 | DMI forward: slave false → interconnect false | test_tlm_axi4_01 | TLM-DMI-11, TLM-DMI-12, TLM-DMI-13 | ✅ COVERED |
| SF-20 | §5.3 | DMI invalidation: translate to global, broadcast ALL masters | test_tlm_axi4_01 | TLM-DMI-14-19, TLM-DMI-22-29 | ✅ COVERED |
| SF-21 | §5.4 | DMI partial invalidation translation | test_tlm_axi4_01 | TLM-DMI-20, TLM-DMI-21, TLM-DMI-24, TLM-DMI-25 | ✅ COVERED |
| SF-22 | §5.6 | DMI does not acquire mutex | test_tlm_axi4_01 | TLM-DMI-07 | ✅ COVERED |
| SF-23 | §6.1 | Debug transport: decode, translate, forward, restore | test_tlm_axi4_02 | TLM-DBG-01-04, TLM-DBG-08-10 | ✅ COVERED |
| SF-24 | §6.1 | Debug transport: returns 0 for unmapped | test_tlm_axi4_02 | TLM-DBG-06 | ✅ COVERED |
| SF-25 | §6.2 | Debug transport: no mutex, no blocking | test_tlm_axi4_02 | TLM-DBG-05 | ✅ COVERED |
| SF-26 | §7.1 | Only TLM_ADDRESS_ERROR_RESPONSE generated by interconnect | test_ec_axi4_01 | EC-AXI4-04 | ✅ COVERED |
| SF-27 | §7.2 | Interconnect never generates GENERIC/BURST/INCOMPLETE errors | test_ec_axi4_01 | EC-AXI4-04, EC-AXI4-05 | ✅ COVERED |
| SF-28 | §7.3 | Transaction spanning regions: route by START address only | test_fn_axi4_04 | FN-RW-56, FN-RW-57, FN-RW-58 | ✅ COVERED |
| SF-29 | §3.3 | Zero-copy: same data_ptr forwarded to slave | test_fn_axi4_04 | FN-RW-59, FN-RW-60 | ✅ COVERED |
| SF-30 | §3.3 | TLM_IGNORE_COMMAND forwarded | test_fn_axi4_01 | FN-RW-17, FN-RW-29, FN-RW-30 | ✅ COVERED |
| SF-31 | §1.2 | Full crossbar connectivity (any master → any slave) | test_fn_axi4_01, test_fn_axi4_03 | FN-RW-39-42, FN-ARB-18-19 | ✅ COVERED |
| SF-32 | §13.8 | Zero-length transactions forwarded | test_fn_axi4_01, test_fn_axi4_04 | FN-RW-27, FN-RW-61-64 | ✅ COVERED |
| SF-33 | §12.1 | Initial state: last_served = NUM_MASTERS−1 | test_fn_axi4_03 | FN-ARB-12, FN-ARB-13 | ✅ COVERED |
| SF-34 | §4.5 | Starvation prevention | test_fn_axi4_03 | FN-ARB-16, FN-ARB-17 | ✅ COVERED |
| SF-35 | §5.2 | No DMI caching by interconnect | test_fn_axi4_04 | TLM-DMI-30, TLM-DMI-31 | ✅ COVERED |
| SF-36 | §10.4 | No transaction memory management (no acquire/release) | test_fn_axi4_04 | FN-RW-51, FN-RW-52, FN-RW-53 | ✅ COVERED |
| SF-37 | §3.3 | Byte enable passthrough | test_fn_axi4_04 | FN-RW-44, FN-RW-45, FN-RW-46, FN-RW-47 | ✅ COVERED |
| SF-38 | §3.3 | Extensions remain attached (same object) | test_fn_axi4_04 | FN-RW-48, FN-RW-49, FN-RW-50 | ✅ COVERED |

---

## 4. Coverage Summary

| Metric | Value |
|--------|-------|
| Total spec features (SF-01 to SF-38) | 38 |
| Features COVERED | 38 |
| Features NOT COVERED | 0 |
| Features PARTIAL | 0 |
| **Overall coverage** | **100.0%** (38/38) |

### Assertion Distribution by Category

| Category | Assertions | Test Function(s) |
|----------|:---:|---|
| Address decode & routing | 42 | test_fn_axi4_01 |
| Address translation | 7 | test_fn_axi4_02 |
| Error handling | 5 | test_ec_axi4_01 |
| Multi-master arbitration | 23 | test_fn_axi4_03 |
| DMI forward & backward | 31 | test_tlm_axi4_01, test_fn_axi4_04 |
| Debug transport | 10 | test_tlm_axi4_02 |
| Payload integrity & corner cases | 21 | test_fn_axi4_04 |

### Coverage Gaps

**None.** All 38 spec features are fully covered by the testbench with dedicated assertions verified by the golden implementation run.

---

*End of Coverage Report*