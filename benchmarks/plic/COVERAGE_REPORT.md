# PLIC Functional Coverage Traceability Report

**Generated:** From golden testbench run output  
**Benchmark:** RISC-V PLIC (LLM-VPBench, Tier: Medium)  
**Spec:** `spec/plic-spec.md`  
**Testbench:** `testbench/plic_testbench.cpp`  
**Golden DUT:** `golden/plic.h`

---

## 1. Overview

| Metric | Value |
|--------|-------|
| Total Assertions | 168 |
| Passed | 168 |
| Failed | 0 |
| Test Functions | 12 |
| Unique Checkpoint IDs | 93 |
| Spec Features Identified | 65 |
| Features Covered | 62 |
| Features Not Covered | 3 (structural/non-testable) |
| Result | **ALL PASS ✓** |

---

## 2. Test Function Summary

| # | Function | Description | Assertions | Category |
|---|----------|-------------|:---:|----------|
| 1 | `test_fn_plic_01` | Register Reset & Access | 22 | Register Access |
| 2 | `test_ec_plic_01` | Address Decode & Error Responses | 8 | Error Handling |
| 3 | `test_fn_plic_02` | Hardwired Zero Bits | 5 | Bit Masking |
| 4 | `test_fn_plic_03` | Level-Sensitive Interrupt Detection | 12 | Gateway (Level) |
| 5 | `test_fn_plic_04` | Edge-Triggered Interrupt Detection | 9 | Gateway (Edge) |
| 6 | `test_fn_plic_05` | Gateway State Machine | 15 | Gateway Protocol |
| 7 | `test_fn_plic_06` | Priority Arbitration | 14 | Arbitration |
| 8 | `test_ir_plic_01` | IRQ Output Generation | 10 | IRQ Output |
| 9 | `test_fn_plic_07` | INT_TYPE Change Interactions | 7 | INT_TYPE |
| 10 | `test_fn_plic_08` | Claim/Complete Protocol Edge Cases | 12 | Claim/Complete |
| 11 | `test_ec_plic_02` | Edge Cases & Gap Coverage | 31 | Edge Cases |
| 12 | `test_ec_plic_03` | Coverage Gap Closure + Zero Delay | 23 | Coverage Gaps |

---

## 3. Spec Feature Traceability

| Feature ID | Spec Section | Description | Test Function(s) | Assertion IDs | Status |
|:---:|:---:|:---|:---|:---|:---:|
| F01 | §3.1, §10 | PRIORITY registers reset to 0, RW, WARL | test_fn_plic_01 | FN-REG-01, FN-REG-07, FN-PRIO-04 | ✅ COVERED |
| F02 | §3.2, §10 | PENDING register reset to 0, read-only, writes ignored | test_fn_plic_01 | FN-REG-02, FN-REG-09 | ✅ COVERED |
| F03 | §3.2, §10 | ENABLE register reset to 0, RW, bit 0 hardwired 0 | test_fn_plic_01, test_fn_plic_02 | FN-REG-03, FN-PRIO-01 | ✅ COVERED |
| F04 | §3.2, §10 | THRESHOLD register reset to 0, RW, WARL | test_fn_plic_01 | FN-REG-04, FN-REG-08 | ✅ COVERED |
| F05 | §3.2, §10 | CLAIM/COMPLETE reset read-value 0 | test_fn_plic_01 | FN-REG-05, FN-REG-10 | ✅ COVERED |
| F06 | §3.2, §10 | INT_TYPE register reset to 0, RW, bit 0 hardwired 0 | test_fn_plic_01, test_fn_plic_02 | FN-REG-06, FN-PRIO-03 | ✅ COVERED |
| F07 | §3.1, §11 | Source 0 reserved: reads 0, writes ignored, no error | test_ec_plic_01, test_ec_plic_03 | EC-ADDR-01, EC-GAP-02 | ✅ COVERED |
| F08 | §11 | Bit 0 of PENDING/ENABLE/INT_TYPE hardwired 0 | test_fn_plic_02, test_ec_plic_02 | FN-PRIO-01, FN-PRIO-02, FN-PRIO-03, EC-EDGE-03 | ✅ COVERED |
| F09 | §3.3 | WARL semantics: upper bits read-as-zero | test_fn_plic_01, test_fn_plic_02 | FN-REG-07, FN-REG-08, FN-PRIO-04 | ✅ COVERED |
| F10 | §4.1 | Level-sensitive: IDLE→PENDING on input high | test_fn_plic_03 | FN-PEND-01 | ✅ COVERED |
| F11 | §4.1 | Level-sensitive: pending tracks input (low clears) | test_fn_plic_03 | FN-PEND-02, FN-PEND-04 | ✅ COVERED |
| F12 | §4.1 | Level-sensitive: PENDING→CLAIMED on claim | test_fn_plic_03 | FN-PEND-06 | ✅ COVERED |
| F13 | §4.1 | Level-sensitive: input ignored while CLAIMED | test_fn_plic_03 | FN-PEND-06 | ✅ COVERED |
| F14 | §4.1 | Level-sensitive: CLAIMED→IDLE; re-pend if input high | test_fn_plic_03 | FN-PEND-07, FN-PEND-08 | ✅ COVERED |
| F15 | §4.1 | Level-sensitive: multiple sources pend independently | test_fn_plic_03 | FN-PEND-03 | ✅ COVERED |
| F16 | §4.2 | Edge-triggered: IDLE→PENDING on rising edge | test_fn_plic_04 | FN-EN-01 | ✅ COVERED |
| F17 | §4.2 | Edge-triggered: pending latched (input low doesn't clear) | test_fn_plic_04 | FN-EN-02 | ✅ COVERED |
| F18 | §4.2 | Edge-triggered: additional edges while pending are lost | test_fn_plic_04 | FN-EN-03 | ✅ COVERED |
| F19 | §4.2 | Edge-triggered: PENDING→CLAIMED on claim | test_fn_plic_04 | FN-EN-04 | ✅ COVERED |
| F20 | §4.2 | Edge-triggered: rising edges during CLAIMED are lost | test_fn_plic_04, test_ec_plic_02 | FN-EN-05, EC-EDGE-12 | ✅ COVERED |
| F21 | §4.2 | Edge-triggered: CLAIMED→IDLE; receptive to new edges | test_fn_plic_04 | FN-EN-06 | ✅ COVERED |
| F22 | §4.2 | Edge-triggered: no retroactive capture during CLAIMED | test_ec_plic_02 | EC-EDGE-12 | ✅ COVERED |
| F23 | §4.2 | Edge-triggered: static high doesn't trigger (needs 0→1) | test_fn_plic_07, test_ec_plic_03 | FN-TYPE-04, EC-GAP-04 | ✅ COVERED |
| F24 | §4.2 | Edge-triggered: complete with input high no auto-repend | test_fn_plic_07, test_ec_plic_03 | FN-TYPE-04, EC-GAP-04 | ✅ COVERED |
| F25 | §5.1 | Qualification: pending + enabled + prio>0 + prio>threshold | test_fn_plic_06 | FN-CLAIM-03, FN-CLAIM-04, FN-CLAIM-05, FN-CLAIM-08 | ✅ COVERED |
| F26 | §5.2 | Winner selection: highest priority wins | test_fn_plic_06 | FN-CLAIM-01 | ✅ COVERED |
| F27 | §5.2 | Winner selection: tie-break by lowest source ID | test_fn_plic_06, test_ec_plic_02 | FN-CLAIM-02, EC-EDGE-09 | ✅ COVERED |
| F28 | §5.2 | No qualifying source → CLAIM returns 0 | test_fn_plic_06, test_ec_plic_02 | FN-CLAIM-03, EC-EDGE-07 | ✅ COVERED |
| F29 | §5.1 | Priority 0 never qualifies | test_fn_plic_06, test_fn_plic_08 | FN-CLAIM-03, FN-MULTI-07 | ✅ COVERED |
| F30 | §5.1 | Threshold comparison is strict greater (not ≥) | test_fn_plic_06, test_ec_plic_02 | FN-CLAIM-04, EC-EDGE-05 | ✅ COVERED |
| F31 | §5.1 | Maximum threshold (7) blocks all sources | test_fn_plic_06 | FN-CLAIM-06 | ✅ COVERED |
| F32 | §5.1 | Disabled source excluded from arbitration | test_fn_plic_06, test_ec_plic_02 | FN-CLAIM-08, EC-EDGE-04 | ✅ COVERED |
| F33 | §5.3 | Dynamic priority/enable/threshold changes immediate | test_fn_plic_06, test_ec_plic_02 | FN-CLAIM-07, EC-EDGE-04, EC-EDGE-05 | ✅ COVERED |
| F34 | §6.1 | Claim atomically transitions gateway, returns winner | test_fn_plic_03, test_fn_plic_04 | FN-PEND-06, FN-EN-04 | ✅ COVERED |
| F35 | §6.2 | Complete transitions CLAIMED→IDLE | test_fn_plic_05 | FN-THRESH-01, FN-THRESH-02 | ✅ COVERED |
| F36 | §6.2 | Complete with ID=0 silently ignored | test_fn_plic_05 | FN-THRESH-05 | ✅ COVERED |
| F37 | §6.2 | Complete with ID>S silently ignored | test_fn_plic_05 | FN-THRESH-06 | ✅ COVERED |
| F38 | §6.2 | Complete for non-CLAIMED source silently ignored | test_fn_plic_05, test_ec_plic_02 | FN-THRESH-04, EC-EDGE-08 | ✅ COVERED |
| F39 | §6.3 | Multiple sources simultaneously CLAIMED | test_fn_plic_05, test_fn_plic_08 | FN-THRESH-03, FN-MULTI-01, FN-MULTI-02 | ✅ COVERED |
| F40 | §6.1 | Claim re-evaluates EIP (irq_out updates) | test_ir_plic_01, test_ec_plic_02 | IR-PLIC-02, EC-EDGE-11 | ✅ COVERED |
| F41 | §6.2 | Complete re-evaluates EIP | test_ir_plic_01, test_ec_plic_02 | IR-PLIC-06, EC-EDGE-11 | ✅ COVERED |
| F42 | §7.1 | irq_out asserts when ≥1 source qualifies | test_ir_plic_01 | IR-PLIC-01 | ✅ COVERED |
| F43 | §7.1 | irq_out deasserts when none qualify | test_ir_plic_01 | IR-PLIC-02 | ✅ COVERED |
| F44 | §7.2 | irq_out re-evaluated on src_irq change | test_ir_plic_01 | IR-PLIC-01 | ✅ COVERED |
| F45 | §7.2 | irq_out re-evaluated on PRIORITY write | test_fn_plic_06 | FN-CLAIM-07 | ✅ COVERED |
| F46 | §7.2 | irq_out re-evaluated on ENABLE write | test_ec_plic_02 | EC-EDGE-04 | ✅ COVERED |
| F47 | §7.2 | irq_out re-evaluated on THRESHOLD write | test_ir_plic_01, test_ec_plic_02 | IR-PLIC-04, EC-EDGE-05 | ✅ COVERED |
| F48 | §7.2 | irq_out re-evaluated on INT_TYPE write | test_ec_plic_02 | EC-EDGE-06 | ✅ COVERED |
| F49 | §7.1 | irq_out is level-sensitive (stays high) | test_ir_plic_01, test_ec_plic_03 | IR-PLIC-03, EC-GAP-03 | ✅ COVERED |
| F50 | §10 | irq_out false at reset | test_ec_plic_03 | EC-GAP-01 | ✅ COVERED |
| F51 | §3.4 | Address out of range → TLM_ADDRESS_ERROR_RESPONSE | test_ec_plic_01, test_ec_plic_02 | EC-ADDR-03, EC-ADDR-04, EC-EDGE-02 | ✅ COVERED |
| F52 | §3.4 | Misaligned access → TLM_ADDRESS_ERROR_RESPONSE | test_ec_plic_01, test_ec_plic_02 | EC-ADDR-05, EC-ADDR-06, EC-EDGE-10 | ✅ COVERED |
| F53 | §3.4 | Data length ≠ 4 → TLM_GENERIC_ERROR_RESPONSE | test_ec_plic_01 | EC-ADDR-07 | ✅ COVERED |
| F54 | §3.4 | TLM_IGNORE_COMMAND → TLM_GENERIC_ERROR_RESPONSE | test_ec_plic_01, test_ec_plic_02 | EC-ADDR-08, EC-EDGE-01 | ✅ COVERED |
| F55 | §3.4 | Error precedence: address checked before length | test_ec_plic_02 | EC-EDGE-02 | ✅ COVERED |
| F56 | §3.5 | Valid address region boundaries accepted | test_ec_plic_01 | EC-ADDR-01, EC-ADDR-02 | ✅ COVERED |
| F57 | §3.2 | INT_TYPE edge→level: pending clears if input low | test_fn_plic_07, test_ec_plic_02 | FN-TYPE-01, EC-EDGE-06 | ✅ COVERED |
| F58 | §3.2 | INT_TYPE edge→level: pending remains if input high | test_fn_plic_07 | FN-TYPE-02 | ✅ COVERED |
| F59 | §3.2 | INT_TYPE level→edge: pending remains (already latched) | test_fn_plic_07 | FN-TYPE-03 | ✅ COVERED |
| F60 | §3.2 | INT_TYPE change while CLAIMED: no effect until complete | test_fn_plic_07, test_ec_plic_03 | FN-TYPE-04, EC-GAP-05, EC-GAP-06 | ✅ COVERED |
| F61 | §10 | Input high at simulation start detected | test_fn_plic_04 | FN-EN-07 | ✅ COVERED |
| F62 | §11 | Disable while CLAIMED: complete still required | test_fn_plic_08 | FN-MULTI-06 | ✅ COVERED |
| F63 | §8.1 | Blocking transport with zero delay | test_ec_plic_03 | EC-ZDEL-01, EC-ZDEL-02, EC-ZDEL-03 | ✅ COVERED |
| F64 | §12 | Header-only implementation | - | - | ⬜ NOT COVERED |
| F65 | §9.1 | Module interface: socket, src_irq, irq_out | - | - | ⬜ NOT COVERED |

---

## 4. Coverage Summary

| Metric | Value |
|--------|-------|
| **Total Spec Features** | 65 |
| **COVERED (runtime-verified)** | 63 |
| **NOT COVERED (structural only)** | 2 |
| **Functional Coverage** | **96.9%** |

### Gaps

| Feature | Gap Description | Justification |
|:---:|:---|:---|
| F64 | Header-only implementation constraint | Build system / structural requirement; not runtime-verifiable |
| F65 | Module interface conformance (port names/types) | Verified by successful compilation; not a runtime assertion |

### Notes

- **F63 (zero-delay b_transport)** is now explicitly verified via EC-ZDEL-01/02/03 assertions that check simulation time does not advance during transactions.
- All 168 assertions pass against the golden `plic.h` implementation.
- The testbench covers all RISC-V PLIC spec §1-§11 functional requirements including the benchmark extension (INT_TYPE register).

---

*End of Coverage Report*
