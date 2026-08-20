# NoC Router - Functional Coverage Traceability Report

Generated from spec (`noc-router-spec.md`), golden testbench run, and testbench source (`noc_router_testbench.cpp`).

---

## 1. Overview

| Field | Value |
|-------|-------|
| Benchmark | NoC Crossbar Router (LLM-VPBench, Medium Tier) |
| Spec File | `spec/noc-router-spec.md` |
| Testbench | `testbench/noc_router_testbench.cpp` |
| Golden Impl | `golden/` |
| Test Functions | 33 |
| Total Assertions | 301 |
| Passed | 301 |
| Failed | 0 |
| Result | **ALL PASS ✓** |

---

## 2. Test Function Summary

| # | Function | Assertions | Category |
|---|----------|-----------|----------|
| 1 | `test_fn_noc_01` - Reset & Default Configuration | 20 | Register defaults, WARL |
| 2 | `test_fn_noc_02` - Address Decode & Routing | 22 | Route table, priority, boundaries |
| 3 | `test_fn_noc_03` - Round-Robin Arbitration | 10 | RR scheduling, bypass |
| 4 | `test_fn_noc_04` - Fixed Priority Arbitration | 8 | FP mode, QoS interaction |
| 5 | `test_fn_noc_05` - Weighted Round-Robin Arbitration | 8 | WRR weights, WARL |
| 6 | `test_fn_noc_06` - Route Cache | 15 | Hit/miss, flush, capacity |
| 7 | `test_fn_noc_07` - Data Cache | 18 | Alloc, write-through, flush |
| 8 | `test_fn_noc_08` - Quality of Service | 13 | QoS priority registers |
| 9 | `test_fn_noc_09` - Timeout Handling | 8 | Timeout config, status, IRQ |
| 10 | `test_ir_noc_01` - Interrupt Generation | 12 | IRQ assert/deassert, W1C |
| 11 | `test_fn_noc_10` - Statistics Counters | 12 | Enable, increment, clear |
| 12 | `test_fn_noc_11` - Error Handling | 12 | Config & data path errors |
| 13 | `test_tlm_noc_01` - Non-Blocking Transport | 15 | NB protocol, completion |
| 14 | `test_fn_noc_12` - Feature Combinations | 28 | Multi-feature interaction |
| 15 | `test_fn_noc_13` - Stress & Corner Cases | 30 | High-volume, capacity |
| 16 | `test_ec_noc_01` - Data Cache Per-Byte Valid | 10 | Byte-level validity |
| 17 | `test_ec_noc_02` - Route Cache Extended | 4 | Flush-on-write, disable |
| 18 | `test_ec_noc_03` - STATUS Register W1C | 3 | W1C semantics |
| 19 | `test_ec_noc_04` - Statistics Control | 3 | STAT_CTRL edge cases |
| 20 | `test_ec_noc_05` - Feature Toggle Coherence | 6 | Cache flush on disable |
| 21 | `test_ec_noc_06` - WARL Register Edge Cases | 3 | ARB_MODE, WEIGHT clamping |
| 22 | `test_ec_noc_07` - Reserved Address Gaps | 5 | Reserved addr errors |
| 23 | `test_ec_noc_08` - Cache Flush Control | 4 | CACHE_CTRL behavior |
| 24 | `test_ec_noc_09` - Non-Blocking Extended | 1 | NB + data cache |
| 25 | `test_ec_noc_10` - TLM_IGNORE_COMMAND | 3 | IGNORE cmd errors |
| 26 | `test_ec_noc_11` - Error Priority Ordering | 4 | Error precedence |
| 27 | `test_ec_noc_12` - Route Cache LRU Eviction | 5 | LRU policy |
| 28 | `test_ec_noc_13` - Disabled Router Behavior | 3 | router_en=0 |
| 29 | `test_ec_noc_14` - Timeout Post-Hoc Check | 4 | No false timeouts |
| 30 | `test_ec_noc_15` - Route Register WARL | 4 | ROUTE_TGT/CTRL clamping |
| 31 | `test_ec_noc_16` - Data Cache Indexing & Eviction | 4 | Tag-conflict eviction |
| 32 | `test_ec_noc_17` - Statistics Counter Saturation | 2 | Monotonic, no wrap |
| 33 | `test_ec_noc_18` - STAT_CTRL Clear | 2 | Clear-all semantics |

---

## 3. Spec Feature Traceability

| Spec Feature (§) | Test Functions | Assertion IDs | Status |
|---|---|---|---|
| b_transport only (§2.4) | test_fn_noc_02, test_tlm_noc_01 | FN-ROUTE-01..22, TLM-NB-01..15 | ✅ COVERED |
| Tagged sockets 4×4+1 (§2.1) | test_fn_noc_01, test_fn_noc_02 | FN-RST-07, FN-RST-08, FN-ROUTE-01 | ✅ COVERED |
| IRQ output (§2.2) | test_ir_noc_01 | IR-IRQ-01..12 | ✅ COVERED |
| Constructor parameters (§2.3) | test_fn_noc_01 | FN-RST-07, FN-RST-08 | ✅ COVERED |
| Config register access policy (§3) | test_fn_noc_11, test_ec_noc_07 | FN-ERR-01..12, EC-RSVD-01..05 | ✅ COVERED |
| Config error priority ordering (§3) | test_ec_noc_11 | EC-ERRPRI-01..04 | ✅ COVERED |
| GLOBAL_CTRL register (§4.1) | test_fn_noc_01 | FN-RST-01, FN-RST-09, FN-RST-10 | ✅ COVERED |
| ARB_MODE WARL (§4.1) | test_fn_noc_01, test_ec_noc_06 | FN-RST-02, FN-RST-11, FN-RST-12, EC-WARL-01..03 | ✅ COVERED |
| TIMEOUT_CFG register (§4.1) | test_fn_noc_01, test_fn_noc_09 | FN-RST-03, FN-RST-13, FN-TMO-01..08 | ✅ COVERED |
| STATUS W1C (§4.1) | test_fn_noc_01, test_ec_noc_03 | FN-RST-04, EC-STATUS-01..03 | ✅ COVERED |
| IRQ_EN register (§4.1) | test_fn_noc_01, test_ir_noc_01 | FN-RST-05, FN-RST-14, IR-IRQ-01..12 | ✅ COVERED |
| VERSION register RO (§4.1) | test_fn_noc_01 | FN-RST-06, FN-RST-15 | ✅ COVERED |
| N_INIT_RO / N_TGT_RO (§4.1) | test_fn_noc_01 | FN-RST-07, FN-RST-08, FN-RST-16, FN-RST-17 | ✅ COVERED |
| Route rules priority (§4.2) | test_fn_noc_02 | FN-ROUTE-03..08 | ✅ COVERED |
| ROUTE_START/END inclusive/exclusive (§4.2) | test_fn_noc_02 | FN-ROUTE-13..15, FN-ROUTE-21 | ✅ COVERED |
| ROUTE_TGT WARL clamping (§4.2) | test_fn_noc_02, test_ec_noc_15 | FN-ROUTE-22, EC-RTWARL-01..04 | ✅ COVERED |
| Route valid bit gating (§4.2) | test_fn_noc_02 | FN-ROUTE-09..12, FN-ROUTE-16 | ✅ COVERED |
| Route reg write invalidates cache (§4.2) | test_fn_noc_06, test_ec_noc_02 | FN-RCACHE-05..08, EC-RCACHE-01..04 | ✅ COVERED |
| Statistics counters (§4.3) | test_fn_noc_10 | FN-STAT-01..12 | ✅ COVERED |
| STAT_CTRL clear (§4.3) | test_ec_noc_04, test_ec_noc_18 | EC-STATCTRL-01..03, EC-STATCLR-01..02 | ✅ COVERED |
| Counter saturation (§4.3) | test_ec_noc_17 | EC-SATURATION-01..02 | ✅ COVERED |
| stats_en gating (§4.3) | test_fn_noc_10, test_ec_noc_05 | FN-STAT-01..02, EC-FTOGGLE-04..06 | ✅ COVERED |
| Cache info registers RO (§4.4) | test_fn_noc_06, test_fn_noc_07 | FN-RCACHE-13..15, FN-DCACHE-14..15 | ✅ COVERED |
| Data path validation (§5) | test_fn_noc_11, test_ec_noc_13 | FN-ERR-07..10, EC-DISROUTER-01..03 | ✅ COVERED |
| Address decode (§5) | test_fn_noc_02 | FN-ROUTE-01..22 | ✅ COVERED |
| Arbitration invocation (§5) | test_fn_noc_03, test_fn_noc_04, test_fn_noc_05 | FN-ARB-01..26 | ✅ COVERED |
| Data cache read path (§5, §8.3) | test_fn_noc_07, test_ec_noc_01 | FN-DCACHE-01..18, EC-DCACHE-01..10 | ✅ COVERED |
| Data cache write path (§5, §8.4) | test_fn_noc_07 | FN-DCACHE-06..07 | ✅ COVERED |
| Forward & post-forward (§5) | test_fn_noc_02 | FN-ROUTE-01..02 | ✅ COVERED |
| Decode error detail (§5.1) | test_fn_noc_02, test_ec_noc_03 | FN-ROUTE-11..12, EC-STATUS-01..03 | ✅ COVERED |
| Statistics accounting (§5.2) | test_fn_noc_10 | FN-STAT-03..12 | ✅ COVERED |
| Round-robin arbitration (§6.1) | test_fn_noc_03 | FN-ARB-01..10 | ✅ COVERED |
| Fixed-priority arbitration (§6.2) | test_fn_noc_04 | FN-ARB-11..18 | ✅ COVERED |
| Route cache 8 entries LRU (§7) | test_fn_noc_06, test_ec_noc_12 | FN-RCACHE-01..15, EC-LRU-01..05 | ✅ COVERED |
| Route cache range match/MRU (§7) | test_fn_noc_06, test_ec_noc_12 | FN-RCACHE-01..04, EC-LRU-01..05 | ✅ COVERED |
| Route cache flush (§7) | test_fn_noc_06, test_ec_noc_02 | FN-RCACHE-05..09, EC-RCACHE-01..04 | ✅ COVERED |
| Data cache 16×32B (§8.1) | test_fn_noc_06, test_ec_noc_16 | FN-RCACHE-14..15, EC-DINDEX-01..04 | ✅ COVERED |
| Data cache indexing (§8.1) | test_ec_noc_16 | EC-DINDEX-01..04 | ✅ COVERED |
| Per-byte validity (§8.2) | test_ec_noc_01 | EC-DCACHE-01..10 | ✅ COVERED |
| Read hit requires all bytes valid (§8.3) | test_ec_noc_01 | EC-DCACHE-03..06, EC-DCACHE-10 | ✅ COVERED |
| Eviction on tag conflict (§8.3) | test_ec_noc_16 | EC-DINDEX-01..04 | ✅ COVERED |
| Write-through resident line (§8.4) | test_fn_noc_07, test_ec_noc_01 | FN-DCACHE-06, EC-DCACHE-06 | ✅ COVERED |
| No-write-allocate (§8.4) | test_fn_noc_07 | FN-DCACHE-07 | ✅ COVERED |
| Data cache flush on disable (§8.5) | test_ec_noc_05, test_ec_noc_08 | EC-FTOGGLE-01..04, EC-CACHEFLUSH-01..04 | ✅ COVERED |
| DATA_CACHE_VALID observable (§8.6) | test_fn_noc_07, test_ec_noc_08 | FN-DCACHE-14..15, EC-CACHEFLUSH-01..04 | ✅ COVERED |
| Timeout monitoring (§9) | test_fn_noc_09, test_ec_noc_14 | FN-TMO-01..08, EC-TMOCHK-01..04 | ✅ COVERED |
| IRQ combinational logic (§10) | test_ir_noc_01 | IR-IRQ-01..12 | ✅ COVERED |
| IRQ re-evaluation (§10) | test_ir_noc_01 | IR-IRQ-05..12 | ✅ COVERED |
| Reset state (§11) | test_fn_noc_01 | FN-RST-01..20 | ✅ COVERED |
| Downstream error forwarding (§5) | test_fn_noc_11 | FN-ERR-11..12 | ✅ COVERED |
| Non-cacheable bypass (§5) | test_fn_noc_07 | FN-DCACHE-12 | ✅ COVERED |
| Feature gating independent (§13) | test_fn_noc_12, test_ec_noc_05 | FN-COMBO-01..20, EC-FTOGGLE-01..06 | ✅ COVERED |

---

## 4. Coverage Summary

| Metric | Count |
|--------|-------|
| Spec Features Identified | 52 |
| Features COVERED | 52 |
| Features PARTIAL | 0 |
| Features NOT COVERED | 0 |
| Total Test Functions | 33 |
| Total Assertion Checkpoints | 301 |
| Assertions Passed | 301 |
| Assertions Failed | 0 |
| **Overall Feature Coverage** | **100%** |

### Assertion ID Prefixes

| Prefix | Count | Category |
|--------|-------|----------|
| FN-RST-* | 20 | Reset & register defaults |
| FN-ROUTE-* | 22 | Address decode & routing |
| FN-ARB-* | 26 | Arbitration (RR, FP, WRR) |
| FN-RCACHE-* | 15 | Route cache |
| FN-DCACHE-* | 18 | Data cache |
| FN-QOS-* | 13 | Quality of Service |
| FN-TMO-* | 8 | Timeout handling |
| FN-STAT-* | 12 | Statistics counters |
| FN-ERR-* | 12 | Error handling |
| FN-COMBO-* | 28 | Feature combinations |
| FN-STRESS-* | 30 | Stress & corner cases |
| IR-IRQ-* | 12 | Interrupt generation |
| TLM-NB-* | 15 | Non-blocking transport |
| EC-DCACHE-* | 10 | Per-byte validity edge cases |
| EC-RCACHE-* | 4 | Route cache edge cases |
| EC-STATUS-* | 3 | STATUS W1C edge cases |
| EC-STATCTRL-* | 3 | STAT_CTRL edge cases |
| EC-FTOGGLE-* | 6 | Feature toggle coherence |
| EC-WARL-* | 3 | WARL edge cases |
| EC-RSVD-* | 5 | Reserved address gaps |
| EC-CACHEFLUSH-* | 4 | Cache flush control |
| EC-NB-* | 1 | NB + cache interaction |
| EC-IGNORE-* | 3 | TLM_IGNORE_COMMAND |
| EC-ERRPRI-* | 4 | Error priority ordering |
| EC-LRU-* | 5 | Route cache LRU eviction |
| EC-DISROUTER-* | 3 | Disabled router behavior |
| EC-TMOCHK-* | 4 | Timeout post-hoc check |
| EC-RTWARL-* | 4 | Route register WARL |
| EC-DINDEX-* | 4 | Data cache indexing |
| EC-SATURATION-* | 2 | Counter saturation |
| EC-STATCLR-* | 2 | STAT_CTRL clear |

### Coverage Gaps

**No spec requirements are left without test coverage.**

### Notes on Spec vs. Testbench Discrepancies

1. **Testbench extensions beyond spec**: The testbench tests `nb_transport_fw` (TLM-NB-*), QoS priority registers (FN-QOS-*), weighted round-robin (FN-ARB-19..26), and `CACHE_CTRL` flush register - these go beyond the spec which explicitly lists QoS and AT/NB as non-goals. Submissions strictly following the spec may fail these checks.
2. **VERSION value**: Testbench checks VERSION = 0x00010000 (`FN-RST-06`); spec states 0x00030000 - discrepancy.
3. **ARB_MODE WARL**: Testbench accepts value 2 (`FN-RST-12` checks accepted); spec says values ≥2 clamp to 0 - discrepancy.
4. **GLOBAL_CTRL masking**: Testbench writes 0xFF and expects 0xFF stored (`FN-RST-09`); spec reserves bits [31:6] (writes ignored, reads zero) - discrepancy.

---

*Generated from golden testbench run: 301 passed, 0 failed, 301 total.*
