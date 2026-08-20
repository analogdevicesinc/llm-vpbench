# DMA Controller - Functional Coverage Report

**Benchmark:** LLM-VPBench / Multi-Channel DMA Controller  
**Spec:** `spec/dma-spec.md` (16 sections)  
**Testbench:** `testbench/dma_testbench.cpp`  
**Golden DUT:** `golden/dma_controller.h`  
**Test Functions:** 27  
**Total Assertions:** 233 passed, 0 failed  
**Result:** ALL PASS ✓

---

## 1. Overview

The DMA Controller testbench verifies a 4-channel DMA controller modeled as a SystemC TLM-2.0 IP against the specification in `dma-spec.md`. The golden implementation passes all 233 assertions across 27 test functions covering register reset values, address decoding, FSM transitions, transfer execution, address modes, priority arbitration, circular mode, linked mode, interrupts, pause/resume/abort, write restrictions, corner cases, and TLM protocol compliance.

---

## 2. Test Function Summary

| # | Test Function | Assertions | Category |
|---|---|:---:|---|
| 1 | `test_fn_dma_01` - Register reset | 19 | Register defaults (§16) |
| 2 | `test_ec_dma_01` - Address decode & RO/WO | 11 | Address map / access control (§3, §11) |
| 3 | `test_fn_dma_02` - GLOBAL_CTRL & ENABLE | 9 | Global enable & reset (§3.1, §10, §15) |
| 4 | `test_fn_dma_03` - FSM transitions | 9 | Channel state machine (§4) |
| 5 | `test_ec_dma_02` - Config validation | 8 | Transfer validation (§5) |
| 6 | `test_fn_dma_04` - Burst engine & data | 10 | Transfer execution (§6) |
| 7 | `test_fn_dma_05` - Address modes | 7 | Address modes (§3.4, §6.1) |
| 8 | `test_fn_dma_06` - Multi-channel fairness | 4 | Priority arbitration (§9) |
| 9 | `test_fn_dma_07` - Circular mode | 4 | Circular mode (§7.1) |
| 10 | `test_fn_dma_08` - Linked mode | 2 | Linked mode (§7.2) |
| 11 | `test_ir_dma_01` - Interrupts & masking | 7 | Interrupt system (§8) |
| 12 | `test_fn_dma_09` - PAUSE/RESUME/ABORT | 8 | Flow control (§4.2, §6.5) |
| 13 | `test_fn_dma_10` - Register lock in RUNNING | 3 | Write restrictions (§4.3) |
| 14 | `test_ec_dma_03` - Corner cases | 9 | Corner cases (§13) |
| 15 | `test_fn_dma_11` - SUSPENDED reprogram | 11 | Suspended reprogramming (§4.3) |
| 16 | `test_fn_dma_12` - Starvation prevention | 6 | Arbitration fairness (§9.1, §9.2) |
| 17 | `test_ec_dma_04` - SC bit ordering | 5 | Control bit ordering (§13.6) |
| 18 | `test_fn_dma_08b` - Linked edge cases | 11 | Link edge cases (§7.2) |
| 19 | `test_fn_dma_13` - Circular advanced | 8 | Circular wrap & interrupts (§7.1, §8) |
| 20 | `test_ir_dma_02` - IRQ signal outputs | 14 | IRQ logic & masking (§8.3) |
| 21 | `test_tlm_dma_01` - WARL boundary | 14 | TLM protocol & WARL (§3, §11) |
| 22 | `test_fn_dma_14` - Write restrictions | 21 | State-dependent writes (§4.3) |
| 23 | `test_fn_dma_15` - Transfer engine large | 6 | Large transfers (§6, §13.2) |
| 24 | `test_fn_dma_16` - Address mode reserved | 6 | Reserved mode & underflow (§3.4, §13.7) |
| 25 | `test_ec_dma_05` - Bus error | 9 | Bus error handling (§12, §14) |
| 26 | `test_fn_dma_17` - GLOBAL_CTRL.ENABLE gate | 5 | Enable semantics (§15) |
| 27 | `test_ec_dma_06` - Advanced corners | 7 | Multi-channel ordering (§9, §7.2) |
| | **TOTAL** | **233** | |

---

## 3. Spec Feature Traceability

| Feature ID | Spec Section | Feature Description | Test Function(s) | Assertion IDs | Status |
|:---:|:---:|---|---|---|:---:|
| F01 | §2 | TLM-2.0 interface (target/initiator sockets, irq, irq_global) | test_fn_dma_01, test_ir_dma_02 | FN-REG-19, IR-SIG-* | COVERED |
| F02 | §3.1 | Global registers (STATUS, INT_STATUS, INT_CLEAR, GLOBAL_CTRL, VERSION, INT_MASK) | test_fn_dma_01, test_ec_dma_01 | FN-REG-01..05, EC-ADDR-* | COVERED |
| F03 | §3.1 | VERSION register = 0x444D4102, writes ignored | test_fn_dma_01, test_ec_dma_01 | FN-REG-04, EC-ADDR-10 | COVERED |
| F04 | §3.1 | GLOBAL_CTRL: ENABLE (RW), RESET (SC), reserved WARL | test_fn_dma_02, test_tlm_dma_01 | FN-CTRL-01..05, TLM-DMA-01 | COVERED |
| F05 | §3.1 | INT_MASK reset = 0xFFF | test_fn_dma_01, test_fn_dma_02 | FN-REG-05, FN-CTRL-04 | COVERED |
| F06 | §3.1 | STATUS[3:0] reflects RUNNING/SUSPENDED channels | test_tlm_dma_01 | TLM-DMA-09..11 | COVERED |
| F07 | §3.2 | Per-channel register map (base 0x100 + N×0x40) | test_ec_dma_01, test_fn_dma_01 | EC-ADDR-02, FN-REG-17..18 | COVERED |
| F08 | §3.2 | CH_XFER_SIZE clamped to 0x10000 | test_tlm_dma_01 | TLM-DMA-04..07 | COVERED |
| F09 | §3.3 | CH_CTRL fields (START, CIRCULAR, PAUSE, RESUME, ABORT, CLR_ERR, WIDTH, BURST, PRIORITY) | test_fn_dma_03, test_fn_dma_09 | FN-FSM-*, FN-PAUSE-* | COVERED |
| F10 | §3.3 | CH_CTRL write restrictions (RUNNING/SUSPENDED) | test_fn_dma_10, test_fn_dma_14 | FN-LOCK-*, FN-WRES-12..14 | COVERED |
| F11 | §3.3 | SC bits self-clear, WARL reserved | test_fn_dma_02, test_tlm_dma_01 | FN-CTRL-05, TLM-DMA-02 | COVERED |
| F12 | §3.4 | CH_CONFIG: SRC_MODE, DST_MODE; mode=3 as fixed | test_fn_dma_16 | FN-ADEC-01..02 | COVERED |
| F13 | §3.4 | CH_CONFIG writable only in IDLE/CONFIGURED | test_fn_dma_10, test_fn_dma_14 | FN-LOCK-03, FN-WRES-11 | COVERED |
| F14 | §3.5 | CH_STATUS encoding (BUSY, ERROR, COMPLETE, STATE) | test_fn_dma_04, test_fn_dma_03 | FN-XFER-08..09, FN-FSM-* | COVERED |
| F15 | §3.6 | CH_LINK: TARGET[1:0], ENABLE[7] | test_fn_dma_08b | TLM-LINK-01..03 | COVERED |
| F16 | §4.1 | 6-state FSM (IDLE, CONFIGURED, RUNNING, SUSPENDED, COMPLETE, ERROR) | test_fn_dma_03 | FN-FSM-01..09 | COVERED |
| F17 | §4.2 | IDLE→CONFIGURED on data register write | test_fn_dma_14 | FN-WRES-01..02 | COVERED |
| F18 | §4.2 | CONFIGURED→RUNNING on START+ENABLE+valid | test_fn_dma_03 | FN-FSM-02 | COVERED |
| F19 | §4.2 | CONFIGURED→ERROR on START+validation fail | test_fn_dma_03, test_ec_dma_02 | FN-FSM-03, EC-VAL-* | COVERED |
| F20 | §4.2 | RUNNING→SUSPENDED on PAUSE | test_fn_dma_09 | FN-PAUSE-01 | COVERED |
| F21 | §4.2 | RUNNING→COMPLETE on REMAINING=0, CIRCULAR=0 | test_fn_dma_04 | FN-XFER-08..10 | COVERED |
| F22 | §4.2 | RUNNING→RUNNING (circular wrap) | test_fn_dma_07, test_fn_dma_13 | FN-CIRC-01..02 | COVERED |
| F23 | §4.2 | RUNNING→ERROR on bus error or ABORT | test_fn_dma_09, test_ec_dma_05 | FN-PAUSE-05, EC-BUS-01 | COVERED |
| F24 | §4.2 | SUSPENDED→RUNNING on RESUME | test_fn_dma_09 | FN-PAUSE-03 | COVERED |
| F25 | §4.2 | SUSPENDED→ERROR on ABORT | test_fn_dma_14 | FN-WRES-07 | COVERED |
| F26 | §4.2 | COMPLETE→IDLE on any register write | test_fn_dma_14 | FN-WRES-03..04 | COVERED |
| F27 | §4.2 | ERROR→IDLE on CLR_ERR | test_fn_dma_03, test_fn_dma_14 | FN-FSM-08, FN-WRES-17 | COVERED |
| F28 | §4.2 | Any→IDLE on global reset | test_fn_dma_02 | FN-CTRL-02..08 | COVERED |
| F29 | §4.3 | SUSPENDED reprogramming: SIZE recalculates REMAINING | test_fn_dma_11 | FN-SUSP-01..09 | COVERED |
| F30 | §4.3 | SUSPENDED: SRC/DST writes update CURR registers | test_fn_dma_11 | FN-SUSP-14..15 | COVERED |
| F31 | §5 | Transfer validation rules (WIDTH, SIZE, alignment) | test_ec_dma_02 | EC-VAL-01..06 | COVERED |
| F32 | §5 | Validation order: first failure aborts | test_fn_dma_14 | FN-WRES-21 | COVERED |
| F33 | §5 | START ignored when RUNNING/SUSPENDED or ENABLE=0 | test_fn_dma_02, test_ec_dma_02, test_fn_dma_14 | FN-CTRL-01, EC-VAL-07..08, FN-WRES-06 | COVERED |
| F34 | §5 | START from COMPLETE: implicit ack + validation | test_fn_dma_03 | FN-FSM-09 | COVERED |
| F35 | §6.1 | Burst execution: read then write, serialized | test_fn_dma_04 | FN-XFER-01..07 | COVERED |
| F36 | §6.1 | beats = min(burst_length, REMAINING/width) | test_fn_dma_04 | FN-XFER-04..06 | COVERED |
| F37 | §6.1 | Address update: inc/dec/fixed modes | test_fn_dma_05 | FN-AMODE-01..07 | COVERED |
| F38 | §6.2 | Completion: CIRCULAR=0→COMPLETE; CIRCULAR=1→wrap | test_fn_dma_04, test_fn_dma_07 | FN-XFER-08, FN-CIRC-* | COVERED |
| F39 | §6.3 | Half-transfer interrupt | test_ir_dma_01, test_fn_dma_13 | IR-DMA-05, FN-CIRC-10 | COVERED |
| F40 | §6.4 | Partial burst (REMAINING < width×burst) | test_fn_dma_04 | FN-XFER-07 | COVERED |
| F41 | §6.5 | Pause semantics: completion takes priority | test_ec_dma_03 | EC-CORNER-02 | COVERED |
| F42 | §7.1 | Circular mode: addresses reset, REMAINING reloads, WRAP_COUNT increments | test_fn_dma_07, test_fn_dma_13 | FN-CIRC-01..04, FN-CIRC-07..08 | COVERED |
| F43 | §7.1 | Stopping circular: clear CIRCULAR → complete after iteration | test_fn_dma_07 | FN-CIRC-03 | COVERED |
| F44 | §7.2 | Linked mode: completion triggers START on target | test_fn_dma_08 | FN-LINK-01 | COVERED |
| F45 | §7.2 | Link bypasses GLOBAL_CTRL.ENABLE gate | test_fn_dma_08b | TLM-LINK-05 | COVERED |
| F46 | §7.2 | Link ignored if target RUNNING/SUSPENDED | test_fn_dma_08b | TLM-LINK-07..09 | COVERED |
| F47 | §7.2 | Self-linking creates restart loop | test_fn_dma_08b | TLM-LINK-10 | COVERED |
| F48 | §7.2 | Circular takes precedence over link | test_fn_dma_08 | FN-LINK-02 | COVERED |
| F49 | §7.2 | Link fires after completion interrupt | test_fn_dma_08b | TLM-LINK-06 | COVERED |
| F50 | §7.2 | No hardware cycle detection (software responsibility) | - | - | NOT TESTED |
| F51 | §8.1 | Interrupt sources: complete/wrap, half-transfer, error | test_ir_dma_01, test_fn_dma_13 | IR-DMA-01..07, FN-CIRC-08..10 | COVERED |
| F52 | §8.2 | INT_MASK: 1=masked, masking doesn't prevent INT_STATUS set | test_ir_dma_01 | IR-DMA-02 | COVERED |
| F53 | §8.3 | irq[N] = OR of unmasked bits; irq_global = OR of all irq[N] | test_ir_dma_02 | IR-SIG-06..08 | COVERED |
| F54 | §8.3 | INT_STATUS sticky, cleared by W1C to INT_CLEAR | test_ir_dma_01 | IR-DMA-03 | COVERED |
| F55 | §8.3 | Outputs re-evaluate on status set, INT_CLEAR, INT_MASK, reset | test_ir_dma_02 | IR-SIG-09..14 | COVERED |
| F56 | §9.1 | Priority arbitration: highest priority first | test_fn_dma_06 | FN-PRIO-01 | COVERED |
| F57 | §9.1 | Round-robin tie-break at same priority | test_fn_dma_06 | FN-PRIO-02..03 | COVERED |
| F58 | §9.1 | Starvation prevention after 8 consecutive rounds | test_fn_dma_12 | FN-STARV-01..02 | COVERED |
| F59 | §9.2 | Dynamic priority: takes effect next arbitration | test_fn_dma_12 | FN-STARV-05..06 | COVERED |
| F60 | §10 | Global reset: channels IDLE, regs reset, INT_STATUS=0, INT_MASK=0xFFF, IRQs deasserted | test_fn_dma_02 | FN-CTRL-02..08 | COVERED |
| F61 | §11 | Target socket: valid→TLM_OK; undefined→ADDRESS_ERROR; wrong length→GENERIC_ERROR | test_ec_dma_01 | EC-ADDR-03..07 | COVERED |
| F62 | §11 | TLM_IGNORE_COMMAND → GENERIC_ERROR | test_ec_dma_01 | EC-ADDR-08 | COVERED |
| F63 | §11 | streaming_width must equal data_length or 0 | test_tlm_dma_01 | TLM-DMA-01..02 | COVERED |
| F64 | §11 | Write to RO→ignored TLM_OK; Read from WO→0 TLM_OK | test_ec_dma_01 | EC-ADDR-09..11 | COVERED |
| F65 | §12 | Initiator protocol: read then write per beat | test_fn_dma_04 | FN-XFER-01..07 | COVERED |
| F66 | §12 | Non-OK response → bus error, abort, ERROR, CURR at failed address | test_ec_dma_05 | EC-BUS-01..09 | COVERED |
| F67 | §13.1 | Corner: minimum transfer (SIZE=1, W=1B, BURST=1) | test_ec_dma_03 | EC-CORNER-01 | COVERED |
| F68 | §13.2 | Corner: maximum transfer (SIZE=65536) | test_fn_dma_15 | FN-ENG-01..03 | COVERED |
| F69 | §13.3 | Corner: SUSPENDED reprogramming REMAINING recalc | test_fn_dma_11 | FN-SUSP-03..09 | COVERED |
| F70 | §13.4 | Corner: pause during final burst → completion wins | test_ec_dma_03 | EC-CORNER-02 | COVERED |
| F71 | §13.5 | Corner: circular + linked → circular precedence | test_fn_dma_08 | FN-LINK-02 | COVERED |
| F72 | §13.6 | Corner: simultaneous START+ABORT → ERROR | test_ec_dma_03, test_ec_dma_04 | EC-CORNER-03, EC-SBIT-* | COVERED |
| F73 | §13.7 | Corner: decrement underflow wraps unsigned | test_fn_dma_16 | FN-ADEC-04..05 | COVERED |
| F74 | §13.8 | Corner: half-transfer with SIZE=1 | test_ec_dma_03 | EC-CORNER-05 | COVERED |
| F75 | §14 | Error handling: config error (no INT), bus error (INT), ABORT (INT) | test_ec_dma_02, test_ec_dma_05, test_fn_dma_09 | EC-VAL-*, EC-BUS-02, FN-PAUSE-06 | COVERED |
| F76 | §14 | Recovery: CLR_ERR→IDLE, CURR/REMAINING preserved | test_ec_dma_05 | EC-BUS-05..07 | COVERED |
| F77 | §15 | Global enable: ENABLE=0 blocks START, doesn't stop RUNNING, RESUME still works | test_fn_dma_17 | FN-GLEN-01..05 | COVERED |
| F78 | §16 | Post-reset register state verified | test_fn_dma_01 | FN-REG-01..19 | COVERED |

---

## 4. Coverage Summary

| Metric | Count |
|--------|-------|
| **Total Spec Features** | 78 |
| **Covered** | 77 |
| **Partially Covered** | 0 |
| **Not Tested** | 1 |
| **Total Assertions** | 233 |
| **Assertions Passed** | 233 |
| **Assertions Failed** | 0 |
| **Test Functions** | 27 |
| **Overall Feature Coverage** | **98.7%** |

### Coverage Gap

| Feature ID | Spec Section | Description | Reason |
|:---:|:---:|---|---|
| F50 | §7.2 | No hardware cycle detection | Spec explicitly states this is "software responsibility" - no hardware behavior to test. Informational only. |

---

## 5. Notes

- All 233 assertions pass on the golden implementation with zero failures.
- Simulation time: 35,390 ns; wall-clock time: ~71 ms.
- The testbench uses structured assertion IDs (e.g., `FN-REG-01`, `EC-ADDR-03`, `IR-DMA-05`) enabling fine-grained pass/fail tracking.
- Feature F50 is architecturally specified as not having hardware support - it is a documentation note, not an omission in the testbench.
- The testbench covers all 16 spec sections including registers (§3), FSM (§4), validation (§5), transfer execution (§6), special modes (§7), interrupts (§8), arbitration (§9), global reset (§10), TLM protocol (§11-§12), corner cases (§13), error handling (§14), global enable (§15), and post-reset state (§16).
