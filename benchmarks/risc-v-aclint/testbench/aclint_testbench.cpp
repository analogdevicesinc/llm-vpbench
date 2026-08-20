#include <systemc>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <cstdio>
#include <chrono>

#include "aclint_mswi.h"
#include "aclint_mtimer.h"
#include "aclint_sswi.h"

static constexpr int NUM_HARTS = 8;

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
    if (scenario) std::cout << "│  Scenario: " << scenario << std::endl;
}

static void log_step(const char* desc) {
    std::cout << "│  [STEP] " << desc << std::endl;
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

static void end_test() {
    std::cout << "└─ " << current_test_id << ": "
              << test_pass << " passed, " << test_fail << " failed"
              << (test_fail == 0 ? " ✓" : " ✗") << "\n";
}

SC_MODULE(TestRunner) {
    tlm_utils::simple_initiator_socket<TestRunner> mswi_socket;
    tlm_utils::simple_initiator_socket<TestRunner> mtimer_socket;
    tlm_utils::simple_initiator_socket<TestRunner> sswi_socket;

    sc_core::sc_signal<bool, sc_core::SC_UNCHECKED_WRITERS>* sw_irq;
    sc_core::sc_signal<bool, sc_core::SC_UNCHECKED_WRITERS>* timer_irq;
    sc_core::sc_signal<bool, sc_core::SC_UNCHECKED_WRITERS>* ssw_irq;

    SC_HAS_PROCESS(TestRunner);

    TestRunner(sc_core::sc_module_name name)
        : sc_module(name)
        , mswi_socket("mswi_socket")
        , mtimer_socket("mtimer_socket")
        , sswi_socket("sswi_socket")
        , sw_irq(nullptr)
        , timer_irq(nullptr)
        , ssw_irq(nullptr)
    {
        SC_THREAD(run_tests);
    }

    // ── Generic TLM transaction helper ──────────────────────────────
    tlm::tlm_response_status do_txn(
        tlm_utils::simple_initiator_socket<TestRunner>& sock,
        uint64_t addr, unsigned char* data, uint32_t len, tlm::tlm_command cmd,
        unsigned char* byte_en = nullptr, unsigned int be_len = 0)
    {
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        trans.set_command(cmd);
        trans.set_address(addr);
        trans.set_data_ptr(data);
        trans.set_data_length(len);
        trans.set_streaming_width(len);
        trans.set_byte_enable_ptr(byte_en);
        trans.set_byte_enable_length(be_len);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sock->b_transport(trans, delay);
        return trans.get_response_status();
    }

    // ── MSWI helpers ────────────────────────────────────────────────
    void write32_mswi(uint64_t addr, uint32_t val) {
        do_txn(mswi_socket, addr, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_WRITE_COMMAND);
    }
    uint32_t read32_mswi(uint64_t addr) {
        uint32_t val = 0;
        do_txn(mswi_socket, addr, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_READ_COMMAND);
        return val;
    }
    tlm::tlm_response_status write32_mswi_status(uint64_t addr, uint32_t val) {
        return do_txn(mswi_socket, addr, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_WRITE_COMMAND);
    }
    tlm::tlm_response_status read32_mswi_status(uint64_t addr) {
        uint32_t val = 0;
        return do_txn(mswi_socket, addr, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_READ_COMMAND);
    }
    tlm::tlm_response_status do_mswi_txn_len(uint64_t addr, uint32_t len) {
        uint32_t val = 0;
        return do_txn(mswi_socket, addr, reinterpret_cast<unsigned char*>(&val), len, tlm::TLM_READ_COMMAND);
    }
    tlm::tlm_response_status do_mswi_txn_be(uint64_t addr, uint32_t val,
                                              unsigned char* be, unsigned int be_len) {
        return do_txn(mswi_socket, addr, reinterpret_cast<unsigned char*>(&val), 4,
                      tlm::TLM_WRITE_COMMAND, be, be_len);
    }

    // ── MTIMER helpers ──────────────────────────────────────────────
    void write32_mtimer(uint64_t addr, uint32_t val) {
        do_txn(mtimer_socket, addr, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_WRITE_COMMAND);
    }
    uint32_t read32_mtimer(uint64_t addr) {
        uint32_t val = 0;
        do_txn(mtimer_socket, addr, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_READ_COMMAND);
        return val;
    }
    void write64_mtimer(uint64_t addr, uint64_t val) {
        do_txn(mtimer_socket, addr, reinterpret_cast<unsigned char*>(&val), 8, tlm::TLM_WRITE_COMMAND);
    }
    uint64_t read64_mtimer(uint64_t addr) {
        uint64_t val = 0;
        do_txn(mtimer_socket, addr, reinterpret_cast<unsigned char*>(&val), 8, tlm::TLM_READ_COMMAND);
        return val;
    }
    tlm::tlm_response_status write32_mtimer_status(uint64_t addr, uint32_t val) {
        return do_txn(mtimer_socket, addr, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_WRITE_COMMAND);
    }
    tlm::tlm_response_status read32_mtimer_status(uint64_t addr) {
        uint32_t val = 0;
        return do_txn(mtimer_socket, addr, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_READ_COMMAND);
    }
    tlm::tlm_response_status do_mtimer_txn_len(uint64_t addr, uint32_t len) {
        uint64_t val = 0;
        return do_txn(mtimer_socket, addr, reinterpret_cast<unsigned char*>(&val), len, tlm::TLM_READ_COMMAND);
    }
    tlm::tlm_response_status do_mtimer_txn_be(uint64_t addr, uint32_t val,
                                               unsigned char* be, unsigned int be_len) {
        return do_txn(mtimer_socket, addr, reinterpret_cast<unsigned char*>(&val), 4,
                      tlm::TLM_WRITE_COMMAND, be, be_len);
    }
    tlm::tlm_response_status do_mtimer_txn_len_write(uint64_t addr, uint32_t len) {
        uint64_t val = 0;
        return do_txn(mtimer_socket, addr, reinterpret_cast<unsigned char*>(&val), len, tlm::TLM_WRITE_COMMAND);
    }
    tlm::tlm_response_status write64_mtimer_status(uint64_t addr, uint64_t val) {
        return do_txn(mtimer_socket, addr, reinterpret_cast<unsigned char*>(&val), 8, tlm::TLM_WRITE_COMMAND);
    }

    // ── SSWI helpers ────────────────────────────────────────────────
    void write32_sswi(uint64_t addr, uint32_t val) {
        do_txn(sswi_socket, addr, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_WRITE_COMMAND);
    }
    uint32_t read32_sswi(uint64_t addr) {
        uint32_t val = 0;
        do_txn(sswi_socket, addr, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_READ_COMMAND);
        return val;
    }
    tlm::tlm_response_status write32_sswi_status(uint64_t addr, uint32_t val) {
        return do_txn(sswi_socket, addr, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_WRITE_COMMAND);
    }
    tlm::tlm_response_status read32_sswi_status(uint64_t addr) {
        uint32_t val = 0;
        return do_txn(sswi_socket, addr, reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_READ_COMMAND);
    }
    tlm::tlm_response_status do_sswi_txn_len(uint64_t addr, uint32_t len) {
        uint32_t val = 0;
        return do_txn(sswi_socket, addr, reinterpret_cast<unsigned char*>(&val), len, tlm::TLM_READ_COMMAND);
    }
    tlm::tlm_response_status do_sswi_txn_be(uint64_t addr, uint32_t val,
                                              unsigned char* be, unsigned int be_len) {
        return do_txn(sswi_socket, addr, reinterpret_cast<unsigned char*>(&val), 4,
                      tlm::TLM_WRITE_COMMAND, be, be_len);
    }

    // ── Cleanup helpers ─────────────────────────────────────────────
    void cleanup_mswi() {
        for (int i = 0; i < NUM_HARTS; i++)
            write32_mswi(i * 4, 0);
    }
    void cleanup_mtimer() {
        write64_mtimer(0x7FF8, 0);
        for (int i = 0; i < NUM_HARTS; i++)
            write64_mtimer(i * 8, 0xFFFFFFFFFFFFFFFFULL);
        write32_mtimer(0x7FF0, 1);   // prescaler = 1
        write32_mtimer(0x7FE8, 1);   // timer_enable = 1
        read32_mtimer(0x7FE0);       // clear overflow_flag
    }
    void cleanup_sswi() {
        for (int i = 0; i < NUM_HARTS; i++)
            write32_sswi(i * 4, 0);
    }

    // ── Test declarations ───────────────────────────────────────────
    void run_tests();

    // MSWI
    void test_fn_mswi_01();
    void test_fn_mswi_02();
    void test_fn_mswi_03();
    void test_fn_mswi_04();
    void test_ir_mswi_05();
    void test_ir_mswi_06();
    void test_ir_mswi_07();
    void test_fn_mswi_08();
    void test_fn_mswi_09();
    void test_fn_mswi_10();
    void test_ec_mswi_11();
    void test_ec_mswi_12();
    void test_fn_mswi_13();

    // MTIMER (original)
    void test_fn_mtimer_01();
    void test_fn_mtimer_02();
    void test_ir_mtimer_03();
    void test_fn_mtimer_04();
    void test_fn_mtimer_05();
    void test_fn_mtimer_06();
    void test_fn_mtimer_07();
    void test_fn_mtimer_08();
    void test_fn_mtimer_09();
    void test_fn_mtimer_10();
    void test_fn_mtimer_11();
    void test_ir_mtimer_12();
    void test_ir_mtimer_13();
    void test_ir_mtimer_14();
    void test_ir_mtimer_15();
    void test_ir_mtimer_16();
    void test_fn_mtimer_17();
    void test_fn_mtimer_18();
    void test_fn_mtimer_19();
    void test_fn_mtimer_20();
    void test_ec_mtimer_21();
    void test_ec_mtimer_22();
    void test_ec_mtimer_23();
    void test_fn_mtimer_24();
    void test_fn_mtimer_25();
    void test_fn_mtimer_26();
    void test_ir_mtimer_27();
    // MTIMER new features
    void test_fn_mtimer_28_prescaler_default();
    void test_fn_mtimer_29_prescaler_rw();
    void test_fn_mtimer_30_prescaler_warl();
    void test_fn_mtimer_31_prescaler_effect();
    void test_fn_mtimer_32_timer_enable_default();
    void test_fn_mtimer_33_timer_disable();
    void test_fn_mtimer_34_timer_reenable();
    void test_fn_mtimer_35_timer_enable_warl();
    void test_fn_mtimer_36_overflow_default();
    void test_fn_mtimer_37_overflow_detect();
    void test_fn_mtimer_38_overflow_read_clear();
    void test_fn_mtimer_39_overflow_write_ignored();
    void test_fn_mtimer_40_split_high_harts();
    void test_ec_mtimer_41_gap_addresses();

    // SSWI
    void test_fn_sswi_01();
    void test_fn_sswi_02();
    void test_fn_sswi_03();
    void test_fn_sswi_04();
    void test_ir_sswi_05();
    void test_ir_sswi_06();
    void test_ir_sswi_07();
    void test_fn_sswi_08();
    void test_fn_sswi_09();
    void test_fn_sswi_10();
    void test_ec_sswi_11();
    void test_ec_sswi_12();
    void test_fn_sswi_13();

    // TLM-2.0 Advanced
    void test_tlm_mswi_dmi();
    void test_tlm_mtimer_dmi();
    void test_tlm_sswi_dmi();
    void test_tlm_mswi_dbg();
    void test_tlm_mtimer_dbg();
    void test_tlm_sswi_dbg();
    void test_tlm_mswi_byte_enable();
    void test_tlm_mtimer_byte_enable();
    void test_tlm_sswi_byte_enable();

    // Cross-device
    void test_xdev_01();
    void test_xdev_02();
    void test_xdev_03();
    void test_xdev_04();

    // Extended: Timer-Enable + IRQ Interaction
    void test_fn_mtimer_42_irq_while_disabled();
    void test_fn_mtimer_43_irq_on_mtime_write_disabled();
    void test_fn_mtimer_44_disable_write_reenable();
    void test_fn_mtimer_45_disable_reenable_preserves();
    void test_fn_mtimer_46_irq_deassert_mtimecmp_disabled();

    // Extended: Overflow Flag Edge Cases
    void test_fn_mtimer_47_multi_overflow_single_flag();
    void test_fn_mtimer_48_overflow_write_ok_response();
    void test_fn_mtimer_49_overflow_write_no_change();
    void test_fn_mtimer_50_no_overflow_on_sw_wrap();

    // Extended: Control Register Width Enforcement
    void test_ec_mtimer_51_prescaler_8byte_read();
    void test_ec_mtimer_52_timer_enable_8byte_read();
    void test_ec_mtimer_53_overflow_8byte_read();
    void test_ec_mtimer_54_prescaler_8byte_write();

    // Extended: Address Space Boundary
    void test_ec_mtimer_55_boundary_0x8000();
    void test_ec_mtimer_56_8byte_at_0x7FFC();
    void test_ec_mswi_14_boundary_0x4000();
    void test_ec_sswi_14_boundary_0x4000();
    void test_ec_mswi_15_upper_reserved();

    // Extended: Prescaler Timing & Edge Cases
    void test_fn_mtimer_57_prescaler_65536_clamp();
    void test_fn_mtimer_58_prescaler_mid_operation();
    void test_fn_mtimer_59_prescaler1_tick_count();
    void test_fn_mtimer_60_prescaler_change_while_disabled();

    // Extended: Split Access Edge Cases
    void test_fn_mtimer_61_mtime_split_lo_preserves_hi();
    void test_fn_mtimer_62_mtime_split_hi_preserves_lo();
    void test_fn_mtimer_63_mtimecmp_last_hart_split();

    // Extended: IRQ Precision
    void test_ir_mtimer_64_exact_equality();
    void test_ir_mtimer_65_irq_reassert_mtimecmp_lowered();
    void test_ir_mtimer_66_per_hart_irq_independence();
    void test_ir_mtimer_67_overflow_irq();

    // Extended: Error Handling Edge Cases
    void test_ec_mtimer_68_gap_writes();
    void test_ec_mtimer_69_8byte_unaligned();
    void test_ec_mswi_16_write_beyond_harts();

    // ── NEW: Gap Coverage Tests ────────────────────────────────────
    // TLM_IGNORE_COMMAND
    void test_tlm_mswi_ignore_cmd();
    void test_tlm_mtimer_ignore_cmd();
    void test_tlm_sswi_ignore_cmd();
    // Error Priority
    void test_ec_error_priority();
    void test_ec_error_priority_sw();
    // Extension register 8-byte writes
    void test_ec_mtimer_ext_8byte_writes();
    // Timer-enable 0→1 multi-hart re-evaluation
    void test_fn_mtimer_70_reenable_all_irq();
    // Little-endian byte ordering
    void test_tlm_le_byte_order();
    // SSWI upper reserved area
    void test_ec_sswi_15_upper_reserved();
    // MTIME wrap with IRQ
    void test_fn_mtimer_71_mtime_wrap_irq();
    // Prescaler boundary values
    void test_fn_mtimer_72_prescaler_boundaries();
    // Overflow no spurious
    void test_fn_mtimer_73_overflow_no_spurious();
    // Safe update no spurious IRQ
    void test_fn_mtimer_74_safe_update_no_spurious();
    // Zero data_length
    void test_ec_zero_data_length();
    // Streaming width
    void test_tlm_streaming_width();
    // Level-sensitive persistence
    void test_fn_level_sensitive_persistence();
    // Response status always set
    void test_tlm_response_status_set();
    // Shared MTIME across harts
    void test_fn_mtimer_75_shared_mtime();
    // DMI coherency
    void test_tlm_dmi_coherency();


    // ── NEW: Helper for custom streaming_width ─────────────────────
    tlm::tlm_response_status do_txn_sw(
        tlm_utils::simple_initiator_socket<TestRunner>& sock,
        uint64_t addr, unsigned char* data, uint32_t len, tlm::tlm_command cmd,
        unsigned int sw)
    {
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        trans.set_command(cmd);
        trans.set_address(addr);
        trans.set_data_ptr(data);
        trans.set_data_length(len);
        trans.set_streaming_width(sw);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_byte_enable_length(0);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sock->b_transport(trans, delay);
        return trans.get_response_status();
    }
};

// ═══════════════════════════════════════════════════════════════════
// run_tests
// ═══════════════════════════════════════════════════════════════════
void TestRunner::run_tests() {
    wait(sc_core::SC_ZERO_TIME);

    std::cout << "===== MSWI Tests =====" << std::endl;
    begin_test("test_fn_mswi_01", "MSWI reset values");
    test_fn_mswi_01();
    end_test();
    begin_test("test_fn_mswi_02", "MSWI write/read");
    test_fn_mswi_02();
    end_test();
    begin_test("test_fn_mswi_03", "MSWI WARL hart 0");
    test_fn_mswi_03();
    end_test();
    begin_test("test_fn_mswi_04", "MSWI WARL all harts");
    test_fn_mswi_04();
    end_test();
    begin_test("test_ir_mswi_05", "MSWI sw_irq assert/deassert");
    test_ir_mswi_05();
    end_test();
    begin_test("test_ir_mswi_06", "MSWI cross-hart isolation");
    test_ir_mswi_06();
    end_test();
    begin_test("test_ir_mswi_07", "MSWI simultaneous all harts");
    test_ir_mswi_07();
    end_test();
    begin_test("test_fn_mswi_08", "MSWI rapid toggle");
    test_fn_mswi_08();
    end_test();
    begin_test("test_fn_mswi_09", "MSWI redundant zero write");
    test_fn_mswi_09();
    end_test();
    begin_test("test_fn_mswi_10", "MSWI back-to-back R/W");
    test_fn_mswi_10();
    end_test();
    begin_test("test_ec_mswi_11", "MSWI address errors");
    test_ec_mswi_11();
    end_test();
    begin_test("test_ec_mswi_12", "MSWI length errors");
    test_ec_mswi_12();
    end_test();
    begin_test("test_fn_mswi_13", "MSWI WARL patterns");
    test_fn_mswi_13();
    end_test();

    std::cout << "\n===== MTIMER Tests =====" << std::endl;
    begin_test("test_fn_mtimer_01", "MTIMER mtimecmp reset");
    test_fn_mtimer_01();
    end_test();
    begin_test("test_fn_mtimer_02", "MTIMER mtime reset");
    test_fn_mtimer_02();
    end_test();
    begin_test("test_ir_mtimer_03", "MTIMER IRQ initial state");
    test_ir_mtimer_03();
    end_test();
    begin_test("test_fn_mtimer_04", "fn_mtimer_04");
    test_fn_mtimer_04();
    end_test();
    begin_test("test_fn_mtimer_05", "fn_mtimer_05");
    test_fn_mtimer_05();
    end_test();
    begin_test("test_fn_mtimer_06", "fn_mtimer_06");
    test_fn_mtimer_06();
    end_test();
    begin_test("test_fn_mtimer_07", "fn_mtimer_07");
    test_fn_mtimer_07();
    end_test();
    begin_test("test_fn_mtimer_08", "fn_mtimer_08");
    test_fn_mtimer_08();
    end_test();
    begin_test("test_fn_mtimer_09", "fn_mtimer_09");
    test_fn_mtimer_09();
    end_test();
    begin_test("test_fn_mtimer_10", "fn_mtimer_10");
    test_fn_mtimer_10();
    end_test();
    begin_test("test_fn_mtimer_11", "fn_mtimer_11");
    test_fn_mtimer_11();
    end_test();
    begin_test("test_ir_mtimer_12", "ir_mtimer_12");
    test_ir_mtimer_12();
    end_test();
    begin_test("test_ir_mtimer_13", "ir_mtimer_13");
    test_ir_mtimer_13();
    end_test();
    begin_test("test_ir_mtimer_14", "ir_mtimer_14");
    test_ir_mtimer_14();
    end_test();
    begin_test("test_ir_mtimer_15", "ir_mtimer_15");
    test_ir_mtimer_15();
    end_test();
    begin_test("test_ir_mtimer_16", "ir_mtimer_16");
    test_ir_mtimer_16();
    end_test();
    begin_test("test_fn_mtimer_17", "fn_mtimer_17");
    test_fn_mtimer_17();
    end_test();
    begin_test("test_fn_mtimer_18", "fn_mtimer_18");
    test_fn_mtimer_18();
    end_test();
    begin_test("test_fn_mtimer_19", "fn_mtimer_19");
    test_fn_mtimer_19();
    end_test();
    begin_test("test_fn_mtimer_20", "fn_mtimer_20");
    test_fn_mtimer_20();
    end_test();
    begin_test("test_ec_mtimer_21", "ec_mtimer_21");
    test_ec_mtimer_21();
    end_test();
    begin_test("test_ec_mtimer_22", "ec_mtimer_22");
    test_ec_mtimer_22();
    end_test();
    begin_test("test_ec_mtimer_23", "ec_mtimer_23");
    test_ec_mtimer_23();
    end_test();
    begin_test("test_fn_mtimer_24", "fn_mtimer_24");
    test_fn_mtimer_24();
    end_test();
    begin_test("test_fn_mtimer_25", "fn_mtimer_25");
    test_fn_mtimer_25();
    end_test();
    begin_test("test_fn_mtimer_26", "fn_mtimer_26");
    test_fn_mtimer_26();
    end_test();
    begin_test("test_ir_mtimer_27", "ir_mtimer_27");
    test_ir_mtimer_27();
    end_test();

    std::cout << "\n===== MTIMER New Feature Tests =====" << std::endl;
    begin_test("test_fn_mtimer_28", "prescaler default");
    test_fn_mtimer_28_prescaler_default();
    end_test();
    begin_test("test_fn_mtimer_29", "prescaler rw");
    test_fn_mtimer_29_prescaler_rw();
    end_test();
    begin_test("test_fn_mtimer_30", "prescaler warl");
    test_fn_mtimer_30_prescaler_warl();
    end_test();
    begin_test("test_fn_mtimer_31", "prescaler effect");
    test_fn_mtimer_31_prescaler_effect();
    end_test();
    begin_test("test_fn_mtimer_32", "timer enable default");
    test_fn_mtimer_32_timer_enable_default();
    end_test();
    begin_test("test_fn_mtimer_33", "timer disable");
    test_fn_mtimer_33_timer_disable();
    end_test();
    begin_test("test_fn_mtimer_34", "timer reenable");
    test_fn_mtimer_34_timer_reenable();
    end_test();
    begin_test("test_fn_mtimer_35", "timer enable warl");
    test_fn_mtimer_35_timer_enable_warl();
    end_test();
    begin_test("test_fn_mtimer_36", "overflow default");
    test_fn_mtimer_36_overflow_default();
    end_test();
    begin_test("test_fn_mtimer_37", "overflow detect");
    test_fn_mtimer_37_overflow_detect();
    end_test();
    begin_test("test_fn_mtimer_38", "overflow read clear");
    test_fn_mtimer_38_overflow_read_clear();
    end_test();
    begin_test("test_fn_mtimer_39", "overflow write ignored");
    test_fn_mtimer_39_overflow_write_ignored();
    end_test();
    begin_test("test_fn_mtimer_40", "split high harts");
    test_fn_mtimer_40_split_high_harts();
    end_test();
    begin_test("test_ec_mtimer_41", "gap addresses");
    test_ec_mtimer_41_gap_addresses();
    end_test();

    std::cout << "\n===== SSWI Tests =====" << std::endl;
    begin_test("test_fn_sswi_01", "SSWI reset values");
    test_fn_sswi_01();
    end_test();
    begin_test("test_fn_sswi_02", "SSWI write/read");
    test_fn_sswi_02();
    end_test();
    begin_test("test_fn_sswi_03", "SSWI WARL hart 0");
    test_fn_sswi_03();
    end_test();
    begin_test("test_fn_sswi_04", "SSWI WARL all harts");
    test_fn_sswi_04();
    end_test();
    begin_test("test_ir_sswi_05", "SSWI irq assert/deassert");
    test_ir_sswi_05();
    end_test();
    begin_test("test_ir_sswi_06", "SSWI cross-hart isolation");
    test_ir_sswi_06();
    end_test();
    begin_test("test_ir_sswi_07", "SSWI simultaneous all harts");
    test_ir_sswi_07();
    end_test();
    begin_test("test_fn_sswi_08", "SSWI rapid toggle");
    test_fn_sswi_08();
    end_test();
    begin_test("test_fn_sswi_09", "SSWI redundant zero write");
    test_fn_sswi_09();
    end_test();
    begin_test("test_fn_sswi_10", "SSWI back-to-back R/W");
    test_fn_sswi_10();
    end_test();
    begin_test("test_ec_sswi_11", "SSWI address errors");
    test_ec_sswi_11();
    end_test();
    begin_test("test_ec_sswi_12", "SSWI length errors");
    test_ec_sswi_12();
    end_test();
    begin_test("test_fn_sswi_13", "SSWI WARL patterns");
    test_fn_sswi_13();
    end_test();

    std::cout << "\n===== TLM-2.0 Advanced Tests =====" << std::endl;
    begin_test("test_tlm_mswi_dmi", "MSWI DMI");
    test_tlm_mswi_dmi();
    end_test();
    begin_test("test_tlm_mtimer_dmi", "MTIMER DMI");
    test_tlm_mtimer_dmi();
    end_test();
    begin_test("test_tlm_sswi_dmi", "SSWI DMI");
    test_tlm_sswi_dmi();
    end_test();
    begin_test("test_tlm_mswi_dbg", "MSWI debug transport");
    test_tlm_mswi_dbg();
    end_test();
    begin_test("test_tlm_mtimer_dbg", "MTIMER debug transport");
    test_tlm_mtimer_dbg();
    end_test();
    begin_test("test_tlm_sswi_dbg", "SSWI debug transport");
    test_tlm_sswi_dbg();
    end_test();
    begin_test("test_tlm_mswi_byte_enable", "MSWI byte enable");
    test_tlm_mswi_byte_enable();
    end_test();
    begin_test("test_tlm_mtimer_byte_enable", "MTIMER byte enable");
    test_tlm_mtimer_byte_enable();
    end_test();
    begin_test("test_tlm_sswi_byte_enable", "SSWI byte enable");
    test_tlm_sswi_byte_enable();
    end_test();

    std::cout << "\n===== Cross-Device Tests =====" << std::endl;
    begin_test("test_xdev_01", "cross-device 01");
    test_xdev_01();
    end_test();
    begin_test("test_xdev_02", "cross-device 02");
    test_xdev_02();
    end_test();
    begin_test("test_xdev_03", "cross-device 03");
    test_xdev_03();
    end_test();
    begin_test("test_xdev_04", "cross-device 04");
    test_xdev_04();
    end_test();

    std::cout << "\n===== Extended: Timer-Enable + IRQ =====" << std::endl;
    begin_test("test_fn_mtimer_42", "IRQ while disabled");
    test_fn_mtimer_42_irq_while_disabled();
    end_test();
    begin_test("test_fn_mtimer_43", "IRQ on mtime write disabled");
    test_fn_mtimer_43_irq_on_mtime_write_disabled();
    end_test();
    begin_test("test_fn_mtimer_44", "disable write reenable");
    test_fn_mtimer_44_disable_write_reenable();
    end_test();
    begin_test("test_fn_mtimer_45", "disable reenable preserves");
    test_fn_mtimer_45_disable_reenable_preserves();
    end_test();
    begin_test("test_fn_mtimer_46", "IRQ deassert mtimecmp disabled");
    test_fn_mtimer_46_irq_deassert_mtimecmp_disabled();
    end_test();

    std::cout << "\n===== Extended: Overflow Edge Cases =====" << std::endl;
    begin_test("test_fn_mtimer_47", "multi overflow single flag");
    test_fn_mtimer_47_multi_overflow_single_flag();
    end_test();
    begin_test("test_fn_mtimer_48", "overflow write ok response");
    test_fn_mtimer_48_overflow_write_ok_response();
    end_test();
    begin_test("test_fn_mtimer_49", "overflow write no change");
    test_fn_mtimer_49_overflow_write_no_change();
    end_test();
    begin_test("test_fn_mtimer_50", "no overflow on sw wrap");
    test_fn_mtimer_50_no_overflow_on_sw_wrap();
    end_test();

    std::cout << "\n===== Extended: Control Reg Width =====" << std::endl;
    begin_test("test_ec_mtimer_51", "prescaler 8byte read");
    test_ec_mtimer_51_prescaler_8byte_read();
    end_test();
    begin_test("test_ec_mtimer_52", "timer enable 8byte read");
    test_ec_mtimer_52_timer_enable_8byte_read();
    end_test();
    begin_test("test_ec_mtimer_53", "overflow 8byte read");
    test_ec_mtimer_53_overflow_8byte_read();
    end_test();
    begin_test("test_ec_mtimer_54", "prescaler 8byte write");
    test_ec_mtimer_54_prescaler_8byte_write();
    end_test();

    std::cout << "\n===== Extended: Address Boundary =====" << std::endl;
    begin_test("test_ec_mtimer_55", "boundary 0x8000");
    test_ec_mtimer_55_boundary_0x8000();
    end_test();
    begin_test("test_ec_mtimer_56", "8byte at 0x7FFC");
    test_ec_mtimer_56_8byte_at_0x7FFC();
    end_test();
    begin_test("test_ec_mswi_14", "MSWI boundary 0x4000");
    test_ec_mswi_14_boundary_0x4000();
    end_test();
    begin_test("test_ec_sswi_14", "SSWI boundary 0x4000");
    test_ec_sswi_14_boundary_0x4000();
    end_test();
    begin_test("test_ec_mswi_15", "MSWI upper reserved");
    test_ec_mswi_15_upper_reserved();
    end_test();

    std::cout << "\n===== Extended: Prescaler Timing =====" << std::endl;
    begin_test("test_fn_mtimer_57", "prescaler 65536 clamp");
    test_fn_mtimer_57_prescaler_65536_clamp();
    end_test();
    begin_test("test_fn_mtimer_58", "prescaler mid operation");
    test_fn_mtimer_58_prescaler_mid_operation();
    end_test();
    begin_test("test_fn_mtimer_59", "prescaler1 tick count");
    test_fn_mtimer_59_prescaler1_tick_count();
    end_test();
    begin_test("test_fn_mtimer_60", "prescaler change while disabled");
    test_fn_mtimer_60_prescaler_change_while_disabled();
    end_test();

    std::cout << "\n===== Extended: Split Access =====" << std::endl;
    begin_test("test_fn_mtimer_61", "mtime split lo preserves hi");
    test_fn_mtimer_61_mtime_split_lo_preserves_hi();
    end_test();
    begin_test("test_fn_mtimer_62", "mtime split hi preserves lo");
    test_fn_mtimer_62_mtime_split_hi_preserves_lo();
    end_test();
    begin_test("test_fn_mtimer_63", "mtimecmp last hart split");
    test_fn_mtimer_63_mtimecmp_last_hart_split();
    end_test();

    std::cout << "\n===== Extended: IRQ Precision =====" << std::endl;
    begin_test("test_ir_mtimer_64", "exact equality");
    test_ir_mtimer_64_exact_equality();
    end_test();
    begin_test("test_ir_mtimer_65", "IRQ reassert mtimecmp lowered");
    test_ir_mtimer_65_irq_reassert_mtimecmp_lowered();
    end_test();
    begin_test("test_ir_mtimer_66", "per hart IRQ independence");
    test_ir_mtimer_66_per_hart_irq_independence();
    end_test();
    begin_test("test_ir_mtimer_67", "overflow IRQ");
    test_ir_mtimer_67_overflow_irq();
    end_test();

    std::cout << "\n===== Extended: Error Handling =====" << std::endl;
    begin_test("test_ec_mtimer_68", "gap writes");
    test_ec_mtimer_68_gap_writes();
    end_test();
    begin_test("test_ec_mtimer_69", "8byte unaligned");
    test_ec_mtimer_69_8byte_unaligned();
    end_test();
    begin_test("test_ec_mswi_16", "write beyond harts");
    test_ec_mswi_16_write_beyond_harts();
    end_test();

    std::cout << "\n===== NEW: TLM_IGNORE_COMMAND =====" << std::endl;
    begin_test("test_tlm_mswi_ignore_cmd", "MSWI ignore cmd");
    test_tlm_mswi_ignore_cmd();
    end_test();
    begin_test("test_tlm_mtimer_ignore_cmd", "MTIMER ignore cmd");
    test_tlm_mtimer_ignore_cmd();
    end_test();
    begin_test("test_tlm_sswi_ignore_cmd", "SSWI ignore cmd");
    test_tlm_sswi_ignore_cmd();
    end_test();

    std::cout << "\n===== NEW: Error Priority =====" << std::endl;
    begin_test("test_ec_error_priority", "error priority");
    test_ec_error_priority();
    end_test();

    std::cout << "\n===== NEW: Error Priority (streaming_width overlap) =====" << std::endl;
    begin_test("test_ec_error_priority_sw", "error priority streaming_width");
    test_ec_error_priority_sw();
    end_test();

    std::cout << "\n===== NEW: Extension Reg 8-Byte Writes =====" << std::endl;
    begin_test("test_ec_mtimer_ext_8byte_writes", "ext reg 8byte writes");
    test_ec_mtimer_ext_8byte_writes();
    end_test();

    std::cout << "\n===== NEW: Timer-Enable Multi-Hart =====" << std::endl;
    begin_test("test_fn_mtimer_70", "reenable all IRQ");
    test_fn_mtimer_70_reenable_all_irq();
    end_test();

    std::cout << "\n===== NEW: Little-Endian Byte Order =====" << std::endl;
    begin_test("test_tlm_le_byte_order", "LE byte order");
    test_tlm_le_byte_order();
    end_test();

    std::cout << "\n===== NEW: SSWI Upper Reserved =====" << std::endl;
    begin_test("test_ec_sswi_15", "SSWI upper reserved");
    test_ec_sswi_15_upper_reserved();
    end_test();

    std::cout << "\n===== NEW: MTIME Wrap IRQ =====" << std::endl;
    begin_test("test_fn_mtimer_71", "mtime wrap IRQ");
    test_fn_mtimer_71_mtime_wrap_irq();
    end_test();

    std::cout << "\n===== NEW: Prescaler Boundaries =====" << std::endl;
    begin_test("test_fn_mtimer_72", "prescaler boundaries");
    test_fn_mtimer_72_prescaler_boundaries();
    end_test();

    std::cout << "\n===== NEW: Overflow No Spurious =====" << std::endl;
    begin_test("test_fn_mtimer_73", "overflow no spurious");
    test_fn_mtimer_73_overflow_no_spurious();
    end_test();

    std::cout << "\n===== NEW: Safe Update No Spurious =====" << std::endl;
    begin_test("test_fn_mtimer_74", "safe update no spurious");
    test_fn_mtimer_74_safe_update_no_spurious();
    end_test();

    std::cout << "\n===== NEW: Zero Data Length =====" << std::endl;
    begin_test("test_ec_zero_data_length", "zero data length");
    test_ec_zero_data_length();
    end_test();

    std::cout << "\n===== NEW: Streaming Width =====" << std::endl;
    begin_test("test_tlm_streaming_width", "streaming width");
    test_tlm_streaming_width();
    end_test();

    std::cout << "\n===== NEW: Level-Sensitive Persistence =====" << std::endl;
    begin_test("test_fn_level_sensitive", "level-sensitive persistence");
    test_fn_level_sensitive_persistence();
    end_test();

    std::cout << "\n===== NEW: Response Status Set =====" << std::endl;
    begin_test("test_tlm_response_status", "response status set");
    test_tlm_response_status_set();
    end_test();

    std::cout << "\n===== NEW: Shared MTIME =====" << std::endl;
    begin_test("test_fn_mtimer_75", "shared mtime");
    test_fn_mtimer_75_shared_mtime();
    end_test();

    std::cout << "\n===== NEW: DMI Coherency =====" << std::endl;
    begin_test("test_tlm_dmi_coherency", "DMI coherency");
    test_tlm_dmi_coherency();
    end_test();


    if (run_stress) {
        cleanup_mtimer();
        cleanup_mswi();

        auto t_start = std::chrono::high_resolution_clock::now();
        int txn = 0;

        for (int i = 0; i < 20000; i++) {
            uint32_t val = (i % 2 == 0) ? 0 : 0xFFFFFFFF;
            write32_mtimer(0, val);
            txn++;
            read32_mtimer(0x7FF8);
            txn++;
        }

        for (int i = 0; i < 10000; i++) {
            write32_mswi(0x0, 1);
            txn++;
            write32_mswi(0x0, 0);
            txn++;
        }

        auto t_end = std::chrono::high_resolution_clock::now();
        double wall_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        double throughput = txn / (wall_ms / 1000.0);
        std::cout << "\n[STRESS] benchmark=risc-v-aclint scenario=timer_comparison transactions=" << txn
                  << " wall_time_ms=" << wall_ms << " throughput_txn_per_s=" << throughput << std::endl;
    }

    std::cout << "\n══════════════════════════════════════════" << std::endl;
    std::cout << "  FINAL: " << pass_count << " passed, " << fail_count << " failed, "
              << (pass_count + fail_count) << " total" << std::endl;
    std::cout << "  RESULT: " << (fail_count == 0 ? "ALL PASS ✓" : "FAILURES DETECTED ✗") << std::endl;
    std::cout << "══════════════════════════════════════════\n" << std::endl;

    sc_core::sc_stop();
}

// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_fn_mswi_01() {
    char tag[64];
    for (int i = 0; i < NUM_HARTS; i++) {
        uint32_t v = read32_mswi(i * 4);
        snprintf(tag, sizeof(tag), "FN-MSWI-01-%02d", i + 1);
        check(v == 0x00000000, tag, "msip reset value == 0");
        check(v == 0x00000000, "CP-2.3-MSWI", "msip reset value == 0");
    }
    for (int i = 0; i < NUM_HARTS; i++) {
        snprintf(tag, sizeof(tag), "FN-MSWI-01-%02d", i + NUM_HARTS + 1);
        check(sw_irq[i].read() == false, tag, "sw_irq deasserted at reset");
        check(sw_irq[i].read() == false, "CP-2.3-MSWI", "sw_irq deasserted at reset");
    }
}

void TestRunner::test_fn_mswi_02() {
    char tag[64];
    int a = 1;
    for (int i = 0; i < NUM_HARTS; i++) {
        write32_mswi(i * 4, 1);
        uint32_t v = read32_mswi(i * 4);
        snprintf(tag, sizeof(tag), "FN-MSWI-02-%02d", a++);
        check(v == 1, tag, "write 1 read back 1");
        check(v == 1, "CP-2.1-MSWI", "write 1 read back 1");

        write32_mswi(i * 4, 0);
        v = read32_mswi(i * 4);
        snprintf(tag, sizeof(tag), "FN-MSWI-02-%02d", a++);
        check(v == 0, tag, "write 0 read back 0");
        check(v == 0, "CP-2.1-MSWI", "write 0 read back 0");
    }
    cleanup_mswi();
}

void TestRunner::test_fn_mswi_03() {
    char tag[64];
    struct { uint32_t wval; uint32_t expect; } patterns[] = {
        {0xFFFFFFFF, 1}, {0xFFFFFFFE, 0}, {0x80000001, 1},
        {0x00000002, 0}, {0x7FFFFFFF, 1}, {0x80000000, 0}
    };
    for (int i = 0; i < 6; i++) {
        write32_mswi(0x0000, patterns[i].wval);
        uint32_t v = read32_mswi(0x0000);
        snprintf(tag, sizeof(tag), "FN-MSWI-03-%02d", i + 1);
        check(v == patterns[i].expect, tag, "WARL enforcement on hart 0");
        check(v == patterns[i].expect, "CP-2.2-MSWI", "WARL enforcement on hart 0");
    }
    cleanup_mswi();
}

void TestRunner::test_fn_mswi_04() {
    char tag[64];
    for (int i = 0; i < NUM_HARTS; i++) {
        write32_mswi(i * 4, 0xDEADBEEF);
        uint32_t v = read32_mswi(i * 4);
        snprintf(tag, sizeof(tag), "FN-MSWI-04-%02d", i + 1);
        check(v == 1, tag, "WARL on all harts: 0xDEADBEEF -> 1");
        check(v == 1, "CP-2.2-MSWI", "WARL on all harts");
    }
    cleanup_mswi();
}

void TestRunner::test_ir_mswi_05() {
    char tag[64];
    int a = 1;
    for (int i = 0; i < NUM_HARTS; i++) {
        write32_mswi(i * 4, 1);
        wait(sc_core::SC_ZERO_TIME);
        snprintf(tag, sizeof(tag), "IR-MSWI-05-%02d", a++);
        check(sw_irq[i].read() == true, tag, "sw_irq asserted after write 1");
        check(sw_irq[i].read() == true, "CP-2.4-MSWI", "sw_irq asserted after write 1");

        write32_mswi(i * 4, 0);
        wait(sc_core::SC_ZERO_TIME);
        snprintf(tag, sizeof(tag), "IR-MSWI-05-%02d", a++);
        check(sw_irq[i].read() == false, tag, "sw_irq deasserted after write 0");
        check(sw_irq[i].read() == false, "CP-2.4-MSWI", "sw_irq deasserted after write 0");
    }
    cleanup_mswi();
}

void TestRunner::test_ir_mswi_06() {
    char tag[64];
    write32_mswi(0x0000, 1);
    wait(sc_core::SC_ZERO_TIME);
    check(sw_irq[0].read() == true, "IR-MSWI-06-01", "sw_irq[0] asserted");
    check(sw_irq[0].read() == true, "CP-2.4-MSWI", "sw_irq[0] asserted for cross-hart check");
    for (int i = 1; i < NUM_HARTS; i++) {
        snprintf(tag, sizeof(tag), "IR-MSWI-06-%02d", i + 1);
        check(sw_irq[i].read() == false, tag, "other hart sw_irq remains false");
        check(sw_irq[i].read() == false, "CP-2.4-MSWI", "cross-hart independence");
    }
    cleanup_mswi();
}

void TestRunner::test_ir_mswi_07() {
    char tag[64];
    for (int i = 0; i < NUM_HARTS; i++)
        write32_mswi(i * 4, 1);
    wait(sc_core::SC_ZERO_TIME);
    for (int i = 0; i < NUM_HARTS; i++) {
        snprintf(tag, sizeof(tag), "IR-MSWI-07-%02d", i + 1);
        check(sw_irq[i].read() == true, tag, "all harts sw_irq asserted");
    }
    for (int i = 0; i < NUM_HARTS; i++)
        write32_mswi(i * 4, 0);
    wait(sc_core::SC_ZERO_TIME);
    for (int i = 0; i < NUM_HARTS; i++) {
        snprintf(tag, sizeof(tag), "IR-MSWI-07-%02d", i + NUM_HARTS + 1);
        check(sw_irq[i].read() == false, tag, "all harts sw_irq deasserted");
    }
    cleanup_mswi();
}

void TestRunner::test_fn_mswi_08() {
    write32_mswi(0x0000, 1);
    write32_mswi(0x0000, 0);
    write32_mswi(0x0000, 1);
    uint32_t v = read32_mswi(0x0000);
    check(v == 1, "FN-MSWI-08-01", "rapid toggle final value == 1");
    check(v == 1, "CP-2.1-MSWI", "rapid toggle final value == 1");
    wait(sc_core::SC_ZERO_TIME);
    check(sw_irq[0].read() == true, "FN-MSWI-08-02", "sw_irq asserted after rapid toggle");
    check(sw_irq[0].read() == true, "CP-2.4-MSWI", "sw_irq asserted after rapid toggle");
    v = read32_mswi(0x0000);
    check(v == 1, "FN-MSWI-08-03", "read after wait still 1");
    cleanup_mswi();
}

void TestRunner::test_fn_mswi_09() {
    cleanup_mswi();
    uint32_t v = read32_mswi(0x0000);
    check(v == 0, "FN-MSWI-09-01", "write 0 to already-0 register reads 0");
    write32_mswi(0x0000, 0);
    v = read32_mswi(0x0000);
    check(v == 0, "FN-MSWI-09-02", "still 0 after redundant write");
    wait(sc_core::SC_ZERO_TIME);
    check(sw_irq[0].read() == false, "FN-MSWI-09-03", "sw_irq still false");
    cleanup_mswi();
}

void TestRunner::test_fn_mswi_10() {
    write32_mswi(0x0000, 1);
    uint32_t v = read32_mswi(0x0000);
    check(v == 1, "FN-MSWI-10-01", "b2b write1 then read == 1");
    check(v == 1, "CP-2.1-MSWI", "b2b write1 then read == 1");
    write32_mswi(0x0000, 0);
    v = read32_mswi(0x0000);
    check(v == 0, "FN-MSWI-10-02", "b2b write0 then read == 0");
    write32_mswi(0x0004, 1);
    v = read32_mswi(0x0004);
    check(v == 1, "FN-MSWI-10-03", "b2b write1 hart1 then read == 1");
    write32_mswi(0x0004, 0);
    v = read32_mswi(0x0004);
    check(v == 0, "FN-MSWI-10-04", "b2b write0 hart1 then read == 0");
    cleanup_mswi();
}

void TestRunner::test_ec_mswi_11() {
    tlm::tlm_response_status st;
    // With 8 harts, valid range is 0x0000..0x001C; 0x0020 is out-of-range
    st = read32_mswi_status(0x0020);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MSWI-11-01", "read 0x0020 -> ADDRESS_ERROR");
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "CP-2.9-MSWI", "read beyond msip7");
    st = write32_mswi_status(0x0020, 1);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MSWI-11-02", "write 0x0020 -> ADDRESS_ERROR");
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "CP-2.9-MSWI", "write beyond msip7");
    st = read32_mswi_status(0x0003);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MSWI-11-03", "read 0x0003 unaligned -> ADDRESS_ERROR");
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "CP-2.9-MSWI", "unaligned read");
    st = read32_mswi_status(0x1000);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MSWI-11-04", "read 0x1000 -> ADDRESS_ERROR");
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "CP-2.9-MSWI", "reserved space");
}

void TestRunner::test_ec_mswi_12() {
    tlm::tlm_response_status st;
    st = do_mswi_txn_len(0x0000, 1);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-MSWI-12-01", "len=1 -> GENERIC_ERROR");
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "CP-2.9-MSWI", "len=1 error");
    st = do_mswi_txn_len(0x0000, 2);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-MSWI-12-02", "len=2 -> GENERIC_ERROR");
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "CP-2.9-MSWI", "len=2 error");
    st = do_mswi_txn_len(0x0000, 3);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-MSWI-12-03", "len=3 -> GENERIC_ERROR");
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "CP-2.9-MSWI", "len=3 error");
    st = do_mswi_txn_len(0x0000, 8);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-MSWI-12-04", "len=8 -> GENERIC_ERROR");
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "CP-2.9-MSWI", "len=8 error");
}

void TestRunner::test_fn_mswi_13() {
    char tag[64];
    struct { uint32_t wval; uint32_t expect; } patterns[] = {
        {0x00000003, 1}, {0x00000000, 0}, {0xAAAAAAAA, 0}, {0x55555555, 1}
    };
    for (int i = 0; i < 4; i++) {
        write32_mswi(0x0000, patterns[i].wval);
        uint32_t v = read32_mswi(0x0000);
        snprintf(tag, sizeof(tag), "FN-MSWI-13-%02d", i + 1);
        check(v == patterns[i].expect, tag, "additional WARL edge pattern");
        check(v == patterns[i].expect, "CP-2.7-MSWI", "boundary WARL pattern");
    }
    cleanup_mswi();
}

// ═══════════════════════════════════════════════════════════════════
//  MTIMER TESTS (original, updated for 8 harts)
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_fn_mtimer_01() {
    char tag[64];
    for (int i = 0; i < NUM_HARTS; i++) {
        uint64_t cmp = read64_mtimer(i * 8);
        snprintf(tag, sizeof(tag), "FN-MTIMER-01-%02d", i + 1);
        check(cmp == 0xFFFFFFFFFFFFFFFFULL, tag, "mtimecmp reset == 0xFFFFFFFFFFFFFFFF");
        check(cmp == 0xFFFFFFFFFFFFFFFFULL, "CP-2.3-MTIMER", "mtimecmp reset value");
    }
}

void TestRunner::test_fn_mtimer_02() {
    uint64_t mt = read64_mtimer(0x7FF8);
    check(mt < 100, "FN-MTIMER-02-01", "mtime near zero at start");
    check(mt < 100, "CP-2.3-MTIMER", "mtime reset value small");
}

void TestRunner::test_ir_mtimer_03() {
    char tag[64];
    for (int i = 0; i < NUM_HARTS; i++) {
        snprintf(tag, sizeof(tag), "IR-MTIMER-03-%02d", i + 1);
        check(timer_irq[i].read() == false, tag, "timer_irq deasserted at reset");
        check(timer_irq[i].read() == false, "CP-2.3-MTIMER", "timer_irq deasserted at reset");
    }
}

void TestRunner::test_fn_mtimer_04() {
    write64_mtimer(0x0000, 0x0000CAFE12345678ULL);
    uint64_t v0 = read64_mtimer(0x0000);
    check(v0 == 0x0000CAFE12345678ULL, "FN-MTIMER-04-01", "mtimecmp[0] 64-bit R/W");
    check(v0 == 0x0000CAFE12345678ULL, "CP-2.1-MTIMER", "mtimecmp[0] 64-bit R/W");

    write64_mtimer(0x0008, 0xAAAABBBBCCCCDDDDULL);
    uint64_t v1 = read64_mtimer(0x0008);
    check(v1 == 0xAAAABBBBCCCCDDDDULL, "FN-MTIMER-04-02", "mtimecmp[1] 64-bit R/W");
    check(v1 == 0xAAAABBBBCCCCDDDDULL, "CP-2.1-MTIMER", "mtimecmp[1] 64-bit R/W");

    write64_mtimer(0x0010, 0x1122334455667788ULL);
    uint64_t v2 = read64_mtimer(0x0010);
    check(v2 == 0x1122334455667788ULL, "FN-MTIMER-04-03", "mtimecmp[2] 64-bit R/W");

    write64_mtimer(0x0018, 0xFEDCBA9876543210ULL);
    uint64_t v3 = read64_mtimer(0x0018);
    check(v3 == 0xFEDCBA9876543210ULL, "FN-MTIMER-04-04", "mtimecmp[3] 64-bit R/W");

    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_05() {
    write64_mtimer(0x7FF8, 0x0000000000001000ULL);
    uint64_t v = read64_mtimer(0x7FF8);
    check(v >= 0x1000, "FN-MTIMER-05-01", "mtime write 0x1000 read back >= 0x1000");
    check(v >= 0x1000, "CP-2.2-MTIMER", "mtime write then read");

    write64_mtimer(0x7FF8, 0);
    v = read64_mtimer(0x7FF8);
    check(v < 100, "FN-MTIMER-05-02", "mtime write 0 read back < 100");
    check(v < 100, "CP-2.2-MTIMER", "mtime write 0 read back small");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_06() {
    write32_mtimer(0x0000, 0x12345678);
    write32_mtimer(0x0004, 0xABCDEF01);
    uint64_t full = read64_mtimer(0x0000);
    check(full == 0xABCDEF0112345678ULL, "FN-MTIMER-06-01", "split access hart0 full64 readback");
    check(full == 0xABCDEF0112345678ULL, "CP-2.7-MTIMER-SPLIT", "split access hart0 full64");

    uint32_t lo = read32_mtimer(0x0000);
    check(lo == 0x12345678, "FN-MTIMER-06-02", "split access hart0 lo word");
    check(lo == 0x12345678, "CP-2.7-MTIMER-SPLIT", "split access hart0 lo");

    uint32_t hi = read32_mtimer(0x0004);
    check(hi == 0xABCDEF01, "FN-MTIMER-06-03", "split access hart0 hi word");
    check(hi == 0xABCDEF01, "CP-2.7-MTIMER-SPLIT", "split access hart0 hi");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_07() {
    write32_mtimer(0x0008, 0xDEADBEEF);
    write32_mtimer(0x000C, 0xCAFEBABE);
    uint64_t full = read64_mtimer(0x0008);
    check(full == 0xCAFEBABEDEADBEEFULL, "FN-MTIMER-07-01", "split access hart1 full64");
    check(full == 0xCAFEBABEDEADBEEFULL, "CP-2.7-MTIMER-SPLIT", "split access hart1 full64");

    uint32_t lo = read32_mtimer(0x0008);
    check(lo == 0xDEADBEEF, "FN-MTIMER-07-02", "split access hart1 lo");

    uint32_t hi = read32_mtimer(0x000C);
    check(hi == 0xCAFEBABE, "FN-MTIMER-07-03", "split access hart1 hi");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_08() {
    write32_mtimer(0x0010, 0x11111111);
    write32_mtimer(0x0014, 0x22222222);
    uint64_t full = read64_mtimer(0x0010);
    check(full == 0x2222222211111111ULL, "FN-MTIMER-08-01", "split access hart2 full64");

    uint32_t lo = read32_mtimer(0x0010);
    check(lo == 0x11111111, "FN-MTIMER-08-02", "split access hart2 lo");

    uint32_t hi = read32_mtimer(0x0014);
    check(hi == 0x22222222, "FN-MTIMER-08-03", "split access hart2 hi");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_09() {
    write32_mtimer(0x0018, 0x33333333);
    write32_mtimer(0x001C, 0x44444444);
    uint64_t full = read64_mtimer(0x0018);
    check(full == 0x4444444433333333ULL, "FN-MTIMER-09-01", "split access hart3 full64");

    uint32_t lo = read32_mtimer(0x0018);
    check(lo == 0x33333333, "FN-MTIMER-09-02", "split access hart3 lo");

    uint32_t hi = read32_mtimer(0x001C);
    check(hi == 0x44444444, "FN-MTIMER-09-03", "split access hart3 hi");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_10() {
    write64_mtimer(0x7FF8, 0);
    write32_mtimer(0x7FF8, 0x00000000);
    write32_mtimer(0x7FFC, 0x00000000);
    uint64_t v = read64_mtimer(0x7FF8);
    check(v < 100, "FN-MTIMER-10-01", "mtime split write 0/0 read < 100");
    check(v < 100, "CP-2.7-MTIMER-SPLIT", "mtime split write zeros");

    write32_mtimer(0x7FFC, 0x12345678);
    uint32_t hi = read32_mtimer(0x7FFC);
    check(hi == 0x12345678, "FN-MTIMER-10-02", "mtime split write hi then read hi");

    v = read64_mtimer(0x7FF8);
    check((v >> 32) == 0x12345678, "FN-MTIMER-10-03", "mtime full read hi word matches");

    write32_mtimer(0x7FF8, 0x00000000);
    write32_mtimer(0x7FFC, 0x00000000);
    v = read64_mtimer(0x7FF8);
    check(v < 100, "FN-MTIMER-10-04", "mtime reset via split write");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_11() {
    write64_mtimer(0x7FF8, 0);
    write64_mtimer(0x0000, 5);
    wait(10, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "FN-MTIMER-11-01", "irq asserted after mtime > mtimecmp");
    check(timer_irq[0].read() == true, "CP-2.4-MTIMER", "safe update sequence: initially asserted");

    write32_mtimer(0x0004, 0xFFFFFFFF);
    write32_mtimer(0x0000, 100);
    write32_mtimer(0x0004, 0x00000000);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == false, "FN-MTIMER-11-02", "irq deasserted after safe update to 100");

    wait(100, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "FN-MTIMER-11-03", "irq re-asserted after mtime >= 100");
    cleanup_mtimer();
}

void TestRunner::test_ir_mtimer_12() {
    write64_mtimer(0x7FF8, 0);
    write64_mtimer(0x0000, 5);
    wait(10, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "IR-MTIMER-12-01", "timer irq assert");
    check(timer_irq[0].read() == true, "CP-2.4-MTIMER", "timer irq assert");

    write64_mtimer(0x0000, 0xFFFFFFFFFFFFFFFFULL);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == false, "IR-MTIMER-12-02", "timer irq deassert after mtimecmp=max");
    check(timer_irq[0].read() == false, "CP-2.4-MTIMER", "timer irq deassert");

    write64_mtimer(0x0000, 5);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "IR-MTIMER-12-03", "timer irq re-assert");

    cleanup_mtimer();
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == false, "IR-MTIMER-12-04", "timer irq deassert after cleanup");
}

void TestRunner::test_ir_mtimer_13() {
    char tag[64];
    write64_mtimer(0x7FF8, 0);
    for (int i = 0; i < NUM_HARTS; i++)
        write64_mtimer(i * 8, 3);
    wait(10, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    for (int i = 0; i < NUM_HARTS; i++) {
        snprintf(tag, sizeof(tag), "IR-MTIMER-13-%02d", i + 1);
        check(timer_irq[i].read() == true, tag, "multi-hart timer irq asserted");
        check(timer_irq[i].read() == true, "CP-2.4-MTIMER", "multi-hart timer irq asserted");
    }
    for (int i = 0; i < NUM_HARTS; i++)
        write64_mtimer(i * 8, 0xFFFFFFFFFFFFFFFFULL);
    wait(sc_core::SC_ZERO_TIME);
    for (int i = 0; i < NUM_HARTS; i++) {
        snprintf(tag, sizeof(tag), "IR-MTIMER-13-%02d", i + NUM_HARTS + 1);
        check(timer_irq[i].read() == false, tag, "multi-hart timer irq deasserted");
        check(timer_irq[i].read() == false, "CP-2.4-MTIMER", "multi-hart timer irq deasserted");
    }
    cleanup_mtimer();
}

void TestRunner::test_ir_mtimer_14() {
    write64_mtimer(0x7FF8, 0);
    write64_mtimer(0x0000, 0);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "IR-MTIMER-14-01", "mtimecmp=0 mtime>=0 irq asserted");
    check(timer_irq[0].read() == true, "CP-2.7-MTIMER", "mtimecmp=0 boundary");

    write64_mtimer(0x0000, 0xFFFFFFFFFFFFFFFFULL);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == false, "IR-MTIMER-14-02", "irq deasserted after mtimecmp=max");
    check(timer_irq[0].read() == false, "CP-2.7-MTIMER", "mtimecmp=max deassert");
    cleanup_mtimer();
}

void TestRunner::test_ir_mtimer_15() {
    char tag[64];
    write64_mtimer(0x7FF8, 0);
    write64_mtimer(0x0000, 3);
    for (int i = 1; i < NUM_HARTS; i++)
        write64_mtimer(i * 8, 0xFFFFFFFFFFFFFFFFULL);
    wait(10, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "IR-MTIMER-15-01", "hart0 timer irq asserted");
    for (int i = 1; i < NUM_HARTS; i++) {
        snprintf(tag, sizeof(tag), "IR-MTIMER-15-%02d", i + 1);
        check(timer_irq[i].read() == false, tag, "other hart timer irq remains false");
    }
    cleanup_mtimer();
}

void TestRunner::test_ir_mtimer_16() {
    write64_mtimer(0x7FF8, 0);
    write64_mtimer(0x0000, 100);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == false, "IR-MTIMER-16-01", "irq false when mtime < mtimecmp");

    write64_mtimer(0x7FF8, 200);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "IR-MTIMER-16-02", "irq true after mtime written >= mtimecmp");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_17() {
    write64_mtimer(0x7FF8, 0xFFFFFFFFFFFFFFFEULL);
    wait(5, sc_core::SC_US);
    uint64_t mt = read64_mtimer(0x7FF8);
    check(mt != 0xFFFFFFFFFFFFFFFEULL, "FN-MTIMER-17-01", "mtime changed from initial value");
    check(mt < 0xFFFFFFFFFFFFFFF0ULL || mt < 100, "FN-MTIMER-17-02", "mtime wrapped or advanced");
    check(mt < 0xFFFFFFFFFFFFFFF0ULL || mt < 100, "CP-2.7-MTIMER", "mtime wrap-around");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_18() {
    write64_mtimer(0x7FF8, 0);
    write64_mtimer(0x0000, 100);
    write64_mtimer(0x0000, 200);
    write64_mtimer(0x0000, 300);
    uint64_t v = read64_mtimer(0x0000);
    check(v == 300, "FN-MTIMER-18-01", "back-to-back mtimecmp updates, final == 300");
    check(v == 300, "CP-2.1-MTIMER", "back-to-back mtimecmp final value");

    wait(110, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == false, "FN-MTIMER-18-02", "mtime ~110 < 300, irq false");

    wait(200, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "FN-MTIMER-18-03", "mtime > 300, irq true");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_19() {
    write64_mtimer(0x7FF8, 0);
    wait(10, sc_core::SC_US);
    write64_mtimer(0x7FF8, 500);
    wait(5, sc_core::SC_US);
    uint64_t mt = read64_mtimer(0x7FF8);
    check(mt >= 500, "FN-MTIMER-19-01", "mtime continued from written value");
    check(mt < 600, "FN-MTIMER-19-02", "mtime within expected range");
    check(mt >= 500, "CP-2.2-MTIMER", "mtime write while timer running");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_20() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0);

    write64_mtimer(0x0000, 10);
    wait(15, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "FN-MTIMER-20-01", "timer_irq asserts after mtime >= mtimecmp");
    check(timer_irq[0].read() == true, "CP-2.8-MTIMER", "timer_irq asserts after mtime >= mtimecmp");

    write64_mtimer(0x0000, 0xFFFFFFFFFFFFFFFFULL);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == false, "FN-MTIMER-20-02", "timer_irq deasserts");
    check(timer_irq[0].read() == false, "CP-2.8-MTIMER", "timer_irq deasserts after mtimecmp max");

    write32_mswi(0x0000, 1);
    wait(sc_core::SC_ZERO_TIME);
    check(sw_irq[0].read() == true, "FN-MTIMER-20-03", "sw_irq asserts via mswi");
    check(sw_irq[0].read() == true, "CP-2.8-MTIMER", "sw_irq asserts after msip write 1");

    write32_mswi(0x0000, 0);
    wait(sc_core::SC_ZERO_TIME);
    check(sw_irq[0].read() == false, "FN-MTIMER-20-04", "sw_irq deasserts via mswi");
    check(sw_irq[0].read() == false, "CP-2.8-MTIMER", "sw_irq deasserts after msip write 0");

    uint64_t cmp = read64_mtimer(0x0000);
    check(cmp == 0xFFFFFFFFFFFFFFFFULL, "FN-MTIMER-20-05", "mtimecmp readback after sequence");

    uint32_t msip = read32_mswi(0x0000);
    check(msip == 0, "FN-MTIMER-20-06", "msip readback after sequence");
    cleanup_mtimer();
    cleanup_mswi();
}

void TestRunner::test_ec_mtimer_21() {
    tlm::tlm_response_status st;
    // With 8 harts, mtimecmp goes 0x0000..0x003F. 0x0040 is in the gap.
    st = read32_mtimer_status(0x0040);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MTIMER-21-01", "gap 0x0040 -> ADDRESS_ERROR");
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "CP-2.9-MTIMER", "reserved gap 0x0040");
    st = read32_mtimer_status(0x1000);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MTIMER-21-02", "gap 0x1000 -> ADDRESS_ERROR");
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "CP-2.9-MTIMER", "reserved gap 0x1000");
    st = read32_mtimer_status(0x7FD0);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MTIMER-21-03", "gap 0x7FD0 -> ADDRESS_ERROR");
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "CP-2.9-MTIMER", "reserved gap 0x7FD0");
}

void TestRunner::test_ec_mtimer_22() {
    tlm::tlm_response_status st;
    st = read32_mtimer_status(0x0003);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MTIMER-22-01", "unaligned 0x0003 -> ADDRESS_ERROR");
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "CP-2.9-MTIMER", "unaligned 0x0003");
    st = read32_mtimer_status(0x0001);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MTIMER-22-02", "unaligned 0x0001 -> ADDRESS_ERROR");
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "CP-2.9-MTIMER", "unaligned 0x0001");
}

void TestRunner::test_ec_mtimer_23() {
    tlm::tlm_response_status st;
    st = do_mtimer_txn_len(0x0000, 1);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-MTIMER-23-01", "len=1 -> GENERIC_ERROR");
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "CP-2.9-MTIMER", "invalid len=1");
    st = do_mtimer_txn_len(0x0000, 3);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-MTIMER-23-02", "len=3 -> GENERIC_ERROR");
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "CP-2.9-MTIMER", "invalid len=3");
    st = do_mtimer_txn_len(0x0000, 5);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-MTIMER-23-03", "len=5 -> GENERIC_ERROR");
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "CP-2.9-MTIMER", "invalid len=5");
    st = do_mtimer_txn_len(0x0000, 16);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-MTIMER-23-04", "len=16 -> GENERIC_ERROR");
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "CP-2.9-MTIMER", "invalid len=16");
}

void TestRunner::test_fn_mtimer_24() {
    write64_mtimer(0x7FF8, 0xDEADCAFE12345678ULL);
    uint64_t v = read64_mtimer(0x7FF8);
    check(v >= 0xDEADCAFE12345678ULL, "FN-MTIMER-24-01", "full 64-bit mtime write and readback");
    check(v >= 0xDEADCAFE12345678ULL, "CP-2.2-MTIMER", "full 64-bit mtime access");

    write64_mtimer(0x7FF8, 0);
    v = read64_mtimer(0x7FF8);
    check(v < 100, "FN-MTIMER-24-02", "mtime reset to 0 readback small");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_25() {
    char tag[64];
    uint64_t vals[] = {0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666, 0x7777, 0x8888};
    for (int i = 0; i < NUM_HARTS; i++)
        write64_mtimer(i * 8, vals[i]);
    for (int i = 0; i < NUM_HARTS; i++) {
        uint64_t v = read64_mtimer(i * 8);
        snprintf(tag, sizeof(tag), "FN-MTIMER-25-%02d", i + 1);
        check(v == vals[i], tag, "mtimecmp persistence across harts");
        check(v == vals[i], "CP-2.1-MTIMER", "mtimecmp persistence");
    }
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_26() {
    write64_mtimer(0x7FF8, 0);
    wait(5, sc_core::SC_US);
    uint64_t val1 = read64_mtimer(0x7FF8);
    wait(5, sc_core::SC_US);
    uint64_t val2 = read64_mtimer(0x7FF8);
    check(val2 > val1, "FN-MTIMER-26-01", "mtime monotonic increment");
    check(val2 >= val1 + 4, "FN-MTIMER-26-02", "mtime incremented by at least 4 ticks in 5us");
    check(val2 > val1, "CP-2.2-MTIMER", "mtime monotonic");
    cleanup_mtimer();
}

void TestRunner::test_ir_mtimer_27() {
    write64_mtimer(0x7FF8, 0);
    write64_mtimer(0x0000, 3);
    wait(10, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "IR-MTIMER-27-01", "timer irq level-sensitive persists");
    check(timer_irq[0].read() == true, "CP-2.4-MTIMER", "timer irq persists");

    wait(10, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "IR-MTIMER-27-02", "timer irq still asserted after more time");
    cleanup_mtimer();
}

// ═══════════════════════════════════════════════════════════════════
//  MTIMER NEW FEATURE TESTS (prescaler, timer_enable, overflow_flag)
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_fn_mtimer_28_prescaler_default() {
    uint32_t ps = read32_mtimer(0x7FF0);
    check(ps == 1, "FN-MTIMER-28-01", "prescaler default value == 1");
    check(ps == 1, "CP-3.8-MTIMER", "prescaler reset value");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_29_prescaler_rw() {
    write32_mtimer(0x7FF0, 10);
    uint32_t v = read32_mtimer(0x7FF0);
    check(v == 10, "FN-MTIMER-29-01", "prescaler write 10 read 10");
    check(v == 10, "CP-3.8-MTIMER", "prescaler RW");

    write32_mtimer(0x7FF0, 100);
    v = read32_mtimer(0x7FF0);
    check(v == 100, "FN-MTIMER-29-02", "prescaler write 100 read 100");

    write32_mtimer(0x7FF0, 65535);
    v = read32_mtimer(0x7FF0);
    check(v == 65535, "FN-MTIMER-29-03", "prescaler write 65535 read 65535");

    write32_mtimer(0x7FF0, 1);
    v = read32_mtimer(0x7FF0);
    check(v == 1, "FN-MTIMER-29-04", "prescaler write 1 read 1");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_30_prescaler_warl() {
    write32_mtimer(0x7FF0, 0);
    uint32_t v = read32_mtimer(0x7FF0);
    check(v == 1, "FN-MTIMER-30-01", "prescaler WARL: 0 stored as 1");
    check(v == 1, "CP-3.8-MTIMER", "prescaler WARL clamp min");

    write32_mtimer(0x7FF0, 70000);
    v = read32_mtimer(0x7FF0);
    check(v == 65535, "FN-MTIMER-30-02", "prescaler WARL: 70000 clamped to 65535");
    check(v == 65535, "CP-3.8-MTIMER", "prescaler WARL clamp max");

    write32_mtimer(0x7FF0, 0xFFFFFFFF);
    v = read32_mtimer(0x7FF0);
    check(v == 65535, "FN-MTIMER-30-03", "prescaler WARL: 0xFFFFFFFF clamped to 65535");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_31_prescaler_effect() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0);
    write32_mtimer(0x7FF0, 1);
    wait(20, sc_core::SC_US);
    uint64_t count_ps1 = read64_mtimer(0x7FF8);

    write64_mtimer(0x7FF8, 0);
    write32_mtimer(0x7FF0, 10);
    wait(20, sc_core::SC_US);
    uint64_t count_ps10 = read64_mtimer(0x7FF8);

    check(count_ps1 > count_ps10, "FN-MTIMER-31-01", "prescaler=1 increments faster than prescaler=10");
    check(count_ps1 > count_ps10, "CP-3.8-MTIMER", "prescaler slows timer");
    check(count_ps10 >= 1, "FN-MTIMER-31-02", "prescaler=10 still increments");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_32_timer_enable_default() {
    uint32_t en = read32_mtimer(0x7FE8);
    check(en == 1, "FN-MTIMER-32-01", "timer_enable default value == 1");
    check(en == 1, "CP-3.9-MTIMER", "timer_enable reset value");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_33_timer_disable() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0);
    wait(5, sc_core::SC_US);
    uint64_t before = read64_mtimer(0x7FF8);
    check(before > 0, "FN-MTIMER-33-01", "mtime incrementing when enabled");

    write32_mtimer(0x7FE8, 0);
    wait(sc_core::SC_ZERO_TIME);
    uint64_t frozen = read64_mtimer(0x7FF8);
    wait(10, sc_core::SC_US);
    uint64_t after = read64_mtimer(0x7FF8);
    check(after == frozen, "FN-MTIMER-33-02", "mtime frozen after timer_enable=0");
    check(after == frozen, "CP-3.9-MTIMER", "timer disabled freezes mtime");

    write64_mtimer(0x7FF8, 1000);
    uint64_t wr = read64_mtimer(0x7FF8);
    check(wr == 1000, "FN-MTIMER-33-03", "mtime writable even when disabled");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_34_timer_reenable() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0);
    write32_mtimer(0x7FE8, 0);
    wait(10, sc_core::SC_US);
    uint64_t frozen = read64_mtimer(0x7FF8);
    check(frozen < 5, "FN-MTIMER-34-01", "mtime stayed frozen");

    write32_mtimer(0x7FE8, 1);
    wait(10, sc_core::SC_US);
    uint64_t resumed = read64_mtimer(0x7FF8);
    check(resumed > frozen, "FN-MTIMER-34-02", "mtime resumed after re-enable");
    check(resumed > frozen, "CP-3.9-MTIMER", "re-enable resumes timer");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_35_timer_enable_warl() {
    write32_mtimer(0x7FE8, 0xFFFFFFFF);
    uint32_t v = read32_mtimer(0x7FE8);
    check(v == 1, "FN-MTIMER-35-01", "timer_enable WARL: 0xFFFFFFFF -> 1");
    check(v == 1, "CP-3.9-MTIMER", "timer_enable WARL bit[0] only");

    write32_mtimer(0x7FE8, 0xFFFFFFFE);
    v = read32_mtimer(0x7FE8);
    check(v == 0, "FN-MTIMER-35-02", "timer_enable WARL: 0xFFFFFFFE -> 0");

    write32_mtimer(0x7FE8, 0x00000002);
    v = read32_mtimer(0x7FE8);
    check(v == 0, "FN-MTIMER-35-03", "timer_enable WARL: 0x02 -> 0");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_36_overflow_default() {
    uint32_t of = read32_mtimer(0x7FE0);
    check(of == 0, "FN-MTIMER-36-01", "overflow_flag default value == 0");
    check(of == 0, "CP-3.10-MTIMER", "overflow_flag reset value");
}

void TestRunner::test_fn_mtimer_37_overflow_detect() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0xFFFFFFFFFFFFFFFDULL);
    wait(5, sc_core::SC_US);
    uint32_t of = read32_mtimer(0x7FE0);
    check(of == 1, "FN-MTIMER-37-01", "overflow_flag set after mtime wraps");
    check(of == 1, "CP-3.10-MTIMER", "overflow detected");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_38_overflow_read_clear() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0xFFFFFFFFFFFFFFFDULL);
    wait(5, sc_core::SC_US);
    uint32_t first = read32_mtimer(0x7FE0);
    check(first == 1, "FN-MTIMER-38-01", "overflow_flag == 1 on first read");
    uint32_t second = read32_mtimer(0x7FE0);
    check(second == 0, "FN-MTIMER-38-02", "overflow_flag auto-cleared after read");
    check(second == 0, "CP-3.10-MTIMER", "read-to-clear semantics");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_39_overflow_write_ignored() {
    cleanup_mtimer();
    write32_mtimer(0x7FE0, 1);
    uint32_t v = read32_mtimer(0x7FE0);
    check(v == 0, "FN-MTIMER-39-01", "overflow_flag write ignored, reads 0");
    check(v == 0, "CP-3.10-MTIMER", "write to overflow_flag is no-op");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_40_split_high_harts() {
    // Test split access on higher harts (4-7) to verify N-hart parameterization
    char tag[64];
    for (int h = 4; h < NUM_HARTS; h++) {
        uint64_t offset = h * 8;
        write32_mtimer(offset, 0xAABBCCDD);
        write32_mtimer(offset + 4, 0x11223344);
        uint64_t full = read64_mtimer(offset);
        snprintf(tag, sizeof(tag), "FN-MTIMER-40-%02d", (h - 4) * 3 + 1);
        check(full == 0x11223344AABBCCDDULL, tag, "split access high hart full64");

        uint32_t lo = read32_mtimer(offset);
        snprintf(tag, sizeof(tag), "FN-MTIMER-40-%02d", (h - 4) * 3 + 2);
        check(lo == 0xAABBCCDD, tag, "split access high hart lo");

        uint32_t hi = read32_mtimer(offset + 4);
        snprintf(tag, sizeof(tag), "FN-MTIMER-40-%02d", (h - 4) * 3 + 3);
        check(hi == 0x11223344, tag, "split access high hart hi");
    }
    cleanup_mtimer();
}

void TestRunner::test_ec_mtimer_41_gap_addresses() {
    tlm::tlm_response_status st;
    // Gaps between control registers
    st = read32_mtimer_status(0x7FE4);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MTIMER-41-01", "gap 0x7FE4 -> ADDRESS_ERROR");
    st = read32_mtimer_status(0x7FEC);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MTIMER-41-02", "gap 0x7FEC -> ADDRESS_ERROR");
    st = read32_mtimer_status(0x7FF4);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MTIMER-41-03", "gap 0x7FF4 -> ADDRESS_ERROR");
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "CP-2.9-MTIMER", "control register gap");
}

// ═══════════════════════════════════════════════════════════════════
//  SSWI TESTS
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_fn_sswi_01() {
    char tag[64];
    for (int i = 0; i < NUM_HARTS; i++) {
        uint32_t v = read32_sswi(i * 4);
        snprintf(tag, sizeof(tag), "FN-SSWI-01-%02d", i + 1);
        check(v == 0x00000000, tag, "setssip reset value == 0");
        check(v == 0x00000000, "CP-2.3-SSWI", "setssip reset value == 0");
    }
    for (int i = 0; i < NUM_HARTS; i++) {
        snprintf(tag, sizeof(tag), "FN-SSWI-01-%02d", i + NUM_HARTS + 1);
        check(ssw_irq[i].read() == false, tag, "ssw_irq deasserted at reset");
        check(ssw_irq[i].read() == false, "CP-2.3-SSWI", "ssw_irq deasserted at reset");
    }
}

void TestRunner::test_fn_sswi_02() {
    char tag[64];
    int a = 1;
    for (int i = 0; i < NUM_HARTS; i++) {
        write32_sswi(i * 4, 1);
        uint32_t v = read32_sswi(i * 4);
        snprintf(tag, sizeof(tag), "FN-SSWI-02-%02d", a++);
        check(v == 1, tag, "write 1 read back 1");
        check(v == 1, "CP-2.1-SSWI", "write 1 read back 1");

        write32_sswi(i * 4, 0);
        v = read32_sswi(i * 4);
        snprintf(tag, sizeof(tag), "FN-SSWI-02-%02d", a++);
        check(v == 0, tag, "write 0 read back 0");
        check(v == 0, "CP-2.1-SSWI", "write 0 read back 0");
    }
    cleanup_sswi();
}

void TestRunner::test_fn_sswi_03() {
    char tag[64];
    struct { uint32_t wval; uint32_t expect; } patterns[] = {
        {0xFFFFFFFF, 1}, {0xFFFFFFFE, 0}, {0x80000001, 1},
        {0x00000002, 0}, {0x7FFFFFFF, 1}, {0x80000000, 0}
    };
    for (int i = 0; i < 6; i++) {
        write32_sswi(0x0000, patterns[i].wval);
        uint32_t v = read32_sswi(0x0000);
        snprintf(tag, sizeof(tag), "FN-SSWI-03-%02d", i + 1);
        check(v == patterns[i].expect, tag, "WARL enforcement on hart 0");
        check(v == patterns[i].expect, "CP-2.2-SSWI", "WARL enforcement");
    }
    cleanup_sswi();
}

void TestRunner::test_fn_sswi_04() {
    char tag[64];
    for (int i = 0; i < NUM_HARTS; i++) {
        write32_sswi(i * 4, 0xDEADBEEF);
        uint32_t v = read32_sswi(i * 4);
        snprintf(tag, sizeof(tag), "FN-SSWI-04-%02d", i + 1);
        check(v == 1, tag, "WARL on all harts: 0xDEADBEEF -> 1");
        check(v == 1, "CP-2.2-SSWI", "WARL on all harts");
    }
    cleanup_sswi();
}

void TestRunner::test_ir_sswi_05() {
    char tag[64];
    int a = 1;
    for (int i = 0; i < NUM_HARTS; i++) {
        write32_sswi(i * 4, 1);
        wait(sc_core::SC_ZERO_TIME);
        snprintf(tag, sizeof(tag), "IR-SSWI-05-%02d", a++);
        check(ssw_irq[i].read() == true, tag, "ssw_irq asserted after write 1");
        check(ssw_irq[i].read() == true, "CP-2.4-SSWI", "ssw_irq asserted");

        write32_sswi(i * 4, 0);
        wait(sc_core::SC_ZERO_TIME);
        snprintf(tag, sizeof(tag), "IR-SSWI-05-%02d", a++);
        check(ssw_irq[i].read() == false, tag, "ssw_irq deasserted after write 0");
        check(ssw_irq[i].read() == false, "CP-2.4-SSWI", "ssw_irq deasserted");
    }
    cleanup_sswi();
}

void TestRunner::test_ir_sswi_06() {
    char tag[64];
    write32_sswi(0x0000, 1);
    wait(sc_core::SC_ZERO_TIME);
    check(ssw_irq[0].read() == true, "IR-SSWI-06-01", "ssw_irq[0] asserted");
    for (int i = 1; i < NUM_HARTS; i++) {
        snprintf(tag, sizeof(tag), "IR-SSWI-06-%02d", i + 1);
        check(ssw_irq[i].read() == false, tag, "other hart ssw_irq remains false");
        check(ssw_irq[i].read() == false, "CP-2.4-SSWI", "cross-hart independence");
    }
    cleanup_sswi();
}

void TestRunner::test_ir_sswi_07() {
    char tag[64];
    for (int i = 0; i < NUM_HARTS; i++)
        write32_sswi(i * 4, 1);
    wait(sc_core::SC_ZERO_TIME);
    for (int i = 0; i < NUM_HARTS; i++) {
        snprintf(tag, sizeof(tag), "IR-SSWI-07-%02d", i + 1);
        check(ssw_irq[i].read() == true, tag, "all harts ssw_irq asserted");
    }
    for (int i = 0; i < NUM_HARTS; i++)
        write32_sswi(i * 4, 0);
    wait(sc_core::SC_ZERO_TIME);
    for (int i = 0; i < NUM_HARTS; i++) {
        snprintf(tag, sizeof(tag), "IR-SSWI-07-%02d", i + NUM_HARTS + 1);
        check(ssw_irq[i].read() == false, tag, "all harts ssw_irq deasserted");
    }
    cleanup_sswi();
}

void TestRunner::test_fn_sswi_08() {
    write32_sswi(0x0000, 1);
    write32_sswi(0x0000, 0);
    write32_sswi(0x0000, 1);
    uint32_t v = read32_sswi(0x0000);
    check(v == 1, "FN-SSWI-08-01", "rapid toggle final value == 1");
    wait(sc_core::SC_ZERO_TIME);
    check(ssw_irq[0].read() == true, "FN-SSWI-08-02", "ssw_irq asserted after rapid toggle");
    v = read32_sswi(0x0000);
    check(v == 1, "FN-SSWI-08-03", "read after wait still 1");
    cleanup_sswi();
}

void TestRunner::test_fn_sswi_09() {
    cleanup_sswi();
    uint32_t v = read32_sswi(0x0000);
    check(v == 0, "FN-SSWI-09-01", "write 0 to already-0 reads 0");
    write32_sswi(0x0000, 0);
    v = read32_sswi(0x0000);
    check(v == 0, "FN-SSWI-09-02", "still 0 after redundant write");
    wait(sc_core::SC_ZERO_TIME);
    check(ssw_irq[0].read() == false, "FN-SSWI-09-03", "ssw_irq still false");
    cleanup_sswi();
}

void TestRunner::test_fn_sswi_10() {
    write32_sswi(0x0000, 1);
    uint32_t v = read32_sswi(0x0000);
    check(v == 1, "FN-SSWI-10-01", "b2b write1 then read == 1");
    write32_sswi(0x0000, 0);
    v = read32_sswi(0x0000);
    check(v == 0, "FN-SSWI-10-02", "b2b write0 then read == 0");
    write32_sswi(0x0004, 1);
    v = read32_sswi(0x0004);
    check(v == 1, "FN-SSWI-10-03", "b2b write1 hart1 then read == 1");
    write32_sswi(0x0004, 0);
    v = read32_sswi(0x0004);
    check(v == 0, "FN-SSWI-10-04", "b2b write0 hart1 then read == 0");
    cleanup_sswi();
}

void TestRunner::test_ec_sswi_11() {
    tlm::tlm_response_status st;
    st = read32_sswi_status(0x0020);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-SSWI-11-01", "read 0x0020 -> ADDRESS_ERROR");
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "CP-2.9-SSWI", "read beyond setssip7");
    st = write32_sswi_status(0x0020, 1);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-SSWI-11-02", "write 0x0020 -> ADDRESS_ERROR");
    st = read32_sswi_status(0x0003);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-SSWI-11-03", "unaligned -> ADDRESS_ERROR");
    st = read32_sswi_status(0x1000);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-SSWI-11-04", "reserved 0x1000 -> ADDRESS_ERROR");
}

void TestRunner::test_ec_sswi_12() {
    tlm::tlm_response_status st;
    st = do_sswi_txn_len(0x0000, 1);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-SSWI-12-01", "len=1 -> GENERIC_ERROR");
    st = do_sswi_txn_len(0x0000, 2);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-SSWI-12-02", "len=2 -> GENERIC_ERROR");
    st = do_sswi_txn_len(0x0000, 3);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-SSWI-12-03", "len=3 -> GENERIC_ERROR");
    st = do_sswi_txn_len(0x0000, 8);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-SSWI-12-04", "len=8 -> GENERIC_ERROR");
}

void TestRunner::test_fn_sswi_13() {
    char tag[64];
    struct { uint32_t wval; uint32_t expect; } patterns[] = {
        {0x00000003, 1}, {0x00000000, 0}, {0xAAAAAAAA, 0}, {0x55555555, 1}
    };
    for (int i = 0; i < 4; i++) {
        write32_sswi(0x0000, patterns[i].wval);
        uint32_t v = read32_sswi(0x0000);
        snprintf(tag, sizeof(tag), "FN-SSWI-13-%02d", i + 1);
        check(v == patterns[i].expect, tag, "additional WARL edge pattern");
        check(v == patterns[i].expect, "CP-2.7-SSWI", "boundary WARL pattern");
    }
    cleanup_sswi();
}

// ═══════════════════════════════════════════════════════════════════
//  TLM-2.0 ADVANCED TESTS (DMI, Debug Transport, Byte Enable)
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_tlm_mswi_dmi() {
    tlm::tlm_generic_payload trans;
    tlm::tlm_dmi dmi_data;
    trans.set_address(0x0000);
    trans.set_data_length(4);
    bool ok = mswi_socket->get_direct_mem_ptr(trans, dmi_data);
    check(ok, "TLM-MSWI-DMI-01", "DMI request returns true");
    check(ok, "CP-TLM-DMI-MSWI", "DMI supported");
    check(dmi_data.get_dmi_ptr() != nullptr, "TLM-MSWI-DMI-02", "DMI pointer non-null");
    check(dmi_data.get_start_address() == 0x0000, "TLM-MSWI-DMI-03", "DMI start address == 0");
    uint64_t expected_end = static_cast<uint64_t>(NUM_HARTS) * 4 - 1;
    check(dmi_data.get_end_address() == expected_end, "TLM-MSWI-DMI-04", "DMI end address correct");
}

void TestRunner::test_tlm_mtimer_dmi() {
    tlm::tlm_generic_payload trans;
    tlm::tlm_dmi dmi_data;
    trans.set_address(0x7FF8);
    trans.set_data_length(8);
    bool ok = mtimer_socket->get_direct_mem_ptr(trans, dmi_data);
    check(ok, "TLM-MTIMER-DMI-01", "DMI request returns true");
    check(ok, "CP-TLM-DMI-MTIMER", "DMI supported");
    check(dmi_data.get_dmi_ptr() != nullptr, "TLM-MTIMER-DMI-02", "DMI pointer non-null");
    check(dmi_data.get_start_address() == 0x7FF8, "TLM-MTIMER-DMI-03", "DMI start == 0x7FF8");
    check(dmi_data.get_end_address() == 0x7FFF, "TLM-MTIMER-DMI-04", "DMI end == 0x7FFF");
}

void TestRunner::test_tlm_sswi_dmi() {
    tlm::tlm_generic_payload trans;
    tlm::tlm_dmi dmi_data;
    trans.set_address(0x0000);
    trans.set_data_length(4);
    bool ok = sswi_socket->get_direct_mem_ptr(trans, dmi_data);
    check(ok, "TLM-SSWI-DMI-01", "DMI request returns true");
    check(ok, "CP-TLM-DMI-SSWI", "DMI supported");
    check(dmi_data.get_dmi_ptr() != nullptr, "TLM-SSWI-DMI-02", "DMI pointer non-null");
    check(dmi_data.get_start_address() == 0x0000, "TLM-SSWI-DMI-03", "DMI start == 0");
    uint64_t expected_end = static_cast<uint64_t>(NUM_HARTS) * 4 - 1;
    check(dmi_data.get_end_address() == expected_end, "TLM-SSWI-DMI-04", "DMI end address correct");
}

void TestRunner::test_tlm_mswi_dbg() {
    cleanup_mswi();
    // Write via b_transport, read via debug transport
    write32_mswi(0x0000, 1);
    tlm::tlm_generic_payload trans;
    uint32_t dbg_val = 0;
    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(0x0000);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&dbg_val));
    trans.set_data_length(4);
    unsigned int bytes = mswi_socket->transport_dbg(trans);
    check(bytes == 4, "TLM-MSWI-DBG-01", "debug transport returns 4 bytes");
    check(bytes == 4, "CP-TLM-DBG-MSWI", "debug transport read");
    check(dbg_val == 1, "TLM-MSWI-DBG-02", "debug transport reads correct value");

    // Write via debug transport, read via b_transport
    uint32_t wr_val = 0;
    trans.set_command(tlm::TLM_WRITE_COMMAND);
    trans.set_address(0x0000);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&wr_val));
    trans.set_data_length(4);
    bytes = mswi_socket->transport_dbg(trans);
    check(bytes == 4, "TLM-MSWI-DBG-03", "debug transport write returns 4 bytes");

    uint32_t readback = read32_mswi(0x0000);
    check(readback == 0, "TLM-MSWI-DBG-04", "debug write reflected in b_transport read");
    cleanup_mswi();
}

void TestRunner::test_tlm_mtimer_dbg() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0x123456789ABCDEF0ULL);

    tlm::tlm_generic_payload trans;
    uint64_t dbg_val = 0;
    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(0x7FF8);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&dbg_val));
    trans.set_data_length(8);
    unsigned int bytes = mtimer_socket->transport_dbg(trans);
    check(bytes == 8, "TLM-MTIMER-DBG-01", "debug transport returns 8 bytes for mtime");
    check(bytes == 8, "CP-TLM-DBG-MTIMER", "debug transport read");
    check(dbg_val >= 0x123456789ABCDEF0ULL, "TLM-MTIMER-DBG-02", "debug reads correct mtime");

    // Debug write to mtimecmp[0]
    uint64_t cmp_val = 0x0000CAFE00000000ULL;
    trans.set_command(tlm::TLM_WRITE_COMMAND);
    trans.set_address(0x0000);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&cmp_val));
    trans.set_data_length(8);
    bytes = mtimer_socket->transport_dbg(trans);
    check(bytes == 8, "TLM-MTIMER-DBG-03", "debug write mtimecmp returns 8");

    uint64_t cmp_readback = read64_mtimer(0x0000);
    check(cmp_readback == 0x0000CAFE00000000ULL, "TLM-MTIMER-DBG-04", "debug write reflected in b_transport");
    cleanup_mtimer();
}

void TestRunner::test_tlm_sswi_dbg() {
    cleanup_sswi();
    write32_sswi(0x0000, 1);
    tlm::tlm_generic_payload trans;
    uint32_t dbg_val = 0;
    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(0x0000);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&dbg_val));
    trans.set_data_length(4);
    unsigned int bytes = sswi_socket->transport_dbg(trans);
    check(bytes == 4, "TLM-SSWI-DBG-01", "debug transport returns 4 bytes");
    check(bytes == 4, "CP-TLM-DBG-SSWI", "debug transport read");
    check(dbg_val == 1, "TLM-SSWI-DBG-02", "debug transport reads correct value");

    uint32_t wr_val = 0;
    trans.set_command(tlm::TLM_WRITE_COMMAND);
    trans.set_address(0x0000);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&wr_val));
    trans.set_data_length(4);
    bytes = sswi_socket->transport_dbg(trans);
    check(bytes == 4, "TLM-SSWI-DBG-03", "debug write returns 4 bytes");

    uint32_t readback = read32_sswi(0x0000);
    check(readback == 0, "TLM-SSWI-DBG-04", "debug write reflected in b_transport read");
    cleanup_sswi();
}

void TestRunner::test_tlm_mswi_byte_enable() {
    // All-0xFF byte enable should pass through
    unsigned char be_ok[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    tlm::tlm_response_status st = do_mswi_txn_be(0x0000, 1, be_ok, 4);
    check(st == tlm::TLM_OK_RESPONSE, "TLM-MSWI-BE-01", "all-0xFF byte enable accepted");
    check(st == tlm::TLM_OK_RESPONSE, "CP-TLM-BE-MSWI", "byte enable pass-through");

    // Non-0xFF byte enable should be rejected
    unsigned char be_bad[4] = {0xFF, 0x00, 0xFF, 0xFF};
    st = do_mswi_txn_be(0x0000, 1, be_bad, 4);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "TLM-MSWI-BE-02", "partial byte enable rejected");
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "CP-TLM-BE-MSWI", "partial BE rejected");
    cleanup_mswi();
}

void TestRunner::test_tlm_mtimer_byte_enable() {
    unsigned char be_ok[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    tlm::tlm_response_status st = do_mtimer_txn_be(0x7FF0, 1, be_ok, 4);
    check(st == tlm::TLM_OK_RESPONSE, "TLM-MTIMER-BE-01", "all-0xFF byte enable accepted");
    check(st == tlm::TLM_OK_RESPONSE, "CP-TLM-BE-MTIMER", "byte enable pass-through");

    unsigned char be_bad[4] = {0xFF, 0xFF, 0x00, 0xFF};
    st = do_mtimer_txn_be(0x7FF0, 1, be_bad, 4);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "TLM-MTIMER-BE-02", "partial byte enable rejected");
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "CP-TLM-BE-MTIMER", "partial BE rejected");
    cleanup_mtimer();
}

void TestRunner::test_tlm_sswi_byte_enable() {
    unsigned char be_ok[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    tlm::tlm_response_status st = do_sswi_txn_be(0x0000, 1, be_ok, 4);
    check(st == tlm::TLM_OK_RESPONSE, "TLM-SSWI-BE-01", "all-0xFF byte enable accepted");
    check(st == tlm::TLM_OK_RESPONSE, "CP-TLM-BE-SSWI", "byte enable pass-through");

    unsigned char be_bad[4] = {0x00, 0xFF, 0xFF, 0xFF};
    st = do_sswi_txn_be(0x0000, 1, be_bad, 4);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "TLM-SSWI-BE-02", "partial byte enable rejected");
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "CP-TLM-BE-SSWI", "partial BE rejected");
    cleanup_sswi();
}

// ═══════════════════════════════════════════════════════════════════
//  CROSS-DEVICE TESTS
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_xdev_01() {
    // MSWI write doesn't affect MTIMER or SSWI
    cleanup_mswi();
    cleanup_mtimer();
    cleanup_sswi();
    wait(sc_core::SC_ZERO_TIME);

    write32_mswi(0x0000, 1);
    wait(sc_core::SC_ZERO_TIME);
    check(sw_irq[0].read() == true, "XDEV-01-01", "sw_irq asserted via MSWI");
    for (int i = 0; i < NUM_HARTS; i++) {
        check(timer_irq[i].read() == false, "XDEV-01-02", "MTIMER unaffected by MSWI write");
        check(ssw_irq[i].read() == false, "XDEV-01-03", "SSWI unaffected by MSWI write");
    }
    cleanup_mswi();
}

void TestRunner::test_xdev_02() {
    // MTIMER write doesn't affect MSWI or SSWI
    cleanup_mswi();
    cleanup_mtimer();
    cleanup_sswi();
    wait(sc_core::SC_ZERO_TIME);

    write64_mtimer(0x7FF8, 0);
    write64_mtimer(0x0000, 5);
    wait(10, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "XDEV-02-01", "timer_irq asserted via MTIMER");
    for (int i = 0; i < NUM_HARTS; i++) {
        check(sw_irq[i].read() == false, "XDEV-02-02", "MSWI unaffected by MTIMER write");
        check(ssw_irq[i].read() == false, "XDEV-02-03", "SSWI unaffected by MTIMER write");
    }
    cleanup_mtimer();
}

void TestRunner::test_xdev_03() {
    // SSWI write doesn't affect MSWI or MTIMER
    cleanup_mswi();
    cleanup_mtimer();
    cleanup_sswi();
    wait(sc_core::SC_ZERO_TIME);

    write32_sswi(0x0000, 1);
    wait(sc_core::SC_ZERO_TIME);
    check(ssw_irq[0].read() == true, "XDEV-03-01", "ssw_irq asserted via SSWI");
    for (int i = 0; i < NUM_HARTS; i++) {
        check(sw_irq[i].read() == false, "XDEV-03-02", "MSWI unaffected by SSWI write");
        check(timer_irq[i].read() == false, "XDEV-03-03", "MTIMER unaffected by SSWI write");
    }
    cleanup_sswi();
}

void TestRunner::test_xdev_04() {
    // Simultaneous operations on all 3 devices
    cleanup_mswi();
    cleanup_mtimer();
    cleanup_sswi();
    wait(sc_core::SC_ZERO_TIME);

    write32_mswi(0x0000, 1);
    write32_mswi(0x0004, 1);
    write32_sswi(0x0008, 1);
    write32_sswi(0x000C, 1);
    write64_mtimer(0x7FF8, 0);
    write64_mtimer(0x0000, 5);
    write64_mtimer(0x0008, 5);
    wait(10, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);

    check(sw_irq[0].read() == true, "XDEV-04-01", "sw_irq[0] asserted");
    check(sw_irq[1].read() == true, "XDEV-04-02", "sw_irq[1] asserted");
    check(sw_irq[2].read() == false, "XDEV-04-03", "sw_irq[2] not asserted");
    check(ssw_irq[2].read() == true, "XDEV-04-04", "ssw_irq[2] asserted");
    check(ssw_irq[3].read() == true, "XDEV-04-05", "ssw_irq[3] asserted");
    check(ssw_irq[0].read() == false, "XDEV-04-06", "ssw_irq[0] not asserted");
    check(timer_irq[0].read() == true, "XDEV-04-07", "timer_irq[0] asserted");
    check(timer_irq[1].read() == true, "XDEV-04-08", "timer_irq[1] asserted");
    check(timer_irq[2].read() == false, "XDEV-04-09", "timer_irq[2] not asserted");

    cleanup_mswi();
    cleanup_mtimer();
    cleanup_sswi();
}

// ═══════════════════════════════════════════════════════════════════
//  EXTENDED: TIMER-ENABLE + IRQ INTERACTION
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_fn_mtimer_42_irq_while_disabled() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0);
    write64_mtimer(0x0000, 5);
    wait(10, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "FN-MTIMER-42-01", "IRQ asserted before disable");

    write32_mtimer(0x7FE8, 0);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "FN-MTIMER-42-02", "IRQ stays asserted while timer disabled");

    uint64_t frozen = read64_mtimer(0x7FF8);
    check(frozen >= 5, "FN-MTIMER-42-03", "mtime still >= mtimecmp while disabled");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_43_irq_on_mtime_write_disabled() {
    cleanup_mtimer();
    write32_mtimer(0x7FE8, 0);
    wait(sc_core::SC_ZERO_TIME);

    write64_mtimer(0x0000, 100);
    write64_mtimer(0x7FF8, 200);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "FN-MTIMER-43-01",
          "IRQ asserts on mtime write while disabled (mtime=200 >= mtimecmp=100)");

    write64_mtimer(0x7FF8, 50);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == false, "FN-MTIMER-43-02",
          "IRQ deasserts on mtime write below mtimecmp while disabled");

    check(read32_mtimer(0x7FE8) == 0, "FN-MTIMER-43-03", "timer still disabled");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_44_disable_write_reenable() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0);
    write32_mtimer(0x7FE8, 0);
    wait(sc_core::SC_ZERO_TIME);

    write64_mtimer(0x7FF8, 500);
    uint64_t v = read64_mtimer(0x7FF8);
    check(v == 500, "FN-MTIMER-44-01", "mtime written to 500 while disabled");

    write32_mtimer(0x7FE8, 1);
    wait(10, sc_core::SC_US);
    uint64_t resumed = read64_mtimer(0x7FF8);
    check(resumed > 500, "FN-MTIMER-44-02", "mtime resumed from written value");
    check(resumed >= 509, "FN-MTIMER-44-03", "mtime incremented ~10 from 500");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_45_disable_reenable_preserves() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0);
    wait(10, sc_core::SC_US);
    uint64_t before_disable = read64_mtimer(0x7FF8);
    check(before_disable >= 9, "FN-MTIMER-45-01", "mtime ran ~10 ticks");

    write32_mtimer(0x7FE8, 0);
    wait(sc_core::SC_ZERO_TIME);
    uint64_t frozen = read64_mtimer(0x7FF8);
    wait(20, sc_core::SC_US);
    uint64_t still_frozen = read64_mtimer(0x7FF8);
    check(still_frozen == frozen, "FN-MTIMER-45-02", "mtime frozen during disable");

    write32_mtimer(0x7FE8, 1);
    wait(10, sc_core::SC_US);
    uint64_t after_reenable = read64_mtimer(0x7FF8);
    check(after_reenable > frozen, "FN-MTIMER-45-03", "mtime resumes from frozen value");
    check(after_reenable < frozen + 15, "FN-MTIMER-45-04", "mtime didn't jump (no catch-up)");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_46_irq_deassert_mtimecmp_disabled() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0);
    write64_mtimer(0x0000, 5);
    wait(10, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "FN-MTIMER-46-01", "IRQ asserted (mtime >= 5)");

    write32_mtimer(0x7FE8, 0);
    wait(sc_core::SC_ZERO_TIME);

    write64_mtimer(0x0000, 0xFFFFFFFFFFFFFFFFULL);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == false, "FN-MTIMER-46-02",
          "IRQ deasserts after mtimecmp=MAX while disabled");
    check(read32_mtimer(0x7FE8) == 0, "FN-MTIMER-46-03", "timer still disabled");
    cleanup_mtimer();
}

// ═══════════════════════════════════════════════════════════════════
//  EXTENDED: OVERFLOW FLAG EDGE CASES
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_fn_mtimer_47_multi_overflow_single_flag() {
    cleanup_mtimer();
    write32_mtimer(0x7FF0, 1);
    write32_mtimer(0x7FE8, 1);
    read32_mtimer(0x7FE0);

    write64_mtimer(0x7FF8, 0xFFFFFFFFFFFFFFFDULL);
    wait(5, sc_core::SC_US);

    write64_mtimer(0x7FF8, 0xFFFFFFFFFFFFFFFDULL);
    wait(5, sc_core::SC_US);

    uint32_t of = read32_mtimer(0x7FE0);
    check(of == 1, "FN-MTIMER-47-01", "overflow_flag == 1 after two overflows (not 2)");
    uint32_t of2 = read32_mtimer(0x7FE0);
    check(of2 == 0, "FN-MTIMER-47-02", "overflow_flag cleared after read");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_48_overflow_write_ok_response() {
    cleanup_mtimer();
    tlm::tlm_response_status st = write32_mtimer_status(0x7FE0, 1);
    check(st == tlm::TLM_OK_RESPONSE, "FN-MTIMER-48-01",
          "write to overflow_flag returns TLM_OK_RESPONSE");
    st = write32_mtimer_status(0x7FE0, 0);
    check(st == tlm::TLM_OK_RESPONSE, "FN-MTIMER-48-02",
          "write 0 to overflow_flag returns TLM_OK_RESPONSE");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_49_overflow_write_no_change() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0xFFFFFFFFFFFFFFFDULL);
    wait(5, sc_core::SC_US);

    write32_mtimer(0x7FE0, 0);
    uint32_t of = read32_mtimer(0x7FE0);
    check(of == 1, "FN-MTIMER-49-01", "write 0 to overflow_flag doesn't clear set flag");

    write32_mtimer(0x7FE0, 0xFFFFFFFF);
    of = read32_mtimer(0x7FE0);
    check(of == 0, "FN-MTIMER-49-02", "flag cleared by previous read, write is no-op");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_50_no_overflow_on_sw_wrap() {
    cleanup_mtimer();
    read32_mtimer(0x7FE0);
    write32_mtimer(0x7FE8, 0);
    wait(sc_core::SC_ZERO_TIME);

    write64_mtimer(0x7FF8, 0xFFFFFFFFFFFFFFFFULL);
    write64_mtimer(0x7FF8, 0);
    uint32_t of = read32_mtimer(0x7FE0);
    check(of == 0, "FN-MTIMER-50-01",
          "software write MAX then 0 does NOT set overflow_flag");
    cleanup_mtimer();
}

// ═══════════════════════════════════════════════════════════════════
//  EXTENDED: CONTROL REGISTER WIDTH ENFORCEMENT
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_ec_mtimer_51_prescaler_8byte_read() {
    tlm::tlm_response_status st = do_mtimer_txn_len(0x7FF0, 8);
    bool is_err = (st == tlm::TLM_GENERIC_ERROR_RESPONSE ||
                   st == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    check(is_err, "EC-MTIMER-51-01",
          "8-byte read at prescaler -> error (4-byte only)");
    cleanup_mtimer();
}

void TestRunner::test_ec_mtimer_52_timer_enable_8byte_read() {
    tlm::tlm_response_status st = do_mtimer_txn_len(0x7FE8, 8);
    bool is_err = (st == tlm::TLM_GENERIC_ERROR_RESPONSE ||
                   st == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    check(is_err, "EC-MTIMER-52-01",
          "8-byte read at timer_enable -> error (4-byte only)");
    cleanup_mtimer();
}

void TestRunner::test_ec_mtimer_53_overflow_8byte_read() {
    tlm::tlm_response_status st = do_mtimer_txn_len(0x7FE0, 8);
    bool is_err = (st == tlm::TLM_GENERIC_ERROR_RESPONSE ||
                   st == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    check(is_err, "EC-MTIMER-53-01",
          "8-byte read at overflow_flag -> error (4-byte only)");
    cleanup_mtimer();
}

void TestRunner::test_ec_mtimer_54_prescaler_8byte_write() {
    tlm::tlm_response_status st = do_mtimer_txn_len_write(0x7FF0, 8);
    bool is_err = (st == tlm::TLM_GENERIC_ERROR_RESPONSE ||
                   st == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    check(is_err, "EC-MTIMER-54-01",
          "8-byte write at prescaler -> error (4-byte only)");
    cleanup_mtimer();
}

// ═══════════════════════════════════════════════════════════════════
//  EXTENDED: ADDRESS SPACE BOUNDARY
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_ec_mtimer_55_boundary_0x8000() {
    tlm::tlm_response_status st = read32_mtimer_status(0x8000);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MTIMER-55-01",
          "read at 0x8000 -> ADDRESS_ERROR (beyond 32 KiB)");
    st = write32_mtimer_status(0x8000, 0);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MTIMER-55-02",
          "write at 0x8000 -> ADDRESS_ERROR");
}

void TestRunner::test_ec_mtimer_56_8byte_at_0x7FFC() {
    tlm::tlm_response_status st = do_mtimer_txn_len(0x7FFC, 8);
    bool is_error = (st == tlm::TLM_ADDRESS_ERROR_RESPONSE ||
                     st == tlm::TLM_GENERIC_ERROR_RESPONSE);
    check(is_error, "EC-MTIMER-56-01",
          "8-byte access at 0x7FFC -> error (not 8-byte aligned)");
}

void TestRunner::test_ec_mswi_14_boundary_0x4000() {
    tlm::tlm_response_status st = read32_mswi_status(0x4000);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MSWI-14-01",
          "read at 0x4000 -> ADDRESS_ERROR (beyond 16 KiB)");
    st = write32_mswi_status(0x4000, 0);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MSWI-14-02",
          "write at 0x4000 -> ADDRESS_ERROR");
}

void TestRunner::test_ec_sswi_14_boundary_0x4000() {
    tlm::tlm_response_status st = read32_sswi_status(0x4000);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-SSWI-14-01",
          "read at 0x4000 -> ADDRESS_ERROR (beyond 16 KiB)");
    st = write32_sswi_status(0x4000, 0);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-SSWI-14-02",
          "write at 0x4000 -> ADDRESS_ERROR");
}

void TestRunner::test_ec_mswi_15_upper_reserved() {
    tlm::tlm_response_status st = read32_mswi_status(0x3FFC);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MSWI-15-01",
          "read at 0x3FFC -> ADDRESS_ERROR (reserved within 16 KiB)");
    st = read32_mswi_status(0x0100);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MSWI-15-02",
          "read at 0x0100 -> ADDRESS_ERROR (beyond hart range)");
}

// ═══════════════════════════════════════════════════════════════════
//  EXTENDED: PRESCALER TIMING & EDGE CASES
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_fn_mtimer_57_prescaler_65536_clamp() {
    write32_mtimer(0x7FF0, 65536);
    uint32_t v = read32_mtimer(0x7FF0);
    check(v == 65535, "FN-MTIMER-57-01", "prescaler WARL: 65536 clamped to 65535");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_58_prescaler_mid_operation() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0);
    write32_mtimer(0x7FF0, 1);
    wait(10, sc_core::SC_US);
    uint64_t fast = read64_mtimer(0x7FF8);

    write32_mtimer(0x7FF0, 50);
    uint64_t before = read64_mtimer(0x7FF8);
    wait(200, sc_core::SC_US);
    uint64_t after = read64_mtimer(0x7FF8);
    uint64_t delta = after - before;
    check(delta >= 2, "FN-MTIMER-58-01", "timer still increments after prescaler change");
    check(delta <= 10, "FN-MTIMER-58-02",
          "prescaler=50 slows increments (~4 in 200us vs ~200 at ps=1)");
    check(fast > delta, "FN-MTIMER-58-03", "prescaler=1 faster than prescaler=50");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_59_prescaler1_tick_count() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0);
    write32_mtimer(0x7FF0, 1);
    wait(10, sc_core::SC_US);
    uint64_t count = read64_mtimer(0x7FF8);
    check(count >= 9, "FN-MTIMER-59-01", "prescaler=1: at least 9 ticks in 10us");
    check(count <= 11, "FN-MTIMER-59-02", "prescaler=1: at most 11 ticks in 10us");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_60_prescaler_change_while_disabled() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0);
    write32_mtimer(0x7FE8, 0);
    wait(sc_core::SC_ZERO_TIME);

    write32_mtimer(0x7FF0, 100);
    uint32_t ps = read32_mtimer(0x7FF0);
    check(ps == 100, "FN-MTIMER-60-01", "prescaler writable while disabled");

    write32_mtimer(0x7FE8, 1);
    uint64_t before = read64_mtimer(0x7FF8);
    wait(500, sc_core::SC_US);
    uint64_t after = read64_mtimer(0x7FF8);
    uint64_t delta = after - before;
    check(delta >= 3, "FN-MTIMER-60-02", "prescaler=100: some ticks in 500us");
    check(delta <= 8, "FN-MTIMER-60-03", "prescaler=100: ~5 ticks in 500us");
    cleanup_mtimer();
}

// ═══════════════════════════════════════════════════════════════════
//  EXTENDED: SPLIT ACCESS EDGE CASES
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_fn_mtimer_61_mtime_split_lo_preserves_hi() {
    cleanup_mtimer();
    write32_mtimer(0x7FE8, 0);
    wait(sc_core::SC_ZERO_TIME);

    write64_mtimer(0x7FF8, 0x1111111122222222ULL);
    write32_mtimer(0x7FF8, 0xAABBCCDD);
    uint32_t hi = read32_mtimer(0x7FFC);
    uint32_t lo = read32_mtimer(0x7FF8);
    check(lo == 0xAABBCCDD, "FN-MTIMER-61-01", "mtime lo updated");
    check(hi == 0x11111111, "FN-MTIMER-61-02", "mtime hi preserved after lo write");
    uint64_t full = read64_mtimer(0x7FF8);
    check(full == 0x11111111AABBCCDDULL, "FN-MTIMER-61-03", "full 64-bit readback correct");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_62_mtime_split_hi_preserves_lo() {
    cleanup_mtimer();
    write32_mtimer(0x7FE8, 0);
    wait(sc_core::SC_ZERO_TIME);

    write64_mtimer(0x7FF8, 0x1111111122222222ULL);
    write32_mtimer(0x7FFC, 0xDDCCBBAA);
    uint32_t lo = read32_mtimer(0x7FF8);
    uint32_t hi = read32_mtimer(0x7FFC);
    check(lo == 0x22222222, "FN-MTIMER-62-01", "mtime lo preserved after hi write");
    check(hi == 0xDDCCBBAA, "FN-MTIMER-62-02", "mtime hi updated");
    uint64_t full = read64_mtimer(0x7FF8);
    check(full == 0xDDCCBBAA22222222ULL, "FN-MTIMER-62-03", "full 64-bit readback correct");
    cleanup_mtimer();
}

void TestRunner::test_fn_mtimer_63_mtimecmp_last_hart_split() {
    cleanup_mtimer();
    uint64_t offset = (NUM_HARTS - 1) * 8;

    write32_mtimer(offset, 0xDEADBEEF);
    write32_mtimer(offset + 4, 0xCAFEBABE);
    uint64_t full = read64_mtimer(offset);
    check(full == 0xCAFEBABEDEADBEEFULL, "FN-MTIMER-63-01",
          "last hart mtimecmp split write full readback");

    uint32_t lo = read32_mtimer(offset);
    check(lo == 0xDEADBEEF, "FN-MTIMER-63-02", "last hart mtimecmp lo readback");
    uint32_t hi = read32_mtimer(offset + 4);
    check(hi == 0xCAFEBABE, "FN-MTIMER-63-03", "last hart mtimecmp hi readback");
    cleanup_mtimer();
}

// ═══════════════════════════════════════════════════════════════════
//  EXTENDED: IRQ PRECISION
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_ir_mtimer_64_exact_equality() {
    cleanup_mtimer();
    write32_mtimer(0x7FE8, 0);
    wait(sc_core::SC_ZERO_TIME);

    write64_mtimer(0x0000, 100);
    write64_mtimer(0x7FF8, 100);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "IR-MTIMER-64-01",
          "IRQ asserted when mtime == mtimecmp (exact equality)");

    write64_mtimer(0x7FF8, 99);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == false, "IR-MTIMER-64-02",
          "IRQ deasserted when mtime < mtimecmp by 1");
    cleanup_mtimer();
}

void TestRunner::test_ir_mtimer_65_irq_reassert_mtimecmp_lowered() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0);
    write64_mtimer(0x0000, 0xFFFFFFFFFFFFFFFFULL);
    wait(10, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == false, "IR-MTIMER-65-01",
          "IRQ off with mtimecmp=MAX");

    uint64_t current = read64_mtimer(0x7FF8);
    write64_mtimer(0x0000, current / 2);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "IR-MTIMER-65-02",
          "IRQ re-asserts when mtimecmp lowered below current mtime");
    cleanup_mtimer();
}

void TestRunner::test_ir_mtimer_66_per_hart_irq_independence() {
    cleanup_mtimer();
    write32_mtimer(0x7FE8, 0);
    wait(sc_core::SC_ZERO_TIME);

    write64_mtimer(0x7FF8, 1000);
    write64_mtimer(0x0000, 500);
    write64_mtimer((NUM_HARTS - 1) * 8, 0xFFFFFFFFFFFFFFFFULL);
    wait(sc_core::SC_ZERO_TIME);

    check(timer_irq[0].read() == true, "IR-MTIMER-66-01",
          "hart 0 IRQ asserted (mtime=1000 >= mtimecmp=500)");
    check(timer_irq[NUM_HARTS - 1].read() == false, "IR-MTIMER-66-02",
          "last hart IRQ not asserted (mtimecmp=MAX)");

    write64_mtimer(0x0000, 0xFFFFFFFFFFFFFFFFULL);
    write64_mtimer((NUM_HARTS - 1) * 8, 500);
    wait(sc_core::SC_ZERO_TIME);

    check(timer_irq[0].read() == false, "IR-MTIMER-66-03",
          "hart 0 IRQ deasserted after mtimecmp=MAX");
    check(timer_irq[NUM_HARTS - 1].read() == true, "IR-MTIMER-66-04",
          "last hart IRQ asserted after mtimecmp=500");
    cleanup_mtimer();
}

void TestRunner::test_ir_mtimer_67_overflow_irq() {
    cleanup_mtimer();
    write64_mtimer(0x0000, 0);
    write64_mtimer(0x7FF8, 0xFFFFFFFFFFFFFFFDULL);
    wait(5, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);

    uint64_t mtime_now = read64_mtimer(0x7FF8);
    check(mtime_now < 0xFFFFFFFFFFFFFFFDULL, "IR-MTIMER-67-01",
          "mtime wrapped past 0 (overflow occurred)");
    check(timer_irq[0].read() == true, "IR-MTIMER-67-02",
          "IRQ asserted after overflow (mtime >= mtimecmp=0)");

    uint32_t of = read32_mtimer(0x7FE0);
    check(of == 1, "IR-MTIMER-67-03", "overflow_flag set");
    cleanup_mtimer();
}

// ═══════════════════════════════════════════════════════════════════
//  EXTENDED: ERROR HANDLING EDGE CASES
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_ec_mtimer_68_gap_writes() {
    tlm::tlm_response_status st;
    st = write32_mtimer_status(0x7FE4, 0);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MTIMER-68-01",
          "write to gap 0x7FE4 -> ADDRESS_ERROR");
    st = write32_mtimer_status(0x7FEC, 0);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MTIMER-68-02",
          "write to gap 0x7FEC -> ADDRESS_ERROR");
    st = write32_mtimer_status(0x7FF4, 0);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MTIMER-68-03",
          "write to gap 0x7FF4 -> ADDRESS_ERROR");
}

void TestRunner::test_ec_mtimer_69_8byte_unaligned() {
    tlm::tlm_response_status st = do_mtimer_txn_len(0x0004, 8);
    bool is_error = (st == tlm::TLM_ADDRESS_ERROR_RESPONSE ||
                     st == tlm::TLM_GENERIC_ERROR_RESPONSE);
    check(is_error, "EC-MTIMER-69-01",
          "8-byte access at 0x0004 (not 8-byte aligned) -> error");
}

void TestRunner::test_ec_mswi_16_write_beyond_harts() {
    tlm::tlm_response_status st = write32_mswi_status(0x0100, 1);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MSWI-16-01",
          "write at 0x0100 -> ADDRESS_ERROR (beyond 8-hart range)");
    st = write32_mswi_status(0x0020, 1);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-MSWI-16-02",
          "write at 0x0020 -> ADDRESS_ERROR (exactly past last hart)");
}

// ═══════════════════════════════════════════════════════════════════
//  NEW: TLM_IGNORE_COMMAND TESTS
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_tlm_mswi_ignore_cmd() {
    cleanup_mswi();
    uint32_t val = 0;
    tlm::tlm_response_status st = do_txn(mswi_socket, 0x0000,
        reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_IGNORE_COMMAND);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "TLM-MSWI-IGN-01",
          "TLM_IGNORE_COMMAND → GENERIC_ERROR");
    uint32_t reg = read32_mswi(0x0000);
    check(reg == 0, "TLM-MSWI-IGN-02", "MSIP[0] unchanged after IGNORE_COMMAND");
    check(sw_irq[0].read() == false, "TLM-MSWI-IGN-03",
          "sw_irq[0] unaffected by IGNORE_COMMAND");
}

void TestRunner::test_tlm_mtimer_ignore_cmd() {
    cleanup_mtimer();
    write32_mtimer(0x7FE8, 0); // disable timer to prevent race
    wait(sc_core::SC_ZERO_TIME);
    uint64_t val = 0;
    tlm::tlm_response_status st = do_txn(mtimer_socket, 0x0000,
        reinterpret_cast<unsigned char*>(&val), 8, tlm::TLM_IGNORE_COMMAND);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "TLM-MTIMER-IGN-01",
          "TLM_IGNORE_COMMAND → GENERIC_ERROR");
    uint64_t cmp = read64_mtimer(0x0000);
    check(cmp == 0xFFFFFFFFFFFFFFFFULL, "TLM-MTIMER-IGN-02",
          "mtimecmp[0] unchanged after IGNORE_COMMAND");
    check(timer_irq[0].read() == false, "TLM-MTIMER-IGN-03",
          "timer_irq[0] unaffected by IGNORE_COMMAND");
}

void TestRunner::test_tlm_sswi_ignore_cmd() {
    cleanup_sswi();
    uint32_t val = 0;
    tlm::tlm_response_status st = do_txn(sswi_socket, 0x0000,
        reinterpret_cast<unsigned char*>(&val), 4, tlm::TLM_IGNORE_COMMAND);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "TLM-SSWI-IGN-01",
          "TLM_IGNORE_COMMAND → GENERIC_ERROR");
    uint32_t reg = read32_sswi(0x0000);
    check(reg == 0, "TLM-SSWI-IGN-02", "SETSSIP[0] unchanged after IGNORE_COMMAND");
    check(ssw_irq[0].read() == false, "TLM-SSWI-IGN-03",
          "ssw_irq[0] unaffected by IGNORE_COMMAND");
}

// ═══════════════════════════════════════════════════════════════════
//  NEW: ERROR PRIORITY (length before address)
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_ec_error_priority() {
    tlm::tlm_response_status st;
    // MSWI: invalid len=2 at OOR address 0x0020 → len checked first → GENERIC_ERROR
    st = do_mswi_txn_len(0x0020, 2);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-PRIO-01",
          "MSWI: len=2 at OOR addr → GENERIC_ERROR (len priority)");
    // MSWI: invalid len=3 at unaligned addr → GENERIC_ERROR
    st = do_mswi_txn_len(0x0003, 3);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-PRIO-02",
          "MSWI: len=3 at unaligned → GENERIC_ERROR (len priority)");
    // MTIMER: invalid len=3 at gap addr 0x0040 → GENERIC_ERROR
    st = do_mtimer_txn_len(0x0040, 3);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-PRIO-03",
          "MTIMER: len=3 at gap addr → GENERIC_ERROR (len priority)");
    // MTIMER: invalid len=5 at unaligned 0x0001 → GENERIC_ERROR
    st = do_mtimer_txn_len(0x0001, 5);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-PRIO-04",
          "MTIMER: len=5 at unaligned → GENERIC_ERROR (len priority)");
    // SSWI: invalid len=2 at OOR addr 0x0020 → GENERIC_ERROR
    st = do_sswi_txn_len(0x0020, 2);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-PRIO-05",
          "SSWI: len=2 at OOR addr → GENERIC_ERROR (len priority)");
    // SSWI: invalid len=1 at unaligned 0x0003 → GENERIC_ERROR
    st = do_sswi_txn_len(0x0003, 1);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-PRIO-06",
          "SSWI: len=1 at unaligned → GENERIC_ERROR (len priority)");
}

// ═══════════════════════════════════════════════════════════════════
//  NEW: ERROR PRIORITY — STREAMING_WIDTH OVERLAP
//  Spec §7 priority: IGNORE > streaming_width > len > 8-byte ext > addr
//  streaming_width != data_length is checked as part of the generic
//  protocol validation and should yield GENERIC_ERROR regardless of
//  whether additional errors (bad address, bad data_length) coexist.
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_ec_error_priority_sw() {
    tlm::tlm_response_status st;
    uint32_t val = 0;

    // EC-PRIO-SW-01: MSWI streaming_width=2 AND out-of-range address 0x0020
    // Both streaming_width error and address error present;
    // streaming_width should be caught first → GENERIC_ERROR (not ADDRESS_ERROR)
    st = do_txn_sw(mswi_socket, 0x0020, reinterpret_cast<unsigned char*>(&val),
                   4, tlm::TLM_READ_COMMAND, 2);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-PRIO-SW-01",
          "MSWI: streaming_width=2 + OOR addr 0x0020 → GENERIC_ERROR (sw priority over addr)");

    // EC-PRIO-SW-02: MTIMER streaming_width=1 AND out-of-range address 0x8000
    // streaming_width error should win over address error
    st = do_txn_sw(mtimer_socket, 0x8000, reinterpret_cast<unsigned char*>(&val),
                   4, tlm::TLM_READ_COMMAND, 1);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-PRIO-SW-02",
          "MTIMER: streaming_width=1 + OOR addr 0x8000 → GENERIC_ERROR (sw priority over addr)");

    // EC-PRIO-SW-03: SSWI streaming_width=2 AND invalid data_length=2
    // Both streaming_width != data_length AND data_length != 4 are errors.
    // Either way GENERIC_ERROR, but this confirms both conditions don't
    // cause unexpected behavior (e.g., ADDRESS_ERROR or crash).
    st = do_txn_sw(sswi_socket, 0x0000, reinterpret_cast<unsigned char*>(&val),
                   2, tlm::TLM_READ_COMMAND, 1);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-PRIO-SW-03",
          "SSWI: streaming_width=1 + len=2 → GENERIC_ERROR (both invalid, no crash)");
}

// ═══════════════════════════════════════════════════════════════════
//  NEW: EXTENSION REGISTER 8-BYTE WRITES
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_ec_mtimer_ext_8byte_writes() {
    tlm::tlm_response_status st;
    // 8-byte write to timer_enable
    st = do_mtimer_txn_len_write(0x7FE8, 8);
    bool is_err = (st == tlm::TLM_GENERIC_ERROR_RESPONSE ||
                   st == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    check(is_err, "EC-MTIMER-EXT-8W-01",
          "8-byte write at timer_enable → error");
    // 8-byte write to overflow_flag
    st = do_mtimer_txn_len_write(0x7FE0, 8);
    is_err = (st == tlm::TLM_GENERIC_ERROR_RESPONSE ||
              st == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    check(is_err, "EC-MTIMER-EXT-8W-02",
          "8-byte write at overflow_flag → error");
    // Verify prescaler still reads 1 (not corrupted)
    uint32_t ps = read32_mtimer(0x7FF0);
    check(ps == 1, "EC-MTIMER-EXT-8W-03",
          "prescaler unchanged after failed 8-byte writes");
    cleanup_mtimer();
}

// ═══════════════════════════════════════════════════════════════════
//  NEW: TIMER-ENABLE 0→1 MULTI-HART RE-EVALUATION
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_fn_mtimer_70_reenable_all_irq() {
    char tag[64];
    cleanup_mtimer();
    // Disable timer
    write32_mtimer(0x7FE8, 0);
    wait(sc_core::SC_ZERO_TIME);
    // Set mtime = 1000 while disabled
    write64_mtimer(0x7FF8, 1000);
    // Set all mtimecmp[0..7] = 500 (all below mtime=1000)
    for (int i = 0; i < NUM_HARTS; i++)
        write64_mtimer(i * 8, 500);
    wait(sc_core::SC_ZERO_TIME);
    // Re-enable timer — all IRQs should immediately re-evaluate
    write32_mtimer(0x7FE8, 1);
    wait(sc_core::SC_ZERO_TIME);
    // Verify all 8 timer_irq asserted
    for (int i = 0; i < NUM_HARTS; i++) {
        snprintf(tag, sizeof(tag), "FN-MTIMER-70-%02d", i + 1);
        check(timer_irq[i].read() == true, tag,
              "timer_irq asserted after re-enable (mtime >= mtimecmp)");
    }
    // Verify timer_enable = 1
    check(read32_mtimer(0x7FE8) == 1, "FN-MTIMER-70-09",
          "timer_enable reads 1 after re-enable");
    // Verify mtime still >= 1000
    uint64_t mt = read64_mtimer(0x7FF8);
    check(mt >= 1000, "FN-MTIMER-70-10", "mtime >= 1000 after re-enable");
    cleanup_mtimer();
}

// ═══════════════════════════════════════════════════════════════════
//  NEW: LITTLE-ENDIAN BYTE ORDERING
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_tlm_le_byte_order() {
    cleanup_mtimer();
    // Write known pattern to mtimecmp[0] lo word
    write32_mtimer(0x0000, 0xDEADCAFE);
    // Read via debug transport as raw bytes
    tlm::tlm_generic_payload trans;
    unsigned char buf[4] = {0};
    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(0x0000);
    trans.set_data_ptr(buf);
    trans.set_data_length(4);
    mtimer_socket->transport_dbg(trans);
    // Verify LE byte order: 0xDEADCAFE → [0xFE, 0xCA, 0xAD, 0xDE]
    check(buf[0] == 0xFE, "TLM-LE-01", "byte[0] = 0xFE (LE LSB)");
    check(buf[1] == 0xCA, "TLM-LE-02", "byte[1] = 0xCA");
    check(buf[2] == 0xAD, "TLM-LE-03", "byte[2] = 0xAD");
    check(buf[3] == 0xDE, "TLM-LE-04", "byte[3] = 0xDE (LE MSB)");

    // Write known pattern to mtimecmp[0] hi word
    write32_mtimer(0x0004, 0x12345678);
    unsigned char buf2[4] = {0};
    trans.set_address(0x0004);
    trans.set_data_ptr(buf2);
    mtimer_socket->transport_dbg(trans);
    // Verify: 0x12345678 → [0x78, 0x56, 0x34, 0x12]
    check(buf2[0] == 0x78, "TLM-LE-05", "hi byte[0] = 0x78 (LE LSB)");
    check(buf2[1] == 0x56, "TLM-LE-06", "hi byte[1] = 0x56");
    check(buf2[2] == 0x34, "TLM-LE-07", "hi byte[2] = 0x34");
    check(buf2[3] == 0x12, "TLM-LE-08", "hi byte[3] = 0x12 (LE MSB)");
    cleanup_mtimer();
}

// ═══════════════════════════════════════════════════════════════════
//  NEW: SSWI UPPER RESERVED AREA
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_ec_sswi_15_upper_reserved() {
    tlm::tlm_response_status st;
    st = read32_sswi_status(0x3FFC);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-SSWI-15-01",
          "read at 0x3FFC → ADDRESS_ERROR (reserved within 16 KiB)");
    st = read32_sswi_status(0x0100);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-SSWI-15-02",
          "read at 0x0100 → ADDRESS_ERROR (beyond hart range)");
    st = write32_sswi_status(0x3FFC, 0);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-SSWI-15-03",
          "write at 0x3FFC → ADDRESS_ERROR");
    st = write32_sswi_status(0x0100, 1);
    check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE, "EC-SSWI-15-04",
          "write at 0x0100 → ADDRESS_ERROR");
}

// ═══════════════════════════════════════════════════════════════════
//  NEW: MTIME WRAP WITH IRQ
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_fn_mtimer_71_mtime_wrap_irq() {
    cleanup_mtimer();
    // Clear overflow
    read32_mtimer(0x7FE0);
    // Set mtimecmp[0] = 0 (always satisfied when mtime >= 0)
    write64_mtimer(0x0000, 0);
    // Set mtime near MAX so it wraps quickly
    write64_mtimer(0x7FF8, 0xFFFFFFFFFFFFFFFCULL);
    wait(10, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);

    // After wrap, mtime should be small
    uint64_t mt = read64_mtimer(0x7FF8);
    check(mt < 0xFFFFFFFFFFFFFFF0ULL, "FN-MTIMER-71-01",
          "mtime wrapped (no longer near MAX)");
    // Overflow flag should be set
    uint32_t of = read32_mtimer(0x7FE0);
    check(of == 1, "FN-MTIMER-71-02", "overflow_flag set after wrap");
    // timer_irq[0] should be asserted (mtimecmp=0, mtime >= 0 always true)
    check(timer_irq[0].read() == true, "FN-MTIMER-71-03",
          "timer_irq asserted after wrap (mtimecmp=0)");
    // overflow_flag cleared by read
    uint32_t of2 = read32_mtimer(0x7FE0);
    check(of2 == 0, "FN-MTIMER-71-04", "overflow_flag cleared by previous read");
    // Verify mtime is still incrementing (small value)
    check(mt < 20, "FN-MTIMER-71-05", "mtime is a small value after wrap");
    // Timer still running
    uint64_t mt2 = read64_mtimer(0x7FF8);
    check(mt2 >= mt, "FN-MTIMER-71-06", "mtime still incrementing after wrap");
    cleanup_mtimer();
}

// ═══════════════════════════════════════════════════════════════════
//  NEW: PRESCALER BOUNDARY VALUES
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_fn_mtimer_72_prescaler_boundaries() {
    cleanup_mtimer();
    // prescaler=1 (minimum valid)
    write32_mtimer(0x7FF0, 1);
    check(read32_mtimer(0x7FF0) == 1, "FN-MTIMER-72-01", "prescaler=1 readback");
    // prescaler=2
    write32_mtimer(0x7FF0, 2);
    check(read32_mtimer(0x7FF0) == 2, "FN-MTIMER-72-02", "prescaler=2 readback");
    // prescaler=32767
    write32_mtimer(0x7FF0, 32767);
    check(read32_mtimer(0x7FF0) == 32767, "FN-MTIMER-72-03", "prescaler=32767 readback");
    // prescaler=32768
    write32_mtimer(0x7FF0, 32768);
    check(read32_mtimer(0x7FF0) == 32768, "FN-MTIMER-72-04", "prescaler=32768 readback");
    // prescaler=65534
    write32_mtimer(0x7FF0, 65534);
    check(read32_mtimer(0x7FF0) == 65534, "FN-MTIMER-72-05", "prescaler=65534 readback");
    // prescaler=65535 (maximum valid)
    write32_mtimer(0x7FF0, 65535);
    check(read32_mtimer(0x7FF0) == 65535, "FN-MTIMER-72-06", "prescaler=65535 readback");
    // Verify prescaler=2 gives half tick rate vs prescaler=1
    write32_mtimer(0x7FF0, 1);
    write64_mtimer(0x7FF8, 0);
    wait(10, sc_core::SC_US);
    uint64_t count_ps1 = read64_mtimer(0x7FF8);
    check(count_ps1 >= 9, "FN-MTIMER-72-07", "prescaler=1: ~10 ticks in 10us");
    write32_mtimer(0x7FF0, 2);
    write64_mtimer(0x7FF8, 0);
    wait(10, sc_core::SC_US);
    uint64_t count_ps2 = read64_mtimer(0x7FF8);
    check(count_ps2 <= count_ps1 / 2 + 1, "FN-MTIMER-72-08",
          "prescaler=2: ~half the ticks of prescaler=1");
    cleanup_mtimer();
}

// ═══════════════════════════════════════════════════════════════════
//  NEW: OVERFLOW NO SPURIOUS
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_fn_mtimer_73_overflow_no_spurious() {
    cleanup_mtimer();
    // Clear any pending overflow
    read32_mtimer(0x7FE0);
    // Read overflow → should be 0
    uint32_t of = read32_mtimer(0x7FE0);
    check(of == 0, "FN-MTIMER-73-01", "overflow_flag = 0 (no overflow)");
    // Wait briefly (mtime starts from 0, no overflow possible)
    wait(5, sc_core::SC_US);
    of = read32_mtimer(0x7FE0);
    check(of == 0, "FN-MTIMER-73-02", "overflow_flag still 0 (no spurious set)");
    // Third consecutive read also 0
    of = read32_mtimer(0x7FE0);
    check(of == 0, "FN-MTIMER-73-03", "overflow_flag third read still 0");
    // Now trigger overflow
    write64_mtimer(0x7FF8, 0xFFFFFFFFFFFFFFFDULL);
    wait(5, sc_core::SC_US);
    of = read32_mtimer(0x7FE0);
    check(of == 1, "FN-MTIMER-73-04", "overflow_flag = 1 after actual overflow");
    cleanup_mtimer();
}

// ═══════════════════════════════════════════════════════════════════
//  NEW: SAFE UPDATE NO SPURIOUS IRQ
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_fn_mtimer_74_safe_update_no_spurious() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0);
    write64_mtimer(0x0000, 0xFFFFFFFFFFFFFFFFULL);
    wait(sc_core::SC_ZERO_TIME);
    // IRQ off initially (mtimecmp=MAX)
    check(timer_irq[0].read() == false, "FN-MTIMER-74-01",
          "IRQ off initially (mtimecmp=MAX)");
    // Safe update step 1: write hi=0xFFFFFFFF (keeps mtimecmp high)
    write32_mtimer(0x0004, 0xFFFFFFFF);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == false, "FN-MTIMER-74-02",
          "IRQ still off after writing hi=MAX");
    // Safe update step 2: write lo=50
    write32_mtimer(0x0000, 50);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == false, "FN-MTIMER-74-03",
          "IRQ still off (hi=0xFFFFFFFF so mtimecmp very large)");
    // Safe update step 3: write hi=0x00000000 (now mtimecmp=50)
    write32_mtimer(0x0004, 0x00000000);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == false, "FN-MTIMER-74-04",
          "IRQ off (mtime ~0 < mtimecmp=50)");
    // Wait for mtime >= 50
    wait(55, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "FN-MTIMER-74-05",
          "IRQ asserted after mtime >= 50");
    // Verify mtimecmp readback
    uint64_t cmp = read64_mtimer(0x0000);
    check(cmp == 50, "FN-MTIMER-74-06", "mtimecmp[0] = 50 after safe update");
    cleanup_mtimer();
}

// ═══════════════════════════════════════════════════════════════════
//  NEW: ZERO DATA LENGTH
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_ec_zero_data_length() {
    tlm::tlm_response_status st;
    uint32_t val = 0;
    // MSWI: len=0 read
    st = do_mswi_txn_len(0x0000, 0);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-ZEROLEN-01",
          "MSWI: len=0 read → GENERIC_ERROR");
    // MSWI: len=0 write
    st = do_txn(mswi_socket, 0x0000, reinterpret_cast<unsigned char*>(&val),
                0, tlm::TLM_WRITE_COMMAND);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-ZEROLEN-02",
          "MSWI: len=0 write → GENERIC_ERROR");
    // MTIMER: len=0 read
    st = do_mtimer_txn_len(0x0000, 0);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-ZEROLEN-03",
          "MTIMER: len=0 read → GENERIC_ERROR");
    // MTIMER: len=0 write
    st = do_txn(mtimer_socket, 0x0000, reinterpret_cast<unsigned char*>(&val),
                0, tlm::TLM_WRITE_COMMAND);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-ZEROLEN-04",
          "MTIMER: len=0 write → GENERIC_ERROR");
    // SSWI: len=0 read
    st = do_sswi_txn_len(0x0000, 0);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-ZEROLEN-05",
          "SSWI: len=0 read → GENERIC_ERROR");
    // SSWI: len=0 write
    st = do_txn(sswi_socket, 0x0000, reinterpret_cast<unsigned char*>(&val),
                0, tlm::TLM_WRITE_COMMAND);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "EC-ZEROLEN-06",
          "SSWI: len=0 write → GENERIC_ERROR");
}

// ═══════════════════════════════════════════════════════════════════
//  NEW: STREAMING WIDTH
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_tlm_streaming_width() {
    tlm::tlm_response_status st;
    uint32_t val = 0;
    // MSWI: streaming_width=2 (≠ data_length=4)
    st = do_txn_sw(mswi_socket, 0x0000, reinterpret_cast<unsigned char*>(&val),
                   4, tlm::TLM_READ_COMMAND, 2);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "TLM-SW-01",
          "MSWI: streaming_width=2 ≠ len=4 → GENERIC_ERROR");
    // MSWI: streaming_width=1
    st = do_txn_sw(mswi_socket, 0x0000, reinterpret_cast<unsigned char*>(&val),
                   4, tlm::TLM_READ_COMMAND, 1);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "TLM-SW-02",
          "MSWI: streaming_width=1 → GENERIC_ERROR");
    // MTIMER: streaming_width=2 (≠ data_length=4)
    st = do_txn_sw(mtimer_socket, 0x7FF0, reinterpret_cast<unsigned char*>(&val),
                   4, tlm::TLM_READ_COMMAND, 2);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "TLM-SW-03",
          "MTIMER: streaming_width=2 ≠ len=4 → GENERIC_ERROR");
    // MTIMER: streaming_width=4 with data_length=8 for mtime
    uint64_t val64 = 0;
    st = do_txn_sw(mtimer_socket, 0x7FF8, reinterpret_cast<unsigned char*>(&val64),
                   8, tlm::TLM_READ_COMMAND, 4);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "TLM-SW-04",
          "MTIMER: streaming_width=4 ≠ len=8 → GENERIC_ERROR");
    // SSWI: streaming_width=2
    st = do_txn_sw(sswi_socket, 0x0000, reinterpret_cast<unsigned char*>(&val),
                   4, tlm::TLM_READ_COMMAND, 2);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "TLM-SW-05",
          "SSWI: streaming_width=2 ≠ len=4 → GENERIC_ERROR");
    // SSWI: streaming_width=1
    st = do_txn_sw(sswi_socket, 0x0000, reinterpret_cast<unsigned char*>(&val),
                   4, tlm::TLM_WRITE_COMMAND, 1);
    check(st == tlm::TLM_GENERIC_ERROR_RESPONSE, "TLM-SW-06",
          "SSWI: streaming_width=1 ≠ len=4 → GENERIC_ERROR");
}

// ═══════════════════════════════════════════════════════════════════
//  NEW: LEVEL-SENSITIVE IRQ PERSISTENCE
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_fn_level_sensitive_persistence() {
    cleanup_mswi();
    cleanup_sswi();
    // MSWI: write 1, verify IRQ persists over multiple delta cycles
    write32_mswi(0x0000, 1);
    wait(sc_core::SC_ZERO_TIME);
    check(sw_irq[0].read() == true, "FN-LEVEL-01",
          "MSWI: sw_irq persists after 1st delta");
    wait(sc_core::SC_ZERO_TIME);
    check(sw_irq[0].read() == true, "FN-LEVEL-02",
          "MSWI: sw_irq persists after 2nd delta");
    wait(sc_core::SC_ZERO_TIME);
    check(sw_irq[0].read() == true, "FN-LEVEL-03",
          "MSWI: sw_irq persists after 3rd delta");
    cleanup_mswi();

    // SSWI: same pattern
    write32_sswi(0x0000, 1);
    wait(sc_core::SC_ZERO_TIME);
    check(ssw_irq[0].read() == true, "FN-LEVEL-04",
          "SSWI: ssw_irq persists after 1st delta");
    wait(sc_core::SC_ZERO_TIME);
    check(ssw_irq[0].read() == true, "FN-LEVEL-05",
          "SSWI: ssw_irq persists after 2nd delta");
    wait(sc_core::SC_ZERO_TIME);
    check(ssw_irq[0].read() == true, "FN-LEVEL-06",
          "SSWI: ssw_irq persists after 3rd delta");
    cleanup_sswi();
}

// ═══════════════════════════════════════════════════════════════════
//  NEW: RESPONSE STATUS ALWAYS SET
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_tlm_response_status_set() {
    tlm::tlm_response_status st;
    // MSWI: valid read → TLM_OK_RESPONSE
    st = read32_mswi_status(0x0000);
    check(st == tlm::TLM_OK_RESPONSE, "TLM-RSP-01",
          "MSWI: valid read → TLM_OK_RESPONSE");
    // MSWI: valid write → TLM_OK_RESPONSE
    st = write32_mswi_status(0x0000, 0);
    check(st == tlm::TLM_OK_RESPONSE, "TLM-RSP-02",
          "MSWI: valid write → TLM_OK_RESPONSE");
    // MTIMER: valid read → TLM_OK_RESPONSE
    st = read32_mtimer_status(0x7FF0);
    check(st == tlm::TLM_OK_RESPONSE, "TLM-RSP-03",
          "MTIMER: valid read (prescaler) → TLM_OK_RESPONSE");
    // MTIMER: valid write → TLM_OK_RESPONSE
    st = write32_mtimer_status(0x7FF0, 1);
    check(st == tlm::TLM_OK_RESPONSE, "TLM-RSP-04",
          "MTIMER: valid write (prescaler) → TLM_OK_RESPONSE");
    // SSWI: valid read → TLM_OK_RESPONSE
    st = read32_sswi_status(0x0000);
    check(st == tlm::TLM_OK_RESPONSE, "TLM-RSP-05",
          "SSWI: valid read → TLM_OK_RESPONSE");
    // SSWI: valid write → TLM_OK_RESPONSE
    st = write32_sswi_status(0x0000, 0);
    check(st == tlm::TLM_OK_RESPONSE, "TLM-RSP-06",
          "SSWI: valid write → TLM_OK_RESPONSE");
}

// ═══════════════════════════════════════════════════════════════════
//  NEW: SHARED MTIME ACROSS HARTS
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_fn_mtimer_75_shared_mtime() {
    cleanup_mtimer();
    write64_mtimer(0x7FF8, 0);
    // Set mtimecmp[0]=5, mtimecmp[1]=10, mtimecmp[2]=15, others=MAX
    write64_mtimer(0x0000, 5);
    write64_mtimer(0x0008, 10);
    write64_mtimer(0x0010, 15);
    for (int i = 3; i < NUM_HARTS; i++)
        write64_mtimer(i * 8, 0xFFFFFFFFFFFFFFFFULL);

    // Wait ~7us (mtime~7): irq[0]=true (7>=5), irq[1]=false (7<10)
    wait(7, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "FN-MTIMER-75-01",
          "mtime~7: irq[0] on (>=5)");
    check(timer_irq[1].read() == false, "FN-MTIMER-75-02",
          "mtime~7: irq[1] off (<10)");
    check(timer_irq[2].read() == false, "FN-MTIMER-75-03",
          "mtime~7: irq[2] off (<15)");

    // Wait ~5 more us (mtime~12): irq[0]=true, irq[1]=true (12>=10), irq[2]=false
    wait(5, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "FN-MTIMER-75-04",
          "mtime~12: irq[0] on (>=5)");
    check(timer_irq[1].read() == true, "FN-MTIMER-75-05",
          "mtime~12: irq[1] on (>=10)");

    // Wait ~5 more us (mtime~17): all three on
    wait(5, sc_core::SC_US);
    wait(sc_core::SC_ZERO_TIME);
    check(timer_irq[0].read() == true, "FN-MTIMER-75-06",
          "mtime~17: irq[0] on (>=5)");
    check(timer_irq[1].read() == true, "FN-MTIMER-75-07",
          "mtime~17: irq[1] on (>=10)");
    check(timer_irq[2].read() == true, "FN-MTIMER-75-08",
          "mtime~17: irq[2] on (>=15)");
    cleanup_mtimer();
}

// ═══════════════════════════════════════════════════════════════════
//  NEW: DMI COHERENCY
// ═══════════════════════════════════════════════════════════════════

void TestRunner::test_tlm_dmi_coherency() {
    // --- MSWI DMI coherency ---
    cleanup_mswi();
    tlm::tlm_generic_payload trans;
    tlm::tlm_dmi dmi_data;
    trans.set_address(0x0000);
    trans.set_data_length(4);
    bool ok = mswi_socket->get_direct_mem_ptr(trans, dmi_data);
    if (ok && dmi_data.get_dmi_ptr() != nullptr) {
        // Write 1 via b_transport, read via DMI pointer
        write32_mswi(0x0000, 1);
        uint32_t dmi_val = 0;
        std::memcpy(&dmi_val, dmi_data.get_dmi_ptr(), 4);
        check(dmi_val == 1, "TLM-DMI-COH-01",
              "MSWI: b_transport write visible via DMI pointer");
        // Write 0 via DMI, read via b_transport
        uint32_t zero = 0;
        std::memcpy(dmi_data.get_dmi_ptr(), &zero, 4);
        uint32_t rb = read32_mswi(0x0000);
        check(rb == 0, "TLM-DMI-COH-02",
              "MSWI: DMI write visible via b_transport read");
        // DMI is read-only for MSWI; write via DMI doesn't trigger side-effects
        wait(sc_core::SC_ZERO_TIME);
        check(sw_irq[0].read() == true, "TLM-DMI-COH-03",
              "MSWI: IRQ unchanged after DMI write (read-only DMI)");
    } else {
        check(false, "TLM-DMI-COH-01", "MSWI: DMI not available");
        check(false, "TLM-DMI-COH-02", "MSWI: DMI not available");
        check(false, "TLM-DMI-COH-03", "MSWI: DMI not available");
    }
    cleanup_mswi();

    // --- MTIMER DMI coherency ---
    cleanup_mtimer();
    write32_mtimer(0x7FE8, 0); // disable timer to freeze mtime
    wait(sc_core::SC_ZERO_TIME);
    trans.set_address(0x7FF8);
    trans.set_data_length(8);
    ok = mtimer_socket->get_direct_mem_ptr(trans, dmi_data);
    if (ok && dmi_data.get_dmi_ptr() != nullptr) {
        // Write mtime=0x1234 via b_transport, read via DMI
        write64_mtimer(0x7FF8, 0x1234);
        uint64_t dmi_mtime = 0;
        unsigned char* ptr = dmi_data.get_dmi_ptr();
        std::memcpy(&dmi_mtime, ptr, 8);
        check(dmi_mtime == 0x1234, "TLM-DMI-COH-04",
              "MTIMER: b_transport write visible via DMI pointer");
        // Write via DMI, read via b_transport
        uint64_t new_val = 0x5678;
        std::memcpy(ptr, &new_val, 8);
        uint64_t rb64 = read64_mtimer(0x7FF8);
        check(rb64 == 0x5678, "TLM-DMI-COH-05",
              "MTIMER: DMI write visible via b_transport read");
        // Verify consistency
        check(rb64 == new_val, "TLM-DMI-COH-06",
              "MTIMER: DMI/b_transport values match");
    } else {
        check(false, "TLM-DMI-COH-04", "MTIMER: DMI not available");
        check(false, "TLM-DMI-COH-05", "MTIMER: DMI not available");
        check(false, "TLM-DMI-COH-06", "MTIMER: DMI not available");
    }
    cleanup_mtimer();

    // --- SSWI DMI coherency ---
    cleanup_sswi();
    trans.set_address(0x0000);
    trans.set_data_length(4);
    ok = sswi_socket->get_direct_mem_ptr(trans, dmi_data);
    if (ok && dmi_data.get_dmi_ptr() != nullptr) {
        write32_sswi(0x0000, 1);
        uint32_t dmi_val = 0;
        std::memcpy(&dmi_val, dmi_data.get_dmi_ptr(), 4);
        check(dmi_val == 1, "TLM-DMI-COH-07",
              "SSWI: b_transport write visible via DMI pointer");
        uint32_t zero = 0;
        std::memcpy(dmi_data.get_dmi_ptr(), &zero, 4);
        uint32_t rb = read32_sswi(0x0000);
        check(rb == 0, "TLM-DMI-COH-08",
              "SSWI: DMI write visible via b_transport read");
        // DMI is read-only for SSWI; write via DMI doesn't trigger side-effects
        wait(sc_core::SC_ZERO_TIME);
        check(ssw_irq[0].read() == true, "TLM-DMI-COH-09",
              "SSWI: IRQ unchanged after DMI write (read-only DMI)");
    } else {
        check(false, "TLM-DMI-COH-07", "SSWI: DMI not available");
        check(false, "TLM-DMI-COH-08", "SSWI: DMI not available");
        check(false, "TLM-DMI-COH-09", "SSWI: DMI not available");
    }
    cleanup_sswi();
}

// ═══════════════════════════════════════════════════════════════════
//  sc_main
// ═══════════════════════════════════════════════════════════════════

#include <chrono>
int sc_main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--stress") run_stress = true;
    }

    TestRunner runner("runner");
    aclint_mswi mswi("mswi", NUM_HARTS);
    aclint_mtimer mtimer("mtimer", sc_core::sc_time(1, sc_core::SC_US), NUM_HARTS);
    aclint_sswi sswi("sswi", NUM_HARTS);

    sc_core::sc_signal<bool, sc_core::SC_UNCHECKED_WRITERS> sw_irq_sig[NUM_HARTS];
    sc_core::sc_signal<bool, sc_core::SC_UNCHECKED_WRITERS> timer_irq_sig[NUM_HARTS];
    sc_core::sc_signal<bool, sc_core::SC_UNCHECKED_WRITERS> ssw_irq_sig[NUM_HARTS];

    runner.mswi_socket.bind(mswi.socket);
    runner.mtimer_socket.bind(mtimer.socket);
    runner.sswi_socket.bind(sswi.socket);

    for (int i = 0; i < NUM_HARTS; i++) {
        mswi.sw_irq[i](sw_irq_sig[i]);
        mtimer.timer_irq[i](timer_irq_sig[i]);
        sswi.ssw_irq[i](ssw_irq_sig[i]);
    }

    runner.sw_irq = sw_irq_sig;
    runner.timer_irq = timer_irq_sig;
    runner.ssw_irq = ssw_irq_sig;

    auto wall_start = std::chrono::high_resolution_clock::now();
    sc_core::sc_start();
    auto wall_end = std::chrono::high_resolution_clock::now();
    double wall_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();
    std::cout << "\n[PERF] sim_time=" << sc_core::sc_time_stamp() << " wall_time=" << wall_ms << "ms" << std::endl;

    return (fail_count > 0) ? 1 : 0;
}
