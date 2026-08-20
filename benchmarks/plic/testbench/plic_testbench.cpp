#include <systemc>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <chrono>

#include "plic.h"


static bool run_stress = false;

static int pass_count = 0;
static int fail_count = 0;
static int test_step = 0;
static int test_pass = 0;
static int test_fail = 0;
static const char* current_test_id = "";
static const char* current_test_name = "";

static void begin_test(const char* id, const char* name, const char* scenario = nullptr) {
    current_test_id = id;
    current_test_name = name;
    test_step = 0;
    test_pass = 0;
    test_fail = 0;
    std::cout << "\n┌─ TEST " << id << ": " << name << std::endl;
    if (scenario)
        std::cout << "│  Scenario: " << scenario << std::endl;
}

static void check(bool cond, const char* cp, const char* msg) {
    test_step++;
    if (cond) {
        std::cout << "│  [PASS] step " << test_step << " | " << cp << " | " << msg << std::endl;
        pass_count++;
        test_pass++;
    } else {
        std::cout << "│  [FAIL] step " << test_step << " | " << cp << " | " << msg << std::endl;
        fail_count++;
        test_fail++;
    }
}

static void log_step(const char* desc) { std::cout << "│  [STEP] " << desc << std::endl; }

static void end_test() {
    std::cout << "└─ " << current_test_id << ": "
              << test_pass << " passed, " << test_fail << " failed"
              << (test_fail == 0 ? " ✓" : " ✗") << "\n";
}

// Address offsets for S=8
static constexpr uint64_t PRIORITY_BASE = 0x000;
static constexpr uint64_t PENDING_OFF   = 0x020;
static constexpr uint64_t ENABLE_OFF    = 0x024;
static constexpr uint64_t THRESHOLD_OFF = 0x028;
static constexpr uint64_t CLAIM_OFF     = 0x02C;
static constexpr uint64_t COMPLETE_OFF  = 0x030;
static constexpr uint64_t INT_TYPE_OFF  = 0x034;

SC_MODULE(TestRunner) {
    tlm_utils::simple_initiator_socket<TestRunner> socket;

    sc_core::sc_signal<bool>* src_irq_sigs;
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS>* irq_out_sig;

    SC_HAS_PROCESS(TestRunner);

    TestRunner(sc_core::sc_module_name name)
        : sc_module(name), socket("socket"),
          src_irq_sigs(nullptr), irq_out_sig(nullptr)
    {
        SC_THREAD(run_tests);
    }

    // ── TLM transaction helper ──
    tlm::tlm_response_status do_txn(
        uint64_t addr, unsigned char* data, uint32_t len, tlm::tlm_command cmd)
    {
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        trans.set_command(cmd);
        trans.set_address(addr);
        trans.set_data_ptr(data);
        trans.set_data_length(len);
        trans.set_streaming_width(len);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_byte_enable_length(0);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        socket->b_transport(trans, delay);
        return trans.get_response_status();
    }

    void write32(uint64_t addr, uint32_t val) {
        do_txn(addr, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_WRITE_COMMAND);
    }
    uint32_t read32(uint64_t addr) {
        uint32_t val = 0;
        do_txn(addr, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_READ_COMMAND);
        return val;
    }
    tlm::tlm_response_status write32_s(uint64_t addr, uint32_t val) {
        return do_txn(addr, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_WRITE_COMMAND);
    }
    tlm::tlm_response_status read32_s(uint64_t addr) {
        uint32_t val = 0;
        return do_txn(addr, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_READ_COMMAND);
    }
    uint32_t read32_val_s(uint64_t addr, tlm::tlm_response_status& s) {
        uint32_t val = 0;
        s = do_txn(addr, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_READ_COMMAND);
        return val;
    }

    void tick() { wait(1, sc_core::SC_NS); }

    void set_irq(int idx, bool val) {
        src_irq_sigs[idx].write(val);
        tick();
    }

    // Reset all state between tests
    void reset_state() {
        write32(ENABLE_OFF, 0);
        write32(THRESHOLD_OFF, 0);
        write32(INT_TYPE_OFF, 0);
        for (int i = 0; i < 8; i++) {
            write32(PRIORITY_BASE + i*4, 0);
            src_irq_sigs[i].write(false);
        }
        tick();
        // Drain any claims
        for (int i = 0; i < 8; i++) write32(COMPLETE_OFF, i+1);
        tick();
    }

    // ════════════════════════════════════════════════════════════════
    // test_fn_plic_01: Register Reset & Access (CAT-1, 22 checks)
    // ════════════════════════════════════════════════════════════════
    void test_fn_plic_01() {
        begin_test("test_fn_plic_01", "Register Reset & Access",
                   "Verify all registers read correct reset values and honor field width masks");

        log_step("Check priority registers reset to zero");
        for (int i = 0; i < 8; i++)
            check(read32(PRIORITY_BASE + i*4) == 0, "FN-REG-01", ("PRIORITY["+std::to_string(i)+"]=0").c_str());

        log_step("Check pending register reset");
        check(read32(PENDING_OFF) == 0, "FN-REG-02", "PENDING=0 at reset");

        log_step("Check enable register reset");
        check(read32(ENABLE_OFF) == 0, "FN-REG-03", "ENABLE=0 at reset");

        log_step("Check threshold register reset");
        check(read32(THRESHOLD_OFF) == 0, "FN-REG-04", "THRESHOLD=0 at reset");

        log_step("Check claim register at reset");
        check(read32(CLAIM_OFF) == 0, "FN-REG-05", "First CLAIM read=0");
        check(read32(CLAIM_OFF) == 0, "FN-REG-05", "Second CLAIM read=0");
        check(read32(PENDING_OFF) == 0, "FN-REG-05", "PENDING still 0 after CLAIM reads");

        log_step("Check INT_TYPE register reset");
        check(read32(INT_TYPE_OFF) == 0, "FN-REG-06", "INT_TYPE=0 at reset");

        log_step("Check priority write/read back with masking");
        check(write32_s(PRIORITY_BASE, 0xFFFFFFFF) == tlm::TLM_OK_RESPONSE, "FN-REG-07", "Write OK");
        check(read32(PRIORITY_BASE) == 0x00000007, "FN-REG-07", "Read back 0x07");
        write32(PRIORITY_BASE, 0);

        log_step("Check threshold write/read back with masking");
        check(write32_s(THRESHOLD_OFF, 0xDEADBEEF) == tlm::TLM_OK_RESPONSE, "FN-REG-08", "Write OK");
        check(read32(THRESHOLD_OFF) == 0x00000007, "FN-REG-08", "Read back 0x07");
        write32(THRESHOLD_OFF, 0);

        log_step("Check pending register is read-only");
        set_irq(0, true);
        check(read32(PENDING_OFF) == 0x00000002, "FN-REG-09", "PENDING bit1 set");
        write32(PENDING_OFF, 0x00000000);
        check(read32(PENDING_OFF) == 0x00000002, "FN-REG-09", "Write to PENDING ignored");
        set_irq(0, false);

        log_step("Check complete register reads zero");
        check(read32(COMPLETE_OFF) == 0, "FN-REG-10", "Read COMPLETE returns 0");

        end_test();
    }

    // ════════════════════════════════════════════════════════════════
    // test_ec_plic_01: Address Decode & Error Responses (CAT-2, 8 checks)
    // ════════════════════════════════════════════════════════════════
    void test_ec_plic_01() {
        begin_test("test_ec_plic_01", "Address Decode & Error Responses",
                   "Verify valid addresses return OK and invalid/misaligned return errors");

        log_step("Valid address reads");
        check(read32_s(0x000) == tlm::TLM_OK_RESPONSE, "EC-ADDR-01", "Read 0x000 OK");
        check(read32_s(0x034) == tlm::TLM_OK_RESPONSE, "EC-ADDR-02", "Read 0x034 OK");

        log_step("Out-of-range address reads");
        check(read32_s(0x038) == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-ADDR-03", "Read 0x038 ADDRESS_ERROR");
        check(read32_s(0x100) == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-ADDR-04", "Read 0x100 ADDRESS_ERROR");

        log_step("Misaligned address access");
        {
            uint32_t val = 0;
            auto s = do_txn(0x001, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_READ_COMMAND);
            check(s == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-ADDR-05", "Misaligned 0x001 ADDRESS_ERROR");
        }
        {
            uint32_t val = 0;
            auto s = do_txn(0x003, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_READ_COMMAND);
            check(s == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-ADDR-06", "Misaligned 0x003 ADDRESS_ERROR");
        }

        log_step("Wrong data length and unsupported command");
        {
            uint32_t val = 0;
            auto s = do_txn(0x000, reinterpret_cast<unsigned char*>(&val), 2, tlm::TLM_READ_COMMAND);
            check(s == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-ADDR-07", "2-byte read GENERIC_ERROR");
        }
        {
            uint32_t val = 0;
            tlm::tlm_generic_payload trans;
            sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
            trans.set_command(tlm::TLM_IGNORE_COMMAND);
            trans.set_address(0x000);
            trans.set_data_ptr(reinterpret_cast<unsigned char*>(&val));
            trans.set_data_length(4);
            trans.set_streaming_width(4);
            trans.set_byte_enable_ptr(nullptr);
            trans.set_byte_enable_length(0);
            trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            socket->b_transport(trans, delay);
            check(trans.get_response_status() == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-ADDR-08", "IGNORE_COMMAND GENERIC_ERROR");
        }

        end_test();
    }

    // ════════════════════════════════════════════════════════════════
    // test_fn_plic_02: Priority & Hardwired Bits (CAT-3, 5 checks)
    // ════════════════════════════════════════════════════════════════
    void test_fn_plic_02() {
        begin_test("test_fn_plic_02", "Hardwired Zero Bits",
                   "Verify source-0 hardwired to zero and field masking in ENABLE/PENDING/INT_TYPE/PRIORITY");

        log_step("ENABLE bit masking");
        write32(ENABLE_OFF, 0xFFFFFFFF);
        check(read32(ENABLE_OFF) == 0x000001FE, "FN-PRIO-01", "ENABLE=0x1FE (bits[8:1], bit0=0)");
        write32(ENABLE_OFF, 0);

        log_step("PENDING bit masking");
        for (int i = 0; i < 8; i++) src_irq_sigs[i].write(true);
        tick();
        check(read32(PENDING_OFF) == 0x000001FE, "FN-PRIO-02", "PENDING=0x1FE (bit0 always 0)");
        for (int i = 0; i < 8; i++) src_irq_sigs[i].write(false);
        tick();

        log_step("INT_TYPE bit masking");
        write32(INT_TYPE_OFF, 0xFFFFFFFF);
        check(read32(INT_TYPE_OFF) == 0x000001FE, "FN-PRIO-03", "INT_TYPE=0x1FE (bit0=0, bits>8=0)");
        write32(INT_TYPE_OFF, 0);

        log_step("PRIORITY field width");
        write32(PRIORITY_BASE, 0x000000FF);
        check(read32(PRIORITY_BASE) == 0x00000007, "FN-PRIO-04", "PRIORITY 0xFF -> 0x07");
        write32(PRIORITY_BASE, 0);

        log_step("Bits beyond S hardwired to zero");
        write32(ENABLE_OFF, 0x00000600);
        check(read32(ENABLE_OFF) == 0x00000000, "FN-PRIO-05", "Bits 9,10 beyond S=8 hardwired 0");
        write32(ENABLE_OFF, 0);

        end_test();
    }

    // ════════════════════════════════════════════════════════════════
    // test_fn_plic_03: Pending / Level-Sensitive Detection (CAT-4, 13 checks)
    // ════════════════════════════════════════════════════════════════
    void test_fn_plic_03() {
        begin_test("test_fn_plic_03", "Level-Sensitive Interrupt Detection",
                   "Verify pending bits track level-sensitive source signals correctly");

        log_step("Single source assert/deassert");
        set_irq(0, true);
        check(read32(PENDING_OFF) == 0x00000002, "FN-PEND-01", "src_irq[0] -> PENDING bit1");
        set_irq(0, false);

        set_irq(0, true);
        set_irq(0, false);
        check(read32(PENDING_OFF) == 0x00000000, "FN-PEND-02", "Deassert clears pending");

        log_step("Multi-source pending");
        src_irq_sigs[0].write(true);
        src_irq_sigs[2].write(true);
        src_irq_sigs[4].write(true);
        tick();
        check(read32(PENDING_OFF) == 0x0000002A, "FN-PEND-03", "Sources 1,3,5 pending=0x2A");
        src_irq_sigs[0].write(false); src_irq_sigs[2].write(false); src_irq_sigs[4].write(false);
        tick();

        log_step("Assert/deassert/reassert cycle");
        set_irq(0, true);
        check(read32(PENDING_OFF) == 0x00000002, "FN-PEND-04", "Assert -> 0x02");
        set_irq(0, false);
        check(read32(PENDING_OFF) == 0x00000000, "FN-PEND-04", "Deassert -> 0x00");
        set_irq(0, true);
        check(read32(PENDING_OFF) == 0x00000002, "FN-PEND-04", "Reassert -> 0x02");
        set_irq(0, false);

        log_step("Level deassert before claim");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5);
        set_irq(0, true);
        set_irq(0, false);
        check(read32(PENDING_OFF) == 0x00000000, "FN-PEND-05", "PENDING=0 after deassert");
        check(read32(CLAIM_OFF) == 0x00000000, "FN-PEND-05", "CLAIM=0 (no pending)");
        write32(PRIORITY_BASE, 0); write32(ENABLE_OFF, 0);

        log_step("Claim while source asserted");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5);
        set_irq(0, true);
        uint32_t id = read32(CLAIM_OFF);
        check(id == 0x00000001, "FN-PEND-06", "CLAIM returns 1");
        check(read32(PENDING_OFF) == 0x00000000, "FN-PEND-06", "PENDING=0 after claim (CLAIMED state)");
        write32(COMPLETE_OFF, 1);
        set_irq(0, false);
        write32(PRIORITY_BASE, 0); write32(ENABLE_OFF, 0);

        log_step("Re-pend after complete with source still high");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5);
        set_irq(0, true);
        read32(CLAIM_OFF); // claim source 1
        write32(COMPLETE_OFF, 1);
        tick();
        check(read32(PENDING_OFF) == 0x00000002, "FN-PEND-07", "Re-pends after COMPLETE (src still high)");
        set_irq(0, false);
        write32(PRIORITY_BASE, 0); write32(ENABLE_OFF, 0);

        log_step("No re-pend when source deasserted before complete");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5);
        set_irq(0, true);
        read32(CLAIM_OFF);
        set_irq(0, false);
        write32(COMPLETE_OFF, 1);
        tick();
        check(read32(PENDING_OFF) == 0x00000000, "FN-PEND-08", "No re-pend (src deasserted before complete)");
        write32(PRIORITY_BASE, 0); write32(ENABLE_OFF, 0);

        end_test();
    }

    // ════════════════════════════════════════════════════════════════
    // test_fn_plic_04: Enable / Edge-Triggered Detection (CAT-5, 10 checks)
    // ════════════════════════════════════════════════════════════════
    void test_fn_plic_04() {
        begin_test("test_fn_plic_04", "Edge-Triggered Interrupt Detection",
                   "Verify edge-triggered sources latch pending on rising edge and clear on claim");

        log_step("Rising edge sets pending");
        write32(INT_TYPE_OFF, 0x002);
        set_irq(0, true);
        check(read32(PENDING_OFF) == 0x00000002, "FN-EN-01", "Rising edge sets pending");
        set_irq(0, false);
        reset_state();

        log_step("Pending latches after deassert");
        write32(INT_TYPE_OFF, 0x002);
        set_irq(0, true);
        set_irq(0, false);
        check(read32(PENDING_OFF) == 0x00000002, "FN-EN-02", "Pending latches after deassert");
        reset_state();

        log_step("Multiple edges produce single pending");
        write32(INT_TYPE_OFF, 0x002);
        set_irq(0, true);
        set_irq(0, false);
        set_irq(0, true);
        set_irq(0, false);
        check(read32(PENDING_OFF) == 0x00000002, "FN-EN-03", "Multiple edges, still single pending");
        reset_state();

        log_step("Edge claim/complete cycle");
        write32(INT_TYPE_OFF, 0x002);
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5);
        set_irq(0, true);
        set_irq(0, false);
        check(read32(PENDING_OFF) == 0x00000002, "FN-EN-04", "Pending latched after deassert");
        uint32_t id = read32(CLAIM_OFF);
        check(id == 1, "FN-EN-04", "CLAIM returns 1");
        check(read32(PENDING_OFF) == 0x00000000, "FN-EN-04", "Pending cleared by claim");
        write32(COMPLETE_OFF, 1);
        reset_state();

        log_step("Edge during CLAIMED is lost");
        write32(INT_TYPE_OFF, 0x002);
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5);
        set_irq(0, true);
        read32(CLAIM_OFF); // -> CLAIMED
        set_irq(0, false);
        set_irq(0, true); // edge during CLAIMED
        set_irq(0, false);
        write32(COMPLETE_OFF, 1);
        tick();
        check(read32(PENDING_OFF) == 0x00000000, "FN-EN-05", "Edge during CLAIMED lost");
        reset_state();

        log_step("New edge after COMPLETE detected");
        write32(INT_TYPE_OFF, 0x002);
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5);
        set_irq(0, true);
        read32(CLAIM_OFF);
        set_irq(0, false);
        write32(COMPLETE_OFF, 1);
        tick();
        set_irq(0, true); // new edge after COMPLETE
        check(read32(PENDING_OFF) == 0x00000002, "FN-EN-06", "New edge after COMPLETE detected");
        set_irq(0, false);
        reset_state();

        log_step("Initial high counts as rising edge");
        write32(INT_TYPE_OFF, 0x002);
        set_irq(0, true); // prev=false -> true = rising edge
        check(read32(PENDING_OFF) == 0x00000002, "FN-EN-07", "Initial high counts as rising edge");
        set_irq(0, false);
        reset_state();

        end_test();
    }

    // ════════════════════════════════════════════════════════════════
    // test_fn_plic_05: Threshold / Gateway State Machine (CAT-6, 14 checks)
    // ════════════════════════════════════════════════════════════════
    void test_fn_plic_05() {
        begin_test("test_fn_plic_05", "Gateway State Machine",
                   "Verify gateway transitions: IDLE->PENDING->CLAIMED->IDLE for level and edge sources");

        uint32_t id;

        log_step("Level gateway: IDLE->PENDING->CLAIMED->IDLE");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5);
        set_irq(0, true);
        check(read32(PENDING_OFF) == 0x00000002, "FN-THRESH-01", "PENDING state");
        id = read32(CLAIM_OFF);
        check(id == 1, "FN-THRESH-01", "CLAIM=1");
        check(read32(PENDING_OFF) == 0x00000000, "FN-THRESH-01", "CLAIMED: pending=0");
        set_irq(0, false);
        write32(COMPLETE_OFF, 1);
        tick();
        check(read32(PENDING_OFF) == 0x00000000, "FN-THRESH-01", "IDLE: pending=0");
        write32(PRIORITY_BASE, 0); write32(ENABLE_OFF, 0);

        log_step("Edge gateway: IDLE->PENDING->CLAIMED->IDLE");
        write32(INT_TYPE_OFF, 0x002);
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5);
        set_irq(0, true);
        check(read32(PENDING_OFF) == 0x00000002, "FN-THRESH-02", "PENDING state (edge)");
        id = read32(CLAIM_OFF);
        check(id == 1, "FN-THRESH-02", "CLAIM=1");
        check(read32(PENDING_OFF) == 0x00000000, "FN-THRESH-02", "CLAIMED: pending=0");
        write32(COMPLETE_OFF, 1);
        tick();
        check(read32(PENDING_OFF) == 0x00000000, "FN-THRESH-02", "IDLE: pending=0");
        set_irq(0, false);
        reset_state();

        log_step("Multi-source gateway: both claim and re-pend");
        write32(ENABLE_OFF, 0x006); write32(PRIORITY_BASE, 5); write32(PRIORITY_BASE+4, 4);
        set_irq(0, true); set_irq(1, true);
        uint32_t c1 = read32(CLAIM_OFF);
        check(c1 == 1, "FN-THRESH-03", "First CLAIM=1 (prio 5)");
        uint32_t c2 = read32(CLAIM_OFF);
        check(c2 == 2, "FN-THRESH-03", "Second CLAIM=2 (prio 4)");
        check(read32(PENDING_OFF) == 0x00000000, "FN-THRESH-03", "Both claimed, pending=0");
        write32(COMPLETE_OFF, 2); write32(COMPLETE_OFF, 1);
        tick();
        check(read32(PENDING_OFF) == 0x00000006, "FN-THRESH-03", "Both re-pend after complete");
        set_irq(0, false); set_irq(1, false);
        reset_state();

        log_step("Spurious and invalid completes");
        write32(COMPLETE_OFF, 1); // complete without prior claim
        check(read32(PENDING_OFF) == 0x00000000, "FN-THRESH-04", "No state change from spurious complete");

        auto s = write32_s(COMPLETE_OFF, 0);
        check(s == tlm::TLM_OK_RESPONSE, "FN-THRESH-05", "COMPLETE ID=0 silently OK");

        s = write32_s(COMPLETE_OFF, 9);
        check(s == tlm::TLM_OK_RESPONSE, "FN-THRESH-06", "COMPLETE ID=9 (>S) silently OK");

        end_test();
    }

    // ════════════════════════════════════════════════════════════════
    // test_fn_plic_06: Claim/Complete & Priority Arbitration (CAT-7, 14 checks)
    // ════════════════════════════════════════════════════════════════
    void test_fn_plic_06() {
        begin_test("test_fn_plic_06", "Priority Arbitration",
                   "Verify claim returns highest-priority enabled source with correct tie-breaking");

        log_step("Higher priority wins");
        write32(ENABLE_OFF, 0x006); write32(PRIORITY_BASE, 3); write32(PRIORITY_BASE+4, 5);
        set_irq(0, true); set_irq(1, true);
        check(read32(CLAIM_OFF) == 2, "FN-CLAIM-01", "Source 2 (prio 5) wins");
        write32(COMPLETE_OFF, 2);
        set_irq(0, false); set_irq(1, false);
        reset_state();

        log_step("Equal priority tie-break by ID");
        write32(ENABLE_OFF, 0x00A); // bits 1,3
        write32(PRIORITY_BASE, 4); write32(PRIORITY_BASE+8, 4);
        set_irq(0, true); set_irq(2, true);
        check(read32(CLAIM_OFF) == 1, "FN-CLAIM-02", "Source 1 wins tie-break (lower ID)");
        write32(COMPLETE_OFF, 1);
        set_irq(0, false); set_irq(2, false);
        reset_state();

        log_step("Priority zero disqualifies source");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 0);
        set_irq(0, true);
        check(read32(CLAIM_OFF) == 0, "FN-CLAIM-03", "Priority 0 -> CLAIM=0");
        check(irq_out_sig->read() == false, "FN-CLAIM-03", "irq_out=false");
        set_irq(0, false);
        reset_state();

        log_step("Threshold filtering");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 3); write32(THRESHOLD_OFF, 3);
        set_irq(0, true);
        check(read32(CLAIM_OFF) == 0, "FN-CLAIM-04", "Priority 3 NOT > threshold 3 -> CLAIM=0");
        check(irq_out_sig->read() == false, "FN-CLAIM-04", "irq_out=false");
        set_irq(0, false);
        reset_state();

        log_step("Priority > threshold qualifies");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 1); write32(THRESHOLD_OFF, 0);
        set_irq(0, true);
        check(irq_out_sig->read() == true, "FN-CLAIM-05", "irq_out=true (prio 1 > thresh 0)");
        check(read32(CLAIM_OFF) == 1, "FN-CLAIM-05", "CLAIM=1");
        write32(COMPLETE_OFF, 1);
        set_irq(0, false);
        reset_state();

        log_step("Max threshold blocks max priority");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 7); write32(THRESHOLD_OFF, 7);
        set_irq(0, true);
        check(read32(CLAIM_OFF) == 0, "FN-CLAIM-06", "Prio 7 NOT > thresh 7 -> CLAIM=0");
        check(irq_out_sig->read() == false, "FN-CLAIM-06", "irq_out=false");
        set_irq(0, false);
        reset_state();

        log_step("Dynamic priority change");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 3);
        set_irq(0, true);
        check(irq_out_sig->read() == true, "FN-CLAIM-07", "irq_out=true (5>3)");
        write32(PRIORITY_BASE, 2);
        tick();
        check(irq_out_sig->read() == false, "FN-CLAIM-07", "irq_out=false after prio change to 2 (2 NOT > 3)");
        set_irq(0, false);
        reset_state();

        log_step("Disabled source not arbitrated");
        write32(ENABLE_OFF, 0x000); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
        set_irq(0, true);
        check(read32(CLAIM_OFF) == 0, "FN-CLAIM-08", "Not enabled -> CLAIM=0");
        check(irq_out_sig->read() == false, "FN-CLAIM-08", "irq_out=false");
        set_irq(0, false);
        reset_state();

        end_test();
    }

    // ════════════════════════════════════════════════════════════════
    // test_ir_plic_01: IRQ output generation (CAT-8, 10 checks)
    // ════════════════════════════════════════════════════════════════
    void test_ir_plic_01() {
        begin_test("test_ir_plic_01", "IRQ Output Generation",
                   "Verify irq_out asserts/deasserts correctly based on pending, claim, threshold");

        log_step("Basic assertion");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
        set_irq(0, true);
        check(irq_out_sig->read() == true, "IR-PLIC-01", "irq_out asserts");
        set_irq(0, false);
        reset_state();

        log_step("Deasserts after claim");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
        set_irq(0, true);
        check(irq_out_sig->read() == true, "IR-PLIC-02", "irq_out=true before claim");
        read32(CLAIM_OFF);
        tick();
        check(irq_out_sig->read() == false, "IR-PLIC-02", "irq_out=false after claim");
        write32(COMPLETE_OFF, 1);
        set_irq(0, false);
        reset_state();

        log_step("Stays asserted with multiple sources");
        write32(ENABLE_OFF, 0x006); write32(PRIORITY_BASE, 5); write32(PRIORITY_BASE+4, 4);
        write32(THRESHOLD_OFF, 0);
        set_irq(0, true); set_irq(1, true);
        check(irq_out_sig->read() == true, "IR-PLIC-03", "irq_out=true (two sources)");
        read32(CLAIM_OFF); // claims source 1
        tick();
        check(irq_out_sig->read() == true, "IR-PLIC-03", "irq_out stays true (source 2 remains)");
        write32(COMPLETE_OFF, 1);
        read32(CLAIM_OFF); write32(COMPLETE_OFF, 2);
        set_irq(0, false); set_irq(1, false);
        reset_state();

        log_step("Threshold change deasserts/reasserts");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
        set_irq(0, true);
        check(irq_out_sig->read() == true, "IR-PLIC-04", "irq_out=true initially");
        write32(THRESHOLD_OFF, 6);
        tick();
        check(irq_out_sig->read() == false, "IR-PLIC-04", "irq_out=false after threshold=6");
        // Continuing: threshold=6, prio=5, src still high
        write32(THRESHOLD_OFF, 4);
        tick();
        check(irq_out_sig->read() == true, "IR-PLIC-05", "irq_out=true after threshold lowered to 4 (5>4)");
        set_irq(0, false);
        reset_state();

        log_step("Re-asserts after complete with level source still high");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
        set_irq(0, true);
        read32(CLAIM_OFF);
        tick();
        check(irq_out_sig->read() == false, "IR-PLIC-06", "irq_out=false after claim");
        write32(COMPLETE_OFF, 1);
        tick();
        check(irq_out_sig->read() == true, "IR-PLIC-06", "irq_out re-asserts after COMPLETE (level re-pends)");
        set_irq(0, false);
        reset_state();

        end_test();
    }

    // ════════════════════════════════════════════════════════════════
    // test_fn_plic_07: INT_TYPE Change Interactions (CAT-9, 8 checks)
    // ════════════════════════════════════════════════════════════════
    void test_fn_plic_07() {
        begin_test("test_fn_plic_07", "INT_TYPE Change Interactions",
                   "Verify behavior when INT_TYPE is changed between level and edge while sources are active");

        log_step("Edge to level: pending clears if input low");
        write32(INT_TYPE_OFF, 0x002); // source 1 edge
        set_irq(0, true);
        set_irq(0, false);
        check(read32(PENDING_OFF) == 0x00000002, "FN-TYPE-01", "Edge-latched pending");
        write32(INT_TYPE_OFF, 0x000); // switch to level
        tick();
        check(read32(PENDING_OFF) == 0x00000000, "FN-TYPE-01", "Level + input low = not pending");
        reset_state();

        log_step("Edge to level: pending remains if input high");
        write32(INT_TYPE_OFF, 0x002);
        set_irq(0, true); // edge -> pending, input stays high
        check(read32(PENDING_OFF) == 0x00000002, "FN-TYPE-02", "Edge pending set");
        write32(INT_TYPE_OFF, 0x000); // switch to level, input still high
        tick();
        check(read32(PENDING_OFF) == 0x00000002, "FN-TYPE-02", "Level + input high = still pending");
        set_irq(0, false);
        reset_state();

        log_step("Level to edge: pending retained");
        write32(INT_TYPE_OFF, 0x000); // level
        set_irq(0, true);
        check(read32(PENDING_OFF) == 0x00000002, "FN-TYPE-03", "Level pending");
        write32(INT_TYPE_OFF, 0x002); // switch to edge
        tick();
        check(read32(PENDING_OFF) == 0x00000002, "FN-TYPE-03", "Edge retains pending");
        set_irq(0, false);
        reset_state();

        log_step("Type change during CLAIMED state");
        write32(INT_TYPE_OFF, 0x000); // level
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5);
        set_irq(0, true);
        read32(CLAIM_OFF); // -> CLAIMED
        write32(INT_TYPE_OFF, 0x002); // change to edge while claimed
        write32(COMPLETE_OFF, 1);
        tick();
        check(read32(PENDING_OFF) == 0x00000000, "FN-TYPE-04", "No pending (edge needs new 0->1)");
        set_irq(0, false);
        reset_state();

        end_test();
    }

    // ════════════════════════════════════════════════════════════════
    // test_fn_plic_08: Multi-source / Claim-Complete Protocol (CAT-10, 12 checks)
    // ════════════════════════════════════════════════════════════════
    void test_fn_plic_08() {
        begin_test("test_fn_plic_08", "Claim/Complete Protocol Edge Cases",
                   "Verify successive claims, re-claim after complete, disable during claimed, etc.");

        log_step("Successive claims exhaust pending sources");
        write32(ENABLE_OFF, 0x006); write32(PRIORITY_BASE, 5); write32(PRIORITY_BASE+4, 4);
        write32(THRESHOLD_OFF, 0);
        set_irq(0, true); set_irq(1, true);
        check(read32(CLAIM_OFF) == 1, "FN-MULTI-01", "First CLAIM=1");
        check(read32(CLAIM_OFF) == 2, "FN-MULTI-02", "Second CLAIM=2");
        check(read32(CLAIM_OFF) == 0, "FN-MULTI-03", "Third CLAIM=0 (none left)");
        write32(COMPLETE_OFF, 1); write32(COMPLETE_OFF, 2);
        set_irq(0, false); set_irq(1, false);
        reset_state();

        log_step("Re-claim after complete with level source high");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
        set_irq(0, true);
        check(read32(CLAIM_OFF) == 1, "FN-MULTI-04", "First CLAIM=1");
        write32(COMPLETE_OFF, 1);
        tick();
        check(read32(CLAIM_OFF) == 1, "FN-MULTI-05", "Re-CLAIM=1 after complete");
        write32(COMPLETE_OFF, 1);
        set_irq(0, false);
        reset_state();

        log_step("Disable during CLAIMED state");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
        set_irq(0, true);
        read32(CLAIM_OFF); // claim
        write32(ENABLE_OFF, 0x000); // disable while claimed
        write32(COMPLETE_OFF, 1);
        tick();
        check(read32(PENDING_OFF) == 0x00000002, "FN-MULTI-06", "Re-pends (level+high)");
        check(irq_out_sig->read() == false, "FN-MULTI-06", "irq_out=false (disabled)");
        set_irq(0, false);
        reset_state();

        log_step("Priority change during CLAIMED state");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
        set_irq(0, true);
        read32(CLAIM_OFF);
        write32(PRIORITY_BASE, 0); // prio=0 while claimed
        write32(COMPLETE_OFF, 1);
        tick();
        check(read32(PENDING_OFF) == 0x00000002, "FN-MULTI-07", "Re-pends (level+high)");
        check(irq_out_sig->read() == false, "FN-MULTI-07", "irq_out=false (prio 0)");
        set_irq(0, false);
        reset_state();

        log_step("Double claim returns 0 on second");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
        set_irq(0, true);
        check(read32(CLAIM_OFF) == 1, "FN-MULTI-08", "First CLAIM=1");
        check(read32(CLAIM_OFF) == 0, "FN-MULTI-09", "Second CLAIM=0 (already claimed)");
        write32(COMPLETE_OFF, 1);
        set_irq(0, false);
        reset_state();

        log_step("Second complete on PENDING source is ignored");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
        set_irq(0, true);
        read32(CLAIM_OFF);
        write32(COMPLETE_OFF, 1); // -> IDLE, re-pends (level+high)
        tick();
        write32(COMPLETE_OFF, 1); // second complete: source is PENDING not CLAIMED, ignored
        tick();
        check(read32(PENDING_OFF) == 0x00000002, "FN-MULTI-10", "PENDING=0x02 (second complete ignored)");
        set_irq(0, false);
        reset_state();

        end_test();
    }

    // ════════════════════════════════════════════════════════════════
    // test_ec_plic_02: Edge Cases / Gap Coverage (CAT-11, 29 checks)
    // ════════════════════════════════════════════════════════════════
    void test_ec_plic_02() {
        begin_test("test_ec_plic_02", "Edge Cases & Gap Coverage",
                   "Cover TLM command errors, error precedence, hardwired bits, dynamic re-evaluation, and edge-triggered corner cases");

        log_step("TLM_IGNORE_COMMAND returns GENERIC_ERROR");
        {
            uint32_t val = 0;
            tlm::tlm_generic_payload trans;
            sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
            trans.set_command(tlm::TLM_IGNORE_COMMAND);
            trans.set_address(PRIORITY_BASE + 4);
            trans.set_data_ptr(reinterpret_cast<unsigned char*>(&val));
            trans.set_data_length(4);
            trans.set_streaming_width(4);
            trans.set_byte_enable_ptr(nullptr);
            trans.set_byte_enable_length(0);
            trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            socket->b_transport(trans, delay);
            check(trans.get_response_status() == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-EDGE-01", "TLM_IGNORE_COMMAND returns GENERIC_ERROR");
        }

        log_step("Error precedence: address error over wrong size");
        {
            uint16_t val = 0;
            auto s = do_txn(0xFFF000, reinterpret_cast<unsigned char*>(&val), 2, tlm::TLM_READ_COMMAND);
            check(s == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-EDGE-02", "Out-of-range+wrong-size -> ADDRESS_ERROR (precedence)");
        }

        log_step("Source 0 (bit 0) hardwired to zero in all registers");
        {
            write32(ENABLE_OFF, 0xFFFFFFFF);
            check((read32(ENABLE_OFF) & 1) == 0, "EC-EDGE-03", "ENABLE bit 0 hardwired to 0");
            for (int i = 0; i < 8; i++) src_irq_sigs[i].write(true);
            tick();
            check((read32(PENDING_OFF) & 1) == 0, "EC-EDGE-03", "PENDING bit 0 hardwired to 0");
            for (int i = 0; i < 8; i++) src_irq_sigs[i].write(false);
            tick();
            write32(INT_TYPE_OFF, 0xFFFFFFFF);
            check((read32(INT_TYPE_OFF) & 1) == 0, "EC-EDGE-03", "INT_TYPE bit 0 hardwired to 0");
            reset_state();
        }

        log_step("irq_out re-evaluation on ENABLE write");
        {
            reset_state();
            write32(PRIORITY_BASE, 5);
            write32(THRESHOLD_OFF, 0);
            write32(ENABLE_OFF, 0x000); // disabled
            set_irq(0, true); // source 1 pending
            check(irq_out_sig->read() == false, "EC-EDGE-04", "irq_out=false (source disabled)");
            write32(ENABLE_OFF, 0x002);
            wait(sc_core::SC_ZERO_TIME);
            check(irq_out_sig->read() == true, "EC-EDGE-04", "irq_out=true immediately after ENABLE write");
            set_irq(0, false);
            reset_state();
        }

        log_step("irq_out re-evaluation on threshold write (boundary)");
        {
            reset_state();
            write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 4);
            set_irq(0, true);
            check(irq_out_sig->read() == true, "EC-EDGE-05", "irq_out=true (prio 5 > thresh 4)");
            write32(THRESHOLD_OFF, 5);
            wait(sc_core::SC_ZERO_TIME);
            check(irq_out_sig->read() == false, "EC-EDGE-05", "irq_out=false after threshold=5 (5 NOT > 5)");
            set_irq(0, false);
            reset_state();
        }

        log_step("irq_out re-evaluation on INT_TYPE write");
        {
            reset_state();
            write32(INT_TYPE_OFF, 0x002);
            write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
            set_irq(0, true);
            set_irq(0, false); // pending stays (edge-latched)
            check(read32(PENDING_OFF) == 0x00000002, "EC-EDGE-06", "Edge-latched pending (input now low)");
            check(irq_out_sig->read() == true, "EC-EDGE-06", "irq_out=true (edge source qualified)");
            write32(INT_TYPE_OFF, 0x000);
            wait(sc_core::SC_ZERO_TIME);
            check(read32(PENDING_OFF) == 0x00000000, "EC-EDGE-06", "Pending clears after INT_TYPE edge→level (input low)");
            check(irq_out_sig->read() == false, "EC-EDGE-06", "irq_out=false after INT_TYPE change clears pending");
            reset_state();
        }

        log_step("Claim with nothing pending returns 0");
        {
            reset_state();
            check(read32(CLAIM_OFF) == 0, "EC-EDGE-07", "Claim with nothing pending returns 0");
            write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
            check(read32(CLAIM_OFF) == 0, "EC-EDGE-07", "Claim with enabled+prioritized but no pending returns 0");
            reset_state();
        }

        log_step("Complete of unclaimed source silently ignored");
        {
            reset_state();
            write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
            auto s = write32_s(COMPLETE_OFF, 1);
            check(s == tlm::TLM_OK_RESPONSE, "EC-EDGE-08", "Complete of unclaimed source returns TLM_OK");
            check(read32(PENDING_OFF) == 0x00000000, "EC-EDGE-08", "No state change after completing unclaimed source");
            check(irq_out_sig->read() == false, "EC-EDGE-08", "irq_out still false after completing unclaimed source");
            reset_state();
        }

        log_step("Multiple sources same priority tie-break and irq_out persistence");
        {
            reset_state();
            write32(PRIORITY_BASE + 2*4, 7);
            write32(PRIORITY_BASE + 4*4, 7);
            write32(ENABLE_OFF, 0x28);       // enable bits 3 and 5
            write32(THRESHOLD_OFF, 0);
            set_irq(2, true);
            set_irq(4, true);
            check(irq_out_sig->read() == true, "EC-EDGE-09", "irq_out=true (two sources qualified)");
            uint32_t id = read32(CLAIM_OFF);
            check(id == 3, "EC-EDGE-09", "Claim returns 3 (lowest ID tie-break)");
            check(irq_out_sig->read() == true, "EC-EDGE-09", "irq_out STAYS true after claim (source 5 still qualified)");
            write32(COMPLETE_OFF, 3);
            write32(COMPLETE_OFF, 5);
            set_irq(2, false); set_irq(4, false);
            reset_state();
        }

        log_step("Misalignment returns ADDRESS_ERROR");
        {
            uint32_t val = 0;
            auto s1 = do_txn(0x002, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_READ_COMMAND);
            check(s1 == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-EDGE-10", "Misaligned 0x002 read -> ADDRESS_ERROR");
            auto s2 = do_txn(0x005, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_WRITE_COMMAND);
            check(s2 == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-EDGE-10", "Misaligned 0x005 write -> ADDRESS_ERROR");
            auto s3 = do_txn(0x001001, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_READ_COMMAND);
            check(s3 == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-EDGE-10", "Misaligned 0x001001 read -> ADDRESS_ERROR");
        }

        log_step("b_transport updates irq_out before return");
        {
            reset_state();
            write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
            set_irq(0, true);
            check(irq_out_sig->read() == true, "EC-EDGE-11", "irq_out=true before claim");
            read32(CLAIM_OFF);
            wait(sc_core::SC_ZERO_TIME);
            check(irq_out_sig->read() == false, "EC-EDGE-11", "irq_out=false immediately after claim (no tick)");
            write32(COMPLETE_OFF, 1);
            wait(sc_core::SC_ZERO_TIME);
            check(irq_out_sig->read() == true, "EC-EDGE-11", "irq_out=true immediately after complete (no tick)");
            set_irq(0, false);
            reset_state();
        }

        log_step("Edge-triggered: edge during CLAIMED is lost");
        {
            reset_state();
            write32(INT_TYPE_OFF, 0x002);
            write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
            set_irq(0, true);
            check(read32(PENDING_OFF) == 0x00000002, "EC-EDGE-12", "Pending set after rising edge");
            read32(CLAIM_OFF);
            check(read32(PENDING_OFF) == 0x00000000, "EC-EDGE-12", "Pending cleared after claim");
            set_irq(0, false);
            set_irq(0, true);
            set_irq(0, false);
            write32(COMPLETE_OFF, 1);
            tick();
            check(read32(PENDING_OFF) == 0x00000000, "EC-EDGE-12", "Pending=0 after complete (edge during CLAIMED lost)");
            check(irq_out_sig->read() == false, "EC-EDGE-12", "irq_out=false (no pending after lost edge)");
            reset_state();
        }

        end_test();
    }

    // ════════════════════════════════════════════════════════════════
    // test_ec_plic_03: Close PARTIAL coverage gaps (F07, F23, F49, F50, F60)
    // ════════════════════════════════════════════════════════════════
    void test_ec_plic_03() {
        begin_test("test_ec_plic_03", "PARTIAL Coverage Gap Closure",
                   "Close gaps: F07 source-0 PRIORITY, F23 static-high edge, F49 irq_out hold, F50 reset irq_out, F60 INT_TYPE change while CLAIMED");

        // F50: irq_out=false at reset (explicit check before any stimulus)
        // Note: by this point we've done reset_state(), which returns to reset-like conditions
        log_step("F50: irq_out=false after full reset_state");
        reset_state();
        check(irq_out_sig->read() == false, "EC-GAP-01", "irq_out=false after reset (F50)");

        // F07: Source 0 is reserved — bit 0 of ENABLE/PENDING/INT_TYPE hardwired 0,
        // and source 0 can never qualify for claim even if all sources triggered
        log_step("F07: Source 0 never qualifies — CLAIM never returns 0-as-source");
        write32(ENABLE_OFF, 0x1FE); // enable all sources 1-8
        for (int i = 0; i < 8; i++) write32(PRIORITY_BASE + i*4, 7);
        write32(THRESHOLD_OFF, 0);
        for (int i = 0; i < 8; i++) src_irq_sigs[i].write(true);
        tick();
        // Bit 0 of PENDING must be 0 (source 0 reserved)
        check((read32(PENDING_OFF) & 1) == 0, "EC-GAP-02", "PENDING bit0=0 (source 0 reserved, F07)");
        // Claim all sources, none should ever return ID=0 as a valid source
        for (int i = 0; i < 8; i++) {
            uint32_t id = read32(CLAIM_OFF);
            check(id != 0 || i >= 8, "EC-GAP-02", "CLAIM never returns source 0");
            if (id == 0) break;
            write32(COMPLETE_OFF, id);
        }
        for (int i = 0; i < 8; i++) src_irq_sigs[i].write(false);
        reset_state();

        // F49: irq_out level persistence — stays high across multiple ticks
        log_step("F49: irq_out persists high over multiple cycles");
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
        set_irq(0, true);
        check(irq_out_sig->read() == true, "EC-GAP-03", "irq_out=true initially");
        tick(); tick(); tick();
        check(irq_out_sig->read() == true, "EC-GAP-03", "irq_out stays true after 3 additional ticks (F49)");
        set_irq(0, false);
        reset_state();

        // F23: Static high does not trigger edge after complete (needs 0→1 transition)
        log_step("F23: Edge-triggered static high after complete does NOT re-pend");
        write32(INT_TYPE_OFF, 0x002);
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
        set_irq(0, true); // rising edge -> pending
        check(read32(PENDING_OFF) == 0x00000002, "EC-GAP-04", "Initial edge sets pending");
        read32(CLAIM_OFF); // claim
        write32(COMPLETE_OFF, 1); // complete; input is STILL high
        tick();
        // Source stays high — no 0→1 transition occurred, so no new pending
        check(read32(PENDING_OFF) == 0x00000000, "EC-GAP-04", "Static high after complete: no re-pend (F23)");
        check(irq_out_sig->read() == false, "EC-GAP-04", "irq_out=false (no pending from static high)");
        // Now verify a real 0→1 transition does work (re-arm)
        set_irq(0, false);
        set_irq(0, true);
        check(read32(PENDING_OFF) == 0x00000002, "EC-GAP-04", "New 0->1 edge after complete re-pends (re-arm works)");
        set_irq(0, false);
        reset_state();

        // F60: INT_TYPE change while CLAIMED — deferred type change isolated
        log_step("F60: INT_TYPE edge->level while CLAIMED, complete honors new type");
        write32(INT_TYPE_OFF, 0x002); // edge
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
        set_irq(0, true); // rising edge -> pending
        read32(CLAIM_OFF); // -> CLAIMED
        // Change to level while CLAIMED, input still high
        write32(INT_TYPE_OFF, 0x000);
        tick();
        // Complete: now level mode, input is high → should re-pend
        write32(COMPLETE_OFF, 1);
        tick();
        check(read32(PENDING_OFF) == 0x00000002, "EC-GAP-05", "After complete, level mode re-pends (input high) (F60)");
        check(irq_out_sig->read() == true, "EC-GAP-05", "irq_out=true after complete with level+input high");
        set_irq(0, false);
        reset_state();

        // F60 variant: INT_TYPE level->edge while CLAIMED, input high, complete should NOT re-pend
        log_step("F60: INT_TYPE level->edge while CLAIMED, complete does NOT re-pend (no new edge)");
        write32(INT_TYPE_OFF, 0x000); // level
        write32(ENABLE_OFF, 0x002); write32(PRIORITY_BASE, 5); write32(THRESHOLD_OFF, 0);
        set_irq(0, true); // level pending
        read32(CLAIM_OFF); // -> CLAIMED
        // Change to edge while CLAIMED
        write32(INT_TYPE_OFF, 0x002);
        tick();
        write32(COMPLETE_OFF, 1);
        tick();
        // Edge mode: input was already high, no 0→1 transition → no pending
        check(read32(PENDING_OFF) == 0x00000000, "EC-GAP-06", "After complete, edge mode: no re-pend (static high, no new edge) (F60)");
        check(irq_out_sig->read() == false, "EC-GAP-06", "irq_out=false (edge mode, no pending)");
        set_irq(0, false);
        reset_state();

        end_test();
    }

    // ════════════════════════════════════════════════════════════════

    void run_tests() {
        tick(); // initial settle

        std::cout << "═══════════════════════════════════════════" << std::endl;
        std::cout << "       PLIC Testbench — Starting" << std::endl;
        std::cout << "═══════════════════════════════════════════" << std::endl;

        test_fn_plic_01();
        reset_state();

        test_ec_plic_01();
        reset_state();

        test_fn_plic_02();
        reset_state();

        test_fn_plic_03();
        reset_state();

        test_fn_plic_04();
        reset_state();

        test_fn_plic_05();
        reset_state();

        test_fn_plic_06();
        reset_state();

        test_ir_plic_01();
        reset_state();

        test_fn_plic_07();
        reset_state();

        test_fn_plic_08();
        reset_state();

        test_ec_plic_02();
        reset_state();

        test_ec_plic_03();

        // Zero-delay transport verification (F63)
        {
            sc_core::sc_time t_before = sc_core::sc_time_stamp();
            write32(PRIORITY_BASE + 4, 5);
            sc_core::sc_time t_after_w = sc_core::sc_time_stamp();
            check(t_before == t_after_w, "EC-ZDEL-01", "b_transport write consumes zero simulation time");
            read32(PENDING_OFF);
            sc_core::sc_time t_after_r = sc_core::sc_time_stamp();
            check(t_after_w == t_after_r, "EC-ZDEL-02", "b_transport read consumes zero simulation time");
            read32(CLAIM_OFF);
            sc_core::sc_time t_after_c = sc_core::sc_time_stamp();
            check(t_after_r == t_after_c, "EC-ZDEL-03", "CLAIM read consumes zero simulation time");
        }

        if (run_stress) test_stress_arbitration();

        std::cout << "\n═══════════════════════════════════════════" << std::endl;
        std::cout << "       PLIC Testbench — Summary" << std::endl;
        std::cout << "═══════════════════════════════════════════" << std::endl;
        std::cout << "\nFINAL: " << pass_count << " passed, " << fail_count << " failed, " << (pass_count+fail_count) << " total\n";
        std::cout << "RESULT: " << (fail_count == 0 ? "ALL PASS ✓" : "FAILURES DETECTED ✗") << "\n";

        sc_core::sc_stop();
    }

    void test_stress_arbitration() {
        write32(THRESHOLD_OFF, 0);
        write32(ENABLE_OFF, 0x1FE);
        for (int i = 0; i < 8; i++) {
            write32(PRIORITY_BASE + i * 4, i + 1);
            src_irq_sigs[i].write(true);
        }
        wait(1, sc_core::SC_NS);

        auto t_start = std::chrono::high_resolution_clock::now();
        int txn = 0;

        for (int iter = 0; iter < 10000; iter++) {
            write32(PRIORITY_BASE + (iter % 8) * 4, 8);
            write32(PRIORITY_BASE + ((iter + 4) % 8) * 4, 1);
            txn += 2;
            read32(CLAIM_OFF);
            txn++;
            write32(COMPLETE_OFF, (iter % 8) + 1);
            txn++;
        }

        auto t_end = std::chrono::high_resolution_clock::now();
        double wall_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        double throughput = txn / (wall_ms / 1000.0);
        std::cout << "\n[STRESS] benchmark=plic scenario=priority_rearbitration transactions=" << txn
                  << " wall_time_ms=" << wall_ms << " throughput_txn_per_s=" << throughput << std::endl;

        for (int i = 0; i < 8; i++) src_irq_sigs[i].write(false);
        wait(1, sc_core::SC_NS);
    }
};

#include <chrono>
int sc_main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--stress") run_stress = true;
    }

    plic dut("plic", 8);

    sc_core::sc_signal<bool> src_irq_sigs[8];
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS> irq_out_sig;

    for (int i = 0; i < 8; i++)
        dut.src_irq[i](src_irq_sigs[i]);
    dut.irq_out(irq_out_sig);

    TestRunner runner("runner");
    runner.socket.bind(dut.socket);
    runner.src_irq_sigs = src_irq_sigs;
    runner.irq_out_sig = &irq_out_sig;

    auto wall_start = std::chrono::high_resolution_clock::now();
    sc_core::sc_start();
    auto wall_end = std::chrono::high_resolution_clock::now();
    double wall_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();
    std::cout << "\n[PERF] sim_time=" << sc_core::sc_time_stamp() << " wall_time=" << wall_ms << "ms" << std::endl;

    return (fail_count == 0) ? 0 : 1;
}
