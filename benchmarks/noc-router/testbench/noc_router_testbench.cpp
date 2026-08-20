#include <systemc>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <vector>
#include <chrono>

#include "noc_router.h"

static constexpr unsigned int N_INIT = 4;
static constexpr unsigned int N_TGT  = 4;

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

static void log_step(const char* desc) { std::cout << "│  [STEP] " << desc << std::endl; }

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

SC_MODULE(MemTarget) {
    tlm_utils::simple_target_socket<MemTarget> socket;
    static constexpr unsigned int MEM_SIZE = 0x10000;
    unsigned char mem[MEM_SIZE];
    uint64_t m_base;
    std::deque<tlm::tlm_generic_payload*> m_pending;
    sc_core::sc_event m_ev;

    SC_HAS_PROCESS(MemTarget);
    MemTarget(sc_core::sc_module_name n, uint64_t base)
        : sc_module(n), socket("socket"), m_base(base)
    {
        std::memset(mem, 0, MEM_SIZE);
        socket.register_b_transport(this, &MemTarget::bt);
        socket.register_nb_transport_fw(this, &MemTarget::nb_fw);
        SC_THREAD(resp_thread);
    }

    void do_op(tlm::tlm_generic_payload& t) {
        uint64_t a = t.get_address() - m_base;
        unsigned char* p = t.get_data_ptr();
        unsigned int len = t.get_data_length();
        if (a + len > MEM_SIZE) {
            t.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (t.get_command() == tlm::TLM_READ_COMMAND)
            std::memcpy(p, mem + a, len);
        else if (t.get_command() == tlm::TLM_WRITE_COMMAND)
            std::memcpy(mem + a, p, len);
        t.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void bt(tlm::tlm_generic_payload& t, sc_core::sc_time&) { do_op(t); }

    tlm::tlm_sync_enum nb_fw(tlm::tlm_generic_payload& t,
                              tlm::tlm_phase& ph, sc_core::sc_time&) {
        if (ph == tlm::BEGIN_REQ) {
            do_op(t);
            m_pending.push_back(&t);
            m_ev.notify(sc_core::SC_ZERO_TIME);
            return tlm::TLM_ACCEPTED;
        }
        return tlm::TLM_ACCEPTED;
    }

    void resp_thread() {
        while (true) {
            wait(m_ev);
            while (!m_pending.empty()) {
                tlm::tlm_generic_payload* t = m_pending.front();
                m_pending.pop_front();
                tlm::tlm_phase ph = tlm::BEGIN_RESP;
                sc_core::sc_time d = sc_core::SC_ZERO_TIME;
                socket->nb_transport_bw(*t, ph, d);
            }
        }
    }
};

SC_MODULE(TestRunner) {
    tlm_utils::simple_initiator_socket<TestRunner> cfg_socket;
    sc_core::sc_vector<tlm_utils::simple_initiator_socket_tagged<TestRunner>> data_socket;
    sc_core::sc_signal<bool, sc_core::SC_UNCHECKED_WRITERS>* irq_sig;

    struct NbTrack { tlm::tlm_generic_payload* trans; bool done; };
    std::vector<NbTrack> m_nb_done;
    sc_core::sc_event m_nb_event;

    SC_HAS_PROCESS(TestRunner);
    TestRunner(sc_core::sc_module_name n)
        : sc_module(n)
        , cfg_socket("cfg_socket")
        , data_socket("data_socket", N_INIT)
        , irq_sig(nullptr)
    {
        for (unsigned int i = 0; i < N_INIT; i++)
            data_socket[i].register_nb_transport_bw(this, &TestRunner::nb_resp, static_cast<int>(i));
        SC_THREAD(run_tests);
    }

    tlm::tlm_sync_enum nb_resp(int /*id*/, tlm::tlm_generic_payload& trans,
                                tlm::tlm_phase& phase, sc_core::sc_time& /*delay*/) {
        if (phase == tlm::BEGIN_RESP) {
            for (auto& c : m_nb_done) {
                if (c.trans == &trans) { c.done = true; break; }
            }
            phase = tlm::END_RESP;
            m_nb_event.notify();
            return tlm::TLM_COMPLETED;
        }
        return tlm::TLM_ACCEPTED;
    }

    // ── Helpers ─────────────────────────────────────────────────────
    tlm::tlm_response_status do_cfg(uint64_t addr, uint32_t& data, tlm::tlm_command cmd) {
        tlm::tlm_generic_payload t;
        sc_core::sc_time d = sc_core::SC_ZERO_TIME;
        t.set_command(cmd);
        t.set_address(addr);
        t.set_data_ptr(reinterpret_cast<unsigned char*>(&data));
        t.set_data_length(4);
        t.set_streaming_width(4);
        t.set_byte_enable_ptr(nullptr);
        t.set_byte_enable_length(0);
        t.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        cfg_socket->b_transport(t, d);
        return t.get_response_status();
    }

    void cfg_wr(uint64_t addr, uint32_t val) {
        do_cfg(addr, val, tlm::TLM_WRITE_COMMAND);
    }

    uint32_t cfg_rd(uint64_t addr) {
        uint32_t v = 0;
        do_cfg(addr, v, tlm::TLM_READ_COMMAND);
        return v;
    }

    tlm::tlm_response_status data_txn(unsigned int iid, uint64_t addr,
                                       unsigned char* buf, unsigned int len,
                                       tlm::tlm_command cmd) {
        tlm::tlm_generic_payload t;
        sc_core::sc_time d = sc_core::SC_ZERO_TIME;
        t.set_command(cmd);
        t.set_address(addr);
        t.set_data_ptr(buf);
        t.set_data_length(len);
        t.set_streaming_width(len);
        t.set_byte_enable_ptr(nullptr);
        t.set_byte_enable_length(0);
        t.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        data_socket[iid]->b_transport(t, d);
        return t.get_response_status();
    }

    tlm::tlm_response_status dw32(unsigned int iid, uint64_t addr, uint32_t val) {
        return data_txn(iid, addr, reinterpret_cast<unsigned char*>(&val), 4,
                        tlm::TLM_WRITE_COMMAND);
    }

    uint32_t dr32(unsigned int iid, uint64_t addr, tlm::tlm_response_status* st = nullptr) {
        uint32_t v = 0;
        auto s = data_txn(iid, addr, reinterpret_cast<unsigned char*>(&v), 4,
                          tlm::TLM_READ_COMMAND);
        if (st) *st = s;
        return v;
    }

    void setup_route(unsigned int idx, uint32_t start, uint32_t end,
                     uint32_t tgt, uint32_t ctrl) {
        uint64_t b = 0x100 + idx * 0x10;
        cfg_wr(b + 0x00, start);
        cfg_wr(b + 0x04, end);
        cfg_wr(b + 0x08, tgt);
        cfg_wr(b + 0x0C, ctrl);
    }

    void setup_default_routes() {
        for (unsigned int i = 0; i < N_TGT; i++)
            setup_route(i, i * 0x10000, (i + 1) * 0x10000, i, 0x03);
    }

    void clear_routes() {
        for (unsigned int i = 0; i < 16; i++)
            setup_route(i, 0, 0, 0, 0);
    }

    void clear_stats() { cfg_wr(0x31C, 1); }

    tlm::tlm_sync_enum send_nb(unsigned int iid, tlm::tlm_generic_payload& trans) {
        m_nb_done.push_back({&trans, false});
        tlm::tlm_phase ph = tlm::BEGIN_REQ;
        sc_core::sc_time d = sc_core::SC_ZERO_TIME;
        return data_socket[iid]->nb_transport_fw(trans, ph, d);
    }

    bool wait_nb(tlm::tlm_generic_payload& trans, unsigned int timeout_us = 100) {
        for (unsigned int i = 0; i < timeout_us; i++) {
            for (auto& c : m_nb_done)
                if (c.trans == &trans && c.done) return true;
            wait(1, sc_core::SC_US);
        }
        return false;
    }

    void setup_nb_txn(tlm::tlm_generic_payload& t, uint64_t addr,
                      unsigned char* buf, unsigned int len, tlm::tlm_command cmd) {
        t.set_command(cmd);
        t.set_address(addr);
        t.set_data_ptr(buf);
        t.set_data_length(len);
        t.set_streaming_width(len);
        t.set_byte_enable_ptr(nullptr);
        t.set_byte_enable_length(0);
        t.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    }

    // ── CP-RST: Reset & Configuration ───────────────────────────────
    void test_fn_noc_01() {

        log_step("Reading default register values");
        check(cfg_rd(0x000) == 0x01,       "FN-RST-01", "GLOBAL_CTRL default = 0x01");
        check(cfg_rd(0x004) == 0x00,       "FN-RST-02", "ARB_MODE default = 0");
        check(cfg_rd(0x008) == 0xFFFF,     "FN-RST-03", "TIMEOUT_CFG default = 0xFFFF");
        check(cfg_rd(0x00C) == 0x00,       "FN-RST-04", "STATUS default = 0");
        check(cfg_rd(0x010) == 0x00,       "FN-RST-05", "IRQ_EN default = 0");
        check(cfg_rd(0x014) == 0x00010000, "FN-RST-06", "VERSION = 0x00010000");
        check(cfg_rd(0x018) == N_INIT,     "FN-RST-07", "N_INIT_RO = 4");
        check(cfg_rd(0x01C) == N_TGT,      "FN-RST-08", "N_TGT_RO = 4");

        log_step("Testing WARL bit masking on GLOBAL_CTRL");
        cfg_wr(0x000, 0xFF);
        check(cfg_rd(0x000) == 0xFF,       "FN-RST-09", "GLOBAL_CTRL accepts 0xFF");
        cfg_wr(0x000, 0x100);
        check(cfg_rd(0x000) == 0x00,       "FN-RST-10", "GLOBAL_CTRL masks upper bits");
        cfg_wr(0x000, 0x01);

        log_step("Testing WARL on ARB_MODE and TIMEOUT_CFG");
        cfg_wr(0x004, 3);
        check(cfg_rd(0x004) == 0,          "FN-RST-11", "ARB_MODE WARL: 3 → 0");
        cfg_wr(0x004, 2);
        check(cfg_rd(0x004) == 2,          "FN-RST-12", "ARB_MODE WARL: 2 accepted");
        cfg_wr(0x004, 0);

        cfg_wr(0x008, 0x1FFFF);
        check(cfg_rd(0x008) == 0xFFFF,     "FN-RST-13", "TIMEOUT_CFG WARL: 16-bit mask");
        cfg_wr(0x008, 0xFFFF);

        cfg_wr(0x010, 0xFF);
        check(cfg_rd(0x010) == 0x07,       "FN-RST-14", "IRQ_EN WARL: mask 0x07");
        cfg_wr(0x010, 0x00);

        cfg_wr(0x014, 0xDEAD);
        check(cfg_rd(0x014) == 0x00010000, "FN-RST-15", "VERSION is read-only");

        cfg_wr(0x018, 0xBEEF);
        check(cfg_rd(0x018) == N_INIT,     "FN-RST-16", "N_INIT_RO is read-only");

        cfg_wr(0x01C, 0xBEEF);
        check(cfg_rd(0x01C) == N_TGT,      "FN-RST-17", "N_TGT_RO is read-only");

        cfg_wr(0x200, 0xFF);
        check(cfg_rd(0x200) == 0x07,       "FN-RST-18", "QOS_PRIORITY WARL: mask 0x07");
        cfg_wr(0x200, 0x00);

        cfg_wr(0x240, 0x00);
        check(cfg_rd(0x240) == 0x01,       "FN-RST-19", "ARB_WEIGHT WARL: 0 → 1");
        cfg_wr(0x240, 0xFF);
        check(cfg_rd(0x240) == 0xFF,       "FN-RST-20", "ARB_WEIGHT WARL: 0xFF accepted");
        cfg_wr(0x240, 0x01);
    }

    // ── CP-ROUTE: Address Decode & Routing ──────────────────────────
    void test_fn_noc_02() {
        cfg_wr(0x000, 0x01);
        setup_default_routes();

        log_step("Basic routing to each target");
        check(dw32(0, 0x00000100, 0xA0A0) == tlm::TLM_OK_RESPONSE,
              "FN-ROUTE-01", "Write to tgt 0 succeeds");
        check(dr32(0, 0x00000100) == 0xA0A0,
              "FN-ROUTE-02", "Read from tgt 0 returns written data");
        check(dw32(0, 0x00010100, 0xB1B1) == tlm::TLM_OK_RESPONSE,
              "FN-ROUTE-03", "Write to tgt 1 succeeds");
        check(dr32(0, 0x00010100) == 0xB1B1,
              "FN-ROUTE-04", "Read from tgt 1 returns written data");
        check(dw32(0, 0x00020100, 0xC2C2) == tlm::TLM_OK_RESPONSE,
              "FN-ROUTE-05", "Write to tgt 2 succeeds");
        check(dw32(0, 0x00030100, 0xD3D3) == tlm::TLM_OK_RESPONSE,
              "FN-ROUTE-06", "Write to tgt 3 succeeds");

        log_step("Testing route priority and override");
        setup_route(5, 0x00000000, 0x00010000, 1, 0x03);
        check(dr32(0, 0x00000100) == 0,
              "FN-ROUTE-07", "Higher-index rule (5) overrides rule 0 → tgt 1");
        setup_route(5, 0, 0, 0, 0);
        check(dr32(0, 0x00000100) == 0xA0A0,
              "FN-ROUTE-08", "After removing rule 5, rule 0 routes to tgt 0 again");

        setup_route(0, 0x00000000, 0x00010000, 0, 0x02);
        tlm::tlm_response_status st;
        dr32(0, 0x00000100, &st);
        check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE,
              "FN-ROUTE-09", "Disabled rule (ctrl=0x02, enable=0) → decode error");
        setup_route(0, 0x00000000, 0x00010000, 0, 0x03);

        dr32(0, 0x00050000, &st);
        check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE,
              "FN-ROUTE-10", "No matching rule → decode error");

        check((cfg_rd(0x00C) & 0x02) != 0,
              "FN-ROUTE-11", "STATUS[1] (decode_err) set after decode failure");
        cfg_wr(0x00C, 0x02);
        check((cfg_rd(0x00C) & 0x02) == 0,
              "FN-ROUTE-12", "STATUS[1] cleared by W1C");

        check(dw32(0, 0x00000000, 0x1234) == tlm::TLM_OK_RESPONSE,
              "FN-ROUTE-13", "Addr at rule start (inclusive) matches");
        check(dw32(0, 0x0000FFFC, 0x5678) == tlm::TLM_OK_RESPONSE,
              "FN-ROUTE-14", "Addr at rule end-4 (last aligned) matches");
        dr32(0, 0x00010000, &st);
        check(st == tlm::TLM_OK_RESPONSE,
              "FN-ROUTE-15", "Addr at rule 1 start matches rule 1");

        clear_routes();
        dr32(0, 0x00000100, &st);
        check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE,
              "FN-ROUTE-16", "All rules disabled → all decode errors");

        setup_default_routes();
        check(cfg_rd(0x100) == 0x00000000, "FN-ROUTE-17", "ROUTE_START[0] readback");
        check(cfg_rd(0x104) == 0x00010000, "FN-ROUTE-18", "ROUTE_END[0] readback");
        check(cfg_rd(0x108) == 0,          "FN-ROUTE-19", "ROUTE_TGT[0] readback");
        check(cfg_rd(0x10C) == 0x03,       "FN-ROUTE-20", "ROUTE_CTRL[0] readback");

        setup_route(0, 0x00000000, 0x00000000, 0, 0x03);
        dr32(0, 0x00000000, &st);
        check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE,
              "FN-ROUTE-21", "Zero-length range (start==end) never matches");

        setup_route(0, 0x00000000, 0x00010000, 99, 0x03);
        check(cfg_rd(0x108) == (N_TGT - 1),
              "FN-ROUTE-22", "ROUTE_TGT WARL: 99 clamped to N_TGT-1");

        setup_default_routes();
    }

    // ── CP-ARB-RR: Round-Robin Arbitration ──────────────────────────
    void test_fn_noc_03() {
        cfg_wr(0x000, 0x23);
        cfg_wr(0x004, 0);
        setup_default_routes();
        clear_stats();

        check(dw32(0, 0x00000200, 0x1111) == tlm::TLM_OK_RESPONSE,
              "FN-ARB-01", "Init 0 RR: write succeeds");
        check(dw32(1, 0x00000204, 0x2222) == tlm::TLM_OK_RESPONSE,
              "FN-ARB-02", "Init 1 RR: write succeeds");
        check(dw32(2, 0x00000208, 0x3333) == tlm::TLM_OK_RESPONSE,
              "FN-ARB-03", "Init 2 RR: write succeeds");
        check(dw32(3, 0x0000020C, 0x4444) == tlm::TLM_OK_RESPONSE,
              "FN-ARB-04", "Init 3 RR: write succeeds");

        check(dr32(1, 0x00000200) == 0x1111,
              "FN-ARB-05", "Init 1 reads data written by init 0");
        check(dr32(2, 0x00000204) == 0x2222,
              "FN-ARB-06", "Init 2 reads data written by init 1");

        cfg_wr(0x000, 0x01);
        check(dw32(0, 0x00000300, 0xAAAA) == tlm::TLM_OK_RESPONSE,
              "FN-ARB-07", "ARB_EN=0: write still succeeds (bypass)");
        check(dr32(0, 0x00000300) == 0xAAAA,
              "FN-ARB-08", "ARB_EN=0: read still succeeds (bypass)");

        cfg_wr(0x000, 0x23);
        for (unsigned int i = 0; i < 4; i++)
            dw32(i, 0x00000400 + i * 4, i + 100);
        check(cfg_rd(0x300) >= 4,
              "FN-ARB-09", "Stats: total_txn >= 4 after 4 arb writes");

        cfg_wr(0x004, 1);
        cfg_wr(0x004, 0);
        check(dw32(0, 0x00000500, 0xBBBB) == tlm::TLM_OK_RESPONSE,
              "FN-ARB-10", "Mode switch FP→RR: still works");

        cfg_wr(0x000, 0x01);
        cfg_wr(0x004, 0);
    }

    // ── CP-ARB-FP: Fixed Priority ───────────────────────────────────
    void test_fn_noc_04() {
        cfg_wr(0x000, 0x23);
        cfg_wr(0x004, 1);
        setup_default_routes();
        clear_stats();

        check(dw32(0, 0x00000600, 0xF0F0) == tlm::TLM_OK_RESPONSE,
              "FN-ARB-11", "Init 0 FP: write succeeds");
        check(dw32(3, 0x00000604, 0xF3F3) == tlm::TLM_OK_RESPONSE,
              "FN-ARB-12", "Init 3 FP: write succeeds");
        check(dr32(0, 0x00000600) == 0xF0F0,
              "FN-ARB-13", "FP readback from init 0");
        check(dr32(3, 0x00000604) == 0xF3F3,
              "FN-ARB-14", "FP readback from init 3");

        cfg_wr(0x000, 0x33);
        cfg_wr(0x200, 7);
        cfg_wr(0x204, 0);
        check(dw32(0, 0x00000608, 0xAA) == tlm::TLM_OK_RESPONSE,
              "FN-ARB-15", "FP+QoS: init 0 (prio 7) write succeeds");
        check(dw32(1, 0x0000060C, 0xBB) == tlm::TLM_OK_RESPONSE,
              "FN-ARB-16", "FP+QoS: init 1 (prio 0) write succeeds");

        check(cfg_rd(0x200) == 7, "FN-ARB-17", "QOS_PRIORITY[0] readback = 7");
        check(cfg_rd(0x204) == 0, "FN-ARB-18", "QOS_PRIORITY[1] readback = 0");

        cfg_wr(0x000, 0x01);
        cfg_wr(0x004, 0);
        for (unsigned int i = 0; i < N_INIT; i++) cfg_wr(0x200 + i * 4, 0);
    }

    // ── CP-ARB-WRR: Weighted Round-Robin ────────────────────────────
    void test_fn_noc_05() {
        cfg_wr(0x000, 0x23);
        cfg_wr(0x004, 2);
        setup_default_routes();
        clear_stats();

        for (unsigned int i = 0; i < N_INIT; i++)
            cfg_wr(0x240 + i * 4, 1);
        check(dw32(0, 0x00000700, 0xAA00) == tlm::TLM_OK_RESPONSE,
              "FN-ARB-19", "WRR equal weights: init 0 write");
        check(dw32(1, 0x00000704, 0xBB01) == tlm::TLM_OK_RESPONSE,
              "FN-ARB-20", "WRR equal weights: init 1 write");

        cfg_wr(0x240, 4);
        cfg_wr(0x244, 1);
        check(cfg_rd(0x240) == 4,          "FN-ARB-21", "WEIGHT[0] readback = 4");
        check(cfg_rd(0x244) == 1,          "FN-ARB-22", "WEIGHT[1] readback = 1");
        check(dw32(0, 0x00000708, 0x0A) == tlm::TLM_OK_RESPONSE,
              "FN-ARB-23", "WRR weight=4 init 0 write");
        check(dw32(0, 0x0000070C, 0x0B) == tlm::TLM_OK_RESPONSE,
              "FN-ARB-24", "WRR weight=4 init 0 2nd write");

        cfg_wr(0x240, 0);
        check(cfg_rd(0x240) == 1,          "FN-ARB-25", "WEIGHT WARL: 0 → 1");
        cfg_wr(0x240, 0x1FF);
        check(cfg_rd(0x240) == 0xFF,       "FN-ARB-26", "WEIGHT WARL: mask 0xFF");

        cfg_wr(0x000, 0x01);
        cfg_wr(0x004, 0);
        for (unsigned int i = 0; i < N_INIT; i++) cfg_wr(0x240 + i * 4, 1);
    }

    // ── CP-RCACHE: Route Cache ──────────────────────────────────────
    void test_fn_noc_06() {
        setup_default_routes();

        cfg_wr(0x000, 0x21);
        clear_stats();
        dw32(0, 0x00000800, 0x11);
        dw32(0, 0x00000804, 0x22);
        check(cfg_rd(0x304) == 0, "FN-RCACHE-01", "Route cache disabled: route_hit = 0");
        check(cfg_rd(0x308) > 0,  "FN-RCACHE-02", "Route cache disabled: route_miss > 0");

        cfg_wr(0x000, 0x25);
        clear_stats();
        dw32(0, 0x00000900, 0x33);
        uint32_t miss1 = cfg_rd(0x308);
        dw32(0, 0x00000904, 0x44);
        uint32_t hit1 = cfg_rd(0x304);
        check(miss1 > 0, "FN-RCACHE-03", "Route cache enabled: first access misses");
        check(hit1 > 0,  "FN-RCACHE-04", "Route cache enabled: second access hits");

        cfg_wr(0x500, 0x01);
        clear_stats();
        dw32(0, 0x00000900, 0x55);
        check(cfg_rd(0x304) == 0, "FN-RCACHE-05", "After flush: first access misses again");

        clear_stats();
        for (unsigned int i = 0; i < N_TGT; i++)
            setup_route(i, i * 0x10000, (i + 1) * 0x10000, i, 0x03);
        cfg_wr(0x500, 0x01);
        for (unsigned int i = 0; i < N_TGT; i++)
            dw32(0, i * 0x10000 + 0x100, i);
        clear_stats();
        dw32(0, 0x00000100, 0xAA);
        uint32_t h = cfg_rd(0x304);
        check(h > 0, "FN-RCACHE-06", "8 rules cached, re-access rule 0 hits");

        setup_route(8, 0x80000, 0x90000, 0, 0x03);
        dw32(0, 0x80100, 0xBB);
        check(true, "FN-RCACHE-07", "9th rule fills and evicts LRU (no crash)");

        setup_route(0, 0x00000000, 0x00010000, 0, 0x03);
        check(true, "FN-RCACHE-08", "Route rule write flushes cache (no crash)");

        cfg_wr(0x000, 0x21);
        clear_stats();
        dw32(0, 0x00000A00, 0x66);
        dw32(0, 0x00000A04, 0x77);
        check(cfg_rd(0x304) == 0, "FN-RCACHE-09", "Cache disabled after CTRL change: no hits");

        cfg_wr(0x000, 0x25);
        clear_stats();
        dw32(0, 0x00000A00, 0x88);
        check(cfg_rd(0x304) == 0, "FN-RCACHE-10", "Re-enable: cache empty, first miss");
        dw32(0, 0x00000A04, 0x99);
        check(cfg_rd(0x304) > 0,  "FN-RCACHE-11", "Re-enable: second access hits");

        clear_stats();
        dw32(0, 0x00000A00, 0xAA);
        dw32(0, 0x00000A00, 0xBB);
        dw32(0, 0x00000A00, 0xCC);
        check(cfg_rd(0x304) >= 2, "FN-RCACHE-12", "Multiple hits from same rule");

        check(cfg_rd(0x504) == 8, "FN-RCACHE-13", "ROUTE_CACHE_ENTRIES RO = 8");
        check(cfg_rd(0x508) == 16, "FN-RCACHE-14", "DATA_CACHE_LINES RO = 16");
        check(cfg_rd(0x50C) == 32, "FN-RCACHE-15", "DATA_CACHE_LINE_SZ RO = 32");

        setup_default_routes();
        cfg_wr(0x000, 0x01);
    }

    // ── CP-DCACHE: Data Cache ───────────────────────────────────────
    void test_fn_noc_07() {
        setup_default_routes();

        cfg_wr(0x000, 0x21);
        clear_stats();
        dw32(0, 0x00000000, 0xCAFE);
        check(dr32(0, 0x00000000) == 0xCAFE,
              "FN-DCACHE-01", "Cache disabled: read goes to target, returns data");

        cfg_wr(0x000, 0x29);
        clear_stats();
        dw32(0, 0x00001000, 0xBEEF);
        uint32_t r1 = dr32(0, 0x00001000);
        check(r1 == 0xBEEF, "FN-DCACHE-02", "Cache enabled: first read (miss) returns target data");
        uint32_t dm1 = cfg_rd(0x310);
        check(dm1 > 0,      "FN-DCACHE-03", "First read is data cache miss");

        uint32_t r2 = dr32(0, 0x00001000);
        check(r2 == 0xBEEF, "FN-DCACHE-04", "Second read (hit) returns cached data");
        uint32_t dh1 = cfg_rd(0x30C);
        check(dh1 > 0,      "FN-DCACHE-05", "Second read is data cache hit");

        dw32(0, 0x00001000, 0xDEAD);
        uint32_t r3 = dr32(0, 0x00001000);
        check(r3 == 0xDEAD, "FN-DCACHE-06", "Write-through updates cache, subsequent read hits new data");

        clear_stats();
        dw32(0, 0x00002000, 0x1234);
        check(cfg_rd(0x30C) == 0, "FN-DCACHE-07", "Write miss: no data cache hit");

        cfg_wr(0x500, 0x02);
        clear_stats();
        uint32_t r4 = dr32(0, 0x00001000);
        check(r4 == 0xDEAD, "FN-DCACHE-08", "After data cache flush: read miss goes to target");
        check(cfg_rd(0x310) > 0,  "FN-DCACHE-09", "After flush: read counted as miss");

        cfg_wr(0x000, 0x21);
        clear_stats();
        dw32(0, 0x00003000, 0xAAAA);
        uint32_t r5 = dr32(0, 0x00003000);
        check(r5 == 0xAAAA, "FN-DCACHE-10", "Feature disable: read goes to target");
        check(cfg_rd(0x30C) == 0, "FN-DCACHE-11", "Feature disable: no data cache hits");

        setup_route(0, 0x00000000, 0x00010000, 0, 0x01);
        cfg_wr(0x000, 0x29);
        clear_stats();
        dw32(0, 0x00004000, 0xBBBB);
        dr32(0, 0x00004000);
        dr32(0, 0x00004000);
        check(cfg_rd(0x30C) == 0, "FN-DCACHE-12", "Cacheable=0: no cache hits even when enabled");
        setup_route(0, 0x00000000, 0x00010000, 0, 0x03);

        cfg_wr(0x000, 0x29);
        clear_stats();
        cfg_wr(0x500, 0x02);
        dw32(0, 0x00005000, 0x1111);
        dr32(0, 0x00005000);
        dw32(0, 0x00005020, 0x2222);
        dr32(0, 0x00005020);
        dr32(0, 0x00005000);
        uint32_t dh2 = cfg_rd(0x30C);
        check(dh2 >= 1, "FN-DCACHE-13", "Multiple lines can be populated");

        check(cfg_rd(0x510) > 0, "FN-DCACHE-14", "DATA_CACHE_STATUS > 0 after allocations");

        cfg_wr(0x500, 0x02);
        check(cfg_rd(0x510) == 0, "FN-DCACHE-15", "DATA_CACHE_STATUS = 0 after flush");

        cfg_wr(0x000, 0x29);
        clear_stats();
        unsigned char wbuf[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        unsigned char rbuf[8] = {0};
        data_txn(0, 0x00006000, wbuf, 8, tlm::TLM_WRITE_COMMAND);
        data_txn(0, 0x00006000, rbuf, 8, tlm::TLM_READ_COMMAND);
        data_txn(0, 0x00006000, rbuf, 8, tlm::TLM_READ_COMMAND);
        check(std::memcmp(wbuf, rbuf, 8) == 0,
              "FN-DCACHE-16", "8-byte read from cache matches written data");
        check(cfg_rd(0x30C) > 0, "FN-DCACHE-17", "8-byte read hit counted");

        cfg_wr(0x000, 0x29);
        clear_stats();
        dw32(0, 0x00007000, 0xCC);
        dr32(0, 0x00007000);
        cfg_wr(0x000, 0x21);
        cfg_wr(0x000, 0x29);
        clear_stats();
        dr32(0, 0x00007000);
        check(cfg_rd(0x310) > 0, "FN-DCACHE-18", "Feature disable/re-enable: cache empty, read misses");

        cfg_wr(0x000, 0x01);
    }

    // ── CP-QOS: Quality of Service ──────────────────────────────────
    void test_fn_noc_08() {
        setup_default_routes();

        cfg_wr(0x000, 0x23);
        cfg_wr(0x004, 0);
        for (unsigned int i = 0; i < N_INIT; i++) cfg_wr(0x200 + i * 4, 0);
        check(dw32(0, 0x00000B00, 0x11) == tlm::TLM_OK_RESPONSE,
              "FN-QOS-01", "QoS disabled (CTRL_QOS_EN=0): write succeeds");

        cfg_wr(0x000, 0x33);
        check(dw32(0, 0x00000B04, 0x22) == tlm::TLM_OK_RESPONSE,
              "FN-QOS-02", "QoS enabled with RR: write succeeds");

        for (unsigned int i = 0; i < N_INIT; i++) {
            cfg_wr(0x200 + i * 4, i);
            check(cfg_rd(0x200 + i * 4) == i,
                  "FN-QOS-03", "QOS_PRIORITY readback correct");
        }

        cfg_wr(0x200, 0x0F);
        check(cfg_rd(0x200) == 0x07, "FN-QOS-04", "QOS WARL: 0x0F clamped to 0x07");
        cfg_wr(0x200, 0);

        cfg_wr(0x004, 1);
        cfg_wr(0x200, 7);
        cfg_wr(0x204, 0);
        check(dw32(0, 0x00000B08, 0x33) == tlm::TLM_OK_RESPONSE,
              "FN-QOS-05", "FP+QoS: init 0 (prio 7) succeeds");
        check(dw32(1, 0x00000B0C, 0x44) == tlm::TLM_OK_RESPONSE,
              "FN-QOS-06", "FP+QoS: init 1 (prio 0) succeeds");

        for (unsigned int i = 0; i < N_INIT; i++) cfg_wr(0x200 + i * 4, 3);
        check(dw32(0, 0x00000B10, 0x55) == tlm::TLM_OK_RESPONSE,
              "FN-QOS-07", "All same priority: write succeeds");
        check(dw32(1, 0x00000B14, 0x66) == tlm::TLM_OK_RESPONSE,
              "FN-QOS-08", "All same priority: init 1 also succeeds");

        cfg_wr(0x200, 5);
        check(dw32(0, 0x00000B18, 0x77) == tlm::TLM_OK_RESPONSE,
              "FN-QOS-09", "Change priority mid-operation: succeeds");

        cfg_wr(0x000, 0x23);
        check(dw32(0, 0x00000B1C, 0x88) == tlm::TLM_OK_RESPONSE,
              "FN-QOS-10", "QoS toggle off: still works");

        cfg_wr(0x000, 0x01);
        cfg_wr(0x004, 0);
        for (unsigned int i = 0; i < N_INIT; i++) cfg_wr(0x200 + i * 4, 0);
    }

    // ── CP-TMO: Timeout ─────────────────────────────────────────────
    void test_fn_noc_09() {

        check(cfg_rd(0x008) == 0xFFFF, "FN-TMO-01", "TIMEOUT_CFG default = 0xFFFF");
        cfg_wr(0x008, 100);
        check(cfg_rd(0x008) == 100,    "FN-TMO-02", "TIMEOUT_CFG write/readback = 100");
        cfg_wr(0x008, 0x20000);
        check(cfg_rd(0x008) == 0x0000, "FN-TMO-03", "TIMEOUT_CFG WARL: 16-bit mask");
        cfg_wr(0x008, 0xFFFF);

        check((cfg_rd(0x00C) & 0x01) == 0, "FN-TMO-04", "STATUS[0] (timeout) clear at start");

        cfg_wr(0x000, 0x41);
        setup_default_routes();
        dw32(0, 0x00000C00, 0x11);
        check((cfg_rd(0x00C) & 0x01) == 0,
              "FN-TMO-05", "Blocking txn completes instantly: no timeout");

        cfg_wr(0x010, 0x01);
        wait(sc_core::SC_ZERO_TIME);
        check(irq_sig->read() == false,
              "FN-TMO-06", "No timeout status: IRQ not asserted even with IRQ_EN[0]");
        cfg_wr(0x010, 0x00);

        check(cfg_rd(0x318) == 0, "FN-TMO-07", "STAT_TIMEOUT = 0 (no stats_en or no timeout)");

        cfg_wr(0x000, 0x61);
        clear_stats();
        dw32(0, 0x00000C04, 0x22);
        check(cfg_rd(0x318) == 0, "FN-TMO-08", "STAT_TIMEOUT still 0 after normal blocking txn");

        cfg_wr(0x000, 0x01);
        cfg_wr(0x008, 0xFFFF);
    }

    // ── CP-IRQ: Interrupts ──────────────────────────────────────────
    void test_ir_noc_01() {
        setup_default_routes();
        cfg_wr(0x010, 0x00);
        cfg_wr(0x00C, 0x07);

        log_step("Verifying IRQ deasserted at reset");
        wait(sc_core::SC_ZERO_TIME);
        check(irq_sig->read() == false, "IR-IRQ-01", "IRQ deasserted at reset");

        cfg_wr(0x010, 0x07);
        check(cfg_rd(0x010) == 0x07, "IR-IRQ-02", "IRQ_EN readback = 0x07");
        wait(sc_core::SC_ZERO_TIME);
        check(irq_sig->read() == false, "IR-IRQ-03", "IRQ still low (no status bits)");

        cfg_wr(0x000, 0x01);
        clear_routes();
        tlm::tlm_response_status st;
        dr32(0, 0x00000100, &st);
        wait(sc_core::SC_ZERO_TIME);
        check((cfg_rd(0x00C) & 0x02) != 0, "IR-IRQ-04", "Decode error sets STATUS[1]");
        check(irq_sig->read() == true,     "IR-IRQ-05", "IRQ asserted (IRQ_EN[1] & STATUS[1])");

        cfg_wr(0x00C, 0x02);
        wait(sc_core::SC_ZERO_TIME);
        check(irq_sig->read() == false, "IR-IRQ-06", "W1C clears STATUS[1] → IRQ deasserted");

        cfg_wr(0x010, 0x00);
        dr32(0, 0x00000100, &st);
        wait(sc_core::SC_ZERO_TIME);
        check(irq_sig->read() == false, "IR-IRQ-07", "IRQ_EN=0: no IRQ despite status");
        cfg_wr(0x00C, 0x07);

        cfg_wr(0x010, 0x04);
        wait(sc_core::SC_ZERO_TIME);
        check(irq_sig->read() == false, "IR-IRQ-08", "IRQ_EN[2] but STATUS[2]=0 → no IRQ");

        cfg_wr(0x010, 0x02);
        dr32(0, 0x00000100, &st);
        wait(sc_core::SC_ZERO_TIME);
        check(irq_sig->read() == true, "IR-IRQ-09", "IRQ_EN[1], decode error → IRQ asserted");
        cfg_wr(0x00C, 0x02);
        wait(sc_core::SC_ZERO_TIME);
        check(irq_sig->read() == false, "IR-IRQ-10", "Clear STATUS → IRQ deasserted");

        cfg_wr(0x00C, 0x01);
        wait(sc_core::SC_ZERO_TIME);
        check((cfg_rd(0x00C) & 0x02) == 0, "IR-IRQ-11", "W1C on bit 0 doesn't affect bit 1");

        cfg_wr(0x010, 0x07);
        setup_default_routes();
        cfg_wr(0x000, 0x29);
        dr32(0, 0x00008000);
        dr32(0, 0x00008000);
        wait(sc_core::SC_ZERO_TIME);
        bool combo_irq = irq_sig->read();
        check(true, "IR-IRQ-12", (std::string("IRQ with all features: irq=") +
              (combo_irq ? "true" : "false")).c_str());

        cfg_wr(0x000, 0x01);
        cfg_wr(0x010, 0x00);
        cfg_wr(0x00C, 0x07);
    }

    // ── CP-STAT: Statistics ─────────────────────────────────────────
    void test_fn_noc_10() {
        setup_default_routes();

        cfg_wr(0x000, 0x01);
        clear_stats();
        dw32(0, 0x0000D000, 0x11);
        check(cfg_rd(0x300) == 0, "FN-STAT-01", "Stats disabled: total_txn stays 0");

        cfg_wr(0x000, 0x21);
        clear_stats();
        dw32(0, 0x0000D004, 0x22);
        check(cfg_rd(0x300) > 0, "FN-STAT-02", "Stats enabled: total_txn increments");

        clear_stats();
        dw32(0, 0x0000D008, 0x33);
        check(cfg_rd(0x308) > 0, "FN-STAT-03", "route_miss > 0 (no route cache)");
        check(cfg_rd(0x304) == 0, "FN-STAT-04", "route_hit = 0 (no route cache)");

        cfg_wr(0x000, 0x29);
        clear_stats();
        dw32(0, 0x0000D100, 0x44);
        dr32(0, 0x0000D100);
        dr32(0, 0x0000D100);
        check(cfg_rd(0x30C) > 0, "FN-STAT-05", "data_hit > 0 after cache hit");
        check(cfg_rd(0x310) > 0, "FN-STAT-06", "data_miss > 0 after initial miss");

        cfg_wr(0x000, 0x21);
        clear_routes();
        clear_stats();
        dr32(0, 0x00050000);
        check(cfg_rd(0x314) > 0, "FN-STAT-07", "decode_err counter increments");
        cfg_wr(0x00C, 0x07);
        setup_default_routes();

        clear_stats();
        check(cfg_rd(0x300) == 0, "FN-STAT-08", "Stats clear: total_txn = 0");
        check(cfg_rd(0x304) == 0, "FN-STAT-09", "Stats clear: route_hit = 0");
        check(cfg_rd(0x314) == 0, "FN-STAT-10", "Stats clear: decode_err = 0");

        cfg_wr(0x300, 999);
        check(cfg_rd(0x300) == 0, "FN-STAT-11", "Stat counters are RO (write ignored)");

        dw32(0, 0x0000D200, 0x55);
        dw32(0, 0x0000D204, 0x66);
        uint32_t before = cfg_rd(0x300);
        dw32(0, 0x0000D208, 0x77);
        check(cfg_rd(0x300) == before + 1, "FN-STAT-12", "Stats resume counting after clear");

        cfg_wr(0x000, 0x01);
    }

    // ── CP-ERR: Error Handling ───────────────────────────────────────
    void test_fn_noc_11() {
        setup_default_routes();
        cfg_wr(0x000, 0x01);

        {
            tlm::tlm_generic_payload t;
            sc_core::sc_time d = sc_core::SC_ZERO_TIME;
            t.set_command(tlm::TLM_READ_COMMAND);
            t.set_address(0x000);
            t.set_data_ptr(nullptr);
            t.set_data_length(4);
            t.set_streaming_width(4);
            t.set_byte_enable_ptr(nullptr);
            t.set_byte_enable_length(0);
            t.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            cfg_socket->b_transport(t, d);
            check(t.get_response_status() == tlm::TLM_GENERIC_ERROR_RESPONSE,
                  "FN-ERR-01", "Config: null data ptr -> GENERIC_ERROR");
        }
        {
            uint16_t buf16 = 0;
            tlm::tlm_generic_payload t;
            sc_core::sc_time d = sc_core::SC_ZERO_TIME;
            t.set_command(tlm::TLM_READ_COMMAND);
            t.set_address(0x000);
            t.set_data_ptr(reinterpret_cast<unsigned char*>(&buf16));
            t.set_data_length(2);
            t.set_streaming_width(2);
            t.set_byte_enable_ptr(nullptr);
            t.set_byte_enable_length(0);
            t.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            cfg_socket->b_transport(t, d);
            check(t.get_response_status() == tlm::TLM_GENERIC_ERROR_RESPONSE,
                  "FN-ERR-02", "Config: 2-byte length -> GENERIC_ERROR");
        }
        {
            uint32_t dummy = 0;
            auto st = do_cfg(0x001, dummy, tlm::TLM_READ_COMMAND);
            check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE,
                  "FN-ERR-03", "Config: unaligned address -> ADDRESS_ERROR");
        }
        {
            uint32_t dummy = 0;
            auto st = do_cfg(0x020, dummy, tlm::TLM_READ_COMMAND);
            check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE,
                  "FN-ERR-04", "Config: reserved gap 0x020 -> ADDRESS_ERROR");
        }
        {
            uint32_t dummy = 0;
            auto st = do_cfg(0x600, dummy, tlm::TLM_READ_COMMAND);
            check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE,
                  "FN-ERR-05", "Config: out of range 0x600 -> ADDRESS_ERROR");
        }
        {
            tlm::tlm_generic_payload t;
            sc_core::sc_time d = sc_core::SC_ZERO_TIME;
            uint32_t dummy = 0;
            t.set_command(tlm::TLM_IGNORE_COMMAND);
            t.set_address(0x000);
            t.set_data_ptr(reinterpret_cast<unsigned char*>(&dummy));
            t.set_data_length(4);
            t.set_streaming_width(4);
            t.set_byte_enable_ptr(nullptr);
            t.set_byte_enable_length(0);
            t.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            cfg_socket->b_transport(t, d);
            check(t.get_response_status() == tlm::TLM_GENERIC_ERROR_RESPONSE,
                  "FN-ERR-06", "Config: IGNORE command -> GENERIC_ERROR");
        }
        {
            tlm::tlm_generic_payload t;
            sc_core::sc_time d = sc_core::SC_ZERO_TIME;
            t.set_command(tlm::TLM_READ_COMMAND);
            t.set_address(0x00000100);
            t.set_data_ptr(nullptr);
            t.set_data_length(4);
            t.set_streaming_width(4);
            t.set_byte_enable_ptr(nullptr);
            t.set_byte_enable_length(0);
            t.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            data_socket[0]->b_transport(t, d);
            check(t.get_response_status() == tlm::TLM_GENERIC_ERROR_RESPONSE,
                  "FN-ERR-07", "Data: null data ptr -> GENERIC_ERROR");
        }

        cfg_wr(0x000, 0x00);
        check(dw32(0, 0x00000100, 0xAA) == tlm::TLM_GENERIC_ERROR_RESPONSE,
              "FN-ERR-08", "Data: router disabled -> GENERIC_ERROR");
        cfg_wr(0x000, 0x01);

        {
            tlm::tlm_response_status st;
            dr32(0, 0xFFF00000, &st);
            check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE,
                  "FN-ERR-09", "Data: no matching route -> ADDRESS_ERROR");
        }
        cfg_wr(0x00C, 0x07);

        check(dw32(0, 0x00000100, 0xBB) == tlm::TLM_OK_RESPONSE,
              "FN-ERR-10", "Data: valid transaction -> OK_RESPONSE");
        {
            uint32_t dummy = 0;
            check(do_cfg(0x0FC, dummy, tlm::TLM_READ_COMMAND) == tlm::TLM_ADDRESS_ERROR_RESPONSE,
                  "FN-ERR-11", "Config: reserved gap 0x0FC -> ADDRESS_ERROR");
        }
        {
            uint32_t dummy = 0;
            check(do_cfg(0x5FC, dummy, tlm::TLM_READ_COMMAND) == tlm::TLM_ADDRESS_ERROR_RESPONSE,
                  "FN-ERR-12", "Config: reserved gap 0x5FC -> ADDRESS_ERROR");
        }
    }

    // ── CP-NB: Non-Blocking Transport ───────────────────────────────
    void test_tlm_noc_01() {
        setup_default_routes();
        cfg_wr(0x000, 0x01);
        m_nb_done.clear();

        log_step("NB transport with feature disabled");
        {
            tlm::tlm_generic_payload t;
            uint32_t buf = 0;
            setup_nb_txn(t, 0x00000100, reinterpret_cast<unsigned char*>(&buf),
                         4, tlm::TLM_READ_COMMAND);
            auto ret = send_nb(0, t);
            check(ret == tlm::TLM_ACCEPTED,
                  "TLM-NB-01", "NB mode disabled: returns TLM_ACCEPTED (dropped)");
            m_nb_done.clear();
        }

        cfg_wr(0x000, 0x81);
        dw32(0, 0x00000200, 0xABCD);
        {
            tlm::tlm_generic_payload t;
            uint32_t buf = 0;
            setup_nb_txn(t, 0x00000200, reinterpret_cast<unsigned char*>(&buf),
                         4, tlm::TLM_READ_COMMAND);
            auto ret = send_nb(0, t);
            check(ret == tlm::TLM_ACCEPTED,
                  "TLM-NB-02", "NB mode enabled: BEGIN_REQ returns TLM_ACCEPTED");
            bool done = wait_nb(t);
            check(done, "TLM-NB-03", "NB read completes within timeout");
            check(buf == 0xABCD, "TLM-NB-04", "NB read returns correct data");
            m_nb_done.clear();
        }

        {
            tlm::tlm_generic_payload t;
            uint32_t wval = 0xDEAD;
            setup_nb_txn(t, 0x00000300, reinterpret_cast<unsigned char*>(&wval),
                         4, tlm::TLM_WRITE_COMMAND);
            auto ret = send_nb(0, t);
            check(ret == tlm::TLM_ACCEPTED, "TLM-NB-05", "NB write: returns TLM_ACCEPTED");
            bool done = wait_nb(t);
            check(done, "TLM-NB-06", "NB write completes within timeout");
            check(t.get_response_status() == tlm::TLM_OK_RESPONSE,
                  "TLM-NB-07", "NB write response is OK");
            m_nb_done.clear();
        }

        check(dr32(0, 0x00000300) == 0xDEAD,
              "TLM-NB-08", "NB written data readable via b_transport");

        {
            tlm::tlm_generic_payload t;
            uint32_t buf = 0;
            setup_nb_txn(t, 0xFFF00000, reinterpret_cast<unsigned char*>(&buf),
                         4, tlm::TLM_READ_COMMAND);
            auto ret = send_nb(0, t);
            check(ret == tlm::TLM_COMPLETED,
                  "TLM-NB-09", "NB decode error: returns TLM_COMPLETED");
            check(t.get_response_status() == tlm::TLM_ADDRESS_ERROR_RESPONSE,
                  "TLM-NB-10", "NB decode error: ADDRESS_ERROR status");
            m_nb_done.clear();
            cfg_wr(0x00C, 0x07);
        }

        cfg_wr(0x000, 0x80);
        {
            tlm::tlm_generic_payload t;
            uint32_t buf = 0;
            setup_nb_txn(t, 0x00000100, reinterpret_cast<unsigned char*>(&buf),
                         4, tlm::TLM_READ_COMMAND);
            auto ret = send_nb(0, t);
            check(ret == tlm::TLM_COMPLETED,
                  "TLM-NB-11", "NB router disabled: returns TLM_COMPLETED");
            m_nb_done.clear();
        }
        cfg_wr(0x000, 0x81);

        {
            tlm::tlm_generic_payload t;
            setup_nb_txn(t, 0x00000100, nullptr, 4, tlm::TLM_READ_COMMAND);
            auto ret = send_nb(0, t);
            check(ret == tlm::TLM_COMPLETED,
                  "TLM-NB-12", "NB null data ptr: returns TLM_COMPLETED");
            m_nb_done.clear();
        }

        cfg_wr(0x000, 0xA1);
        clear_stats();
        dw32(0, 0x00000400, 0x1234);
        {
            tlm::tlm_generic_payload t;
            uint32_t buf = 0;
            setup_nb_txn(t, 0x00000400, reinterpret_cast<unsigned char*>(&buf),
                         4, tlm::TLM_READ_COMMAND);
            send_nb(0, t);
            wait_nb(t);
            check(cfg_rd(0x300) > 0, "TLM-NB-13", "NB with stats: total_txn increments");
            m_nb_done.clear();
        }

        {
            dw32(0, 0x00000500, 0xAAAA);
            dw32(0, 0x00000504, 0xBBBB);
            tlm::tlm_generic_payload t1, t2;
            uint32_t b1 = 0, b2 = 0;
            setup_nb_txn(t1, 0x00000500, reinterpret_cast<unsigned char*>(&b1),
                         4, tlm::TLM_READ_COMMAND);
            setup_nb_txn(t2, 0x00000504, reinterpret_cast<unsigned char*>(&b2),
                         4, tlm::TLM_READ_COMMAND);
            send_nb(0, t1);
            send_nb(1, t2);
            bool d1 = wait_nb(t1);
            bool d2 = wait_nb(t2);
            check(d1 && d2, "TLM-NB-14", "NB sequential requests both complete");
            check(b1 == 0xAAAA && b2 == 0xBBBB,
                  "TLM-NB-15", "NB sequential reads return correct data");
            m_nb_done.clear();
        }

        cfg_wr(0x000, 0x01);
    }

    // ── CP-COMBO: Feature Combinations ──────────────────────────────
    void test_fn_noc_12() {
        setup_default_routes();

        cfg_wr(0x000, 0x3F);
        cfg_wr(0x004, 0);
        clear_stats();
        dw32(0, 0x0000E000, 0xAA);
        check(dr32(0, 0x0000E000) == 0xAA,
              "FN-COMBO-01", "All features enabled: read returns written data");
        dw32(0, 0x0000E004, 0xBB);
        check(dr32(0, 0x0000E004) == 0xBB,
              "FN-COMBO-02", "All features enabled: second addr works");
        check(cfg_rd(0x300) > 0, "FN-COMBO-03", "All features: stats counting");

        cfg_wr(0x000, 0x23);
        clear_stats();
        dw32(0, 0x0000E100, 0xCC);
        check(dr32(0, 0x0000E100) == 0xCC,
              "FN-COMBO-04", "Router+arb only: works");

        cfg_wr(0x000, 0x25);
        clear_stats();
        dw32(0, 0x0000E200, 0xDD);
        dr32(0, 0x0000E200);
        dr32(0, 0x0000E200);
        check(cfg_rd(0x304) > 0, "FN-COMBO-05", "Router+rcache: route hits");

        cfg_wr(0x000, 0x29);
        clear_stats();
        dw32(0, 0x0000E300, 0xEE);
        dr32(0, 0x0000E300);
        dr32(0, 0x0000E300);
        check(cfg_rd(0x30C) > 0, "FN-COMBO-06", "Router+dcache: data hits");

        cfg_wr(0x000, 0x21);
        clear_stats();
        dw32(0, 0x0000E400, 0xFF);
        check(cfg_rd(0x300) > 0, "FN-COMBO-07", "Router+stats only: counting");

        cfg_wr(0x000, 0x41);
        dw32(0, 0x0000E500, 0x11);
        check(dr32(0, 0x0000E500) == 0x11,
              "FN-COMBO-08", "Router+timeout only: works");

        cfg_wr(0x000, 0x11);
        dw32(0, 0x0000E600, 0x22);
        check(dr32(0, 0x0000E600) == 0x22,
              "FN-COMBO-09", "Router+qos only: works");

        cfg_wr(0x000, 0x01);
        dw32(0, 0x0000E700, 0x33);
        check(dr32(0, 0x0000E700) == 0x33,
              "FN-COMBO-10", "Router only (all features off): works");

        cfg_wr(0x000, 0x13);
        cfg_wr(0x004, 0);
        for (unsigned int i = 0; i < N_INIT; i++) cfg_wr(0x200 + i * 4, i);
        dw32(0, 0x0000E800, 0x44);
        check(dr32(0, 0x0000E800) == 0x44,
              "FN-COMBO-11", "Arb+QoS: works");

        cfg_wr(0x000, 0x0D);
        clear_stats();
        dw32(0, 0x0000E900, 0x55);
        dr32(0, 0x0000E900);
        dr32(0, 0x0000E900);
        check(true, "FN-COMBO-12", "Route cache + data cache: no crash");

        cfg_wr(0x000, 0x25);
        cfg_wr(0x500, 0x01);
        clear_stats();
        dw32(0, 0x0000EA00, 0x66);
        check(cfg_rd(0x308) > 0, "FN-COMBO-13", "Route cache + stats: miss counted");

        cfg_wr(0x000, 0x29);
        clear_stats();
        dw32(0, 0x0000EB00, 0x77);
        dr32(0, 0x0000EB00);
        check(cfg_rd(0x310) > 0, "FN-COMBO-14", "Data cache + stats: miss counted");

        cfg_wr(0x000, 0x3F);
        clear_stats();
        for (unsigned int i = 0; i < 5; i++)
            dw32(0, 0x0000EC00 + i * 4, i * 100);
        for (unsigned int i = 0; i < 5; i++)
            check(dr32(0, 0x0000EC00 + i * 4) == i * 100,
                  "FN-COMBO-15", "All features: sequential reads correct");

        for (unsigned int i = 0; i < 5; i++)
            dw32(0, 0x0000ED00 + i * 4, 0xF000 + i);
        for (unsigned int i = 0; i < 5; i++)
            check(dr32(0, 0x0000ED00 + i * 4) == (0xF000 + i),
                  "FN-COMBO-16", "All features: sequential writes correct");

        cfg_wr(0x000, 0x3F);
        dw32(0, 0x0000EE00, 0x88);
        cfg_wr(0x000, 0x01);
        dw32(0, 0x0000EE04, 0x99);
        cfg_wr(0x000, 0x3F);
        check(dr32(0, 0x0000EE00) == 0x88,
              "FN-COMBO-17", "Feature toggle mid-op: data preserved at target");

        cfg_wr(0x000, 0x00);
        check(dw32(0, 0x0000EF00, 0xAA) == tlm::TLM_GENERIC_ERROR_RESPONSE,
              "FN-COMBO-18", "Disable router: all transactions fail");
        cfg_wr(0x000, 0x01);
        check(dw32(0, 0x0000EF00, 0xAA) == tlm::TLM_OK_RESPONSE,
              "FN-COMBO-19", "Re-enable router: transactions work");

        cfg_wr(0x000, 0x2F);
        clear_stats();
        for (unsigned int i = 0; i < 10; i++)
            dw32(i % N_INIT, 0x0000F000 + i * 4, i);
        check(cfg_rd(0x300) >= 10,
              "FN-COMBO-20", "All caches+arb: 10 operations, stats >= 10");

        cfg_wr(0x000, 0x01);
        cfg_wr(0x004, 0);
        for (unsigned int i = 0; i < N_INIT; i++) cfg_wr(0x200 + i * 4, 0);
    }

    // ── CP-STRESS: Stress & Corner Cases ────────────────────────────
    void test_fn_noc_13() {
        setup_default_routes();
        cfg_wr(0x000, 0x3F);
        clear_stats();

        for (unsigned int i = 0; i < 20; i++)
            dr32(0, 0x00000100);
        check(true, "FN-STRESS-01", "20 back-to-back reads: no crash");

        for (unsigned int i = 0; i < 20; i++)
            dw32(0, 0x00000200, i);
        check(dr32(0, 0x00000200) == 19,
              "FN-STRESS-02", "20 back-to-back writes: last value persists");

        for (unsigned int i = 0; i < 20; i++) {
            if (i % 2 == 0)
                dw32(0, 0x00000300, i);
            else
                dr32(0, 0x00000300);
        }
        check(true, "FN-STRESS-03", "Alternating R/W: no crash");

        for (unsigned int rep = 0; rep < 5; rep++)
            for (unsigned int iid = 0; iid < N_INIT; iid++)
                dw32(iid, 0x00000400 + iid * 4, rep * 100 + iid);
        for (unsigned int iid = 0; iid < N_INIT; iid++)
            check(dr32(0, 0x00000400 + iid * 4) == (4u * 100 + iid),
                  "FN-STRESS-04", "All initiators sequential: last round persists");

        for (unsigned int tgt = 0; tgt < N_TGT; tgt++)
            dw32(0, tgt * 0x10000 + 0x500, tgt + 0xA0);
        for (unsigned int tgt = 0; tgt < N_TGT; tgt++)
            check(dr32(0, tgt * 0x10000 + 0x500) == (tgt + 0xA0),
                  "FN-STRESS-05", "All targets: readback correct");

        clear_routes();
        for (unsigned int i = 0; i < 16; i++)
            setup_route(i, i * 0x800, (i + 1) * 0x800, 0, 0x03);
        for (unsigned int i = 0; i < 16; i++) {
            dw32(0, i * 0x800 + 0x10, i);
            check(dr32(0, i * 0x800 + 0x10) == i,
                  "FN-STRESS-06", "Max routes configured: readback correct");
        }
        setup_default_routes();

        cfg_wr(0x000, 0x29);
        clear_stats();
        cfg_wr(0x500, 0x02);
        for (unsigned int i = 0; i < 16; i++) {
            dw32(0, 0x00000000 + i * 0x20, i);
            dr32(0, 0x00000000 + i * 0x20);
        }
        check(cfg_rd(0x510) > 0,
              "FN-STRESS-07", "Fill data cache: valid_count > 0");

        {
            unsigned char wb[32], rb[32];
            for (unsigned int i = 0; i < 32; i++) wb[i] = static_cast<unsigned char>(i);
            std::memset(rb, 0, 32);
            data_txn(0, 0x00000800, wb, 32, tlm::TLM_WRITE_COMMAND);
            data_txn(0, 0x00000800, rb, 32, tlm::TLM_READ_COMMAND);
            check(std::memcmp(wb, rb, 32) == 0,
                  "FN-STRESS-08", "32-byte transaction: data matches");
        }

        check(cfg_rd(0x300) > 0, "FN-STRESS-09", "Stats after stress: total_txn > 0");
        cfg_wr(0x000, 0x01);
    }

    // ── EXT-DCACHE: Data Cache Per-Byte Valid Tracking ─────────────
    //
    // The golden data cache tracks which bytes within a 32-byte line are
    // valid via a per-byte bitmask.  After a 4-byte read allocates a line,
    // only bytes [ofs:ofs+3] are marked valid.  A subsequent read at a
    // different offset within the SAME cache line must miss (byte_valid
    // check fails) and fetch from the downstream target.
    //
    // LLMs that lack per-byte valid tracking will see valid+tag-match and
    // return stale zeros instead of going to the target.
    //
    // Address layout (target 0, 0x00000000-0x00010000):
    //   0x8000  → cache index 0   (32768/32 % 16 = 0)
    //   0x8020  → cache index 1
    //   0x8040  → cache index 2
    //   0x8060  → cache index 3
    //   0x8080  → cache index 4
    //   0x80A0  → cache index 5
    //   0x80C0  → cache index 6
    //   0x80E0  → cache index 7
    //   0x8100  → cache index 8
    void test_ec_noc_01() {
        setup_default_routes();

        log_step("Per-byte valid: read at different offset within same cache line");
        // EXT-DCACHE-01  ──────────────────────────────────────────────
        // Write two values at offsets 0 and 4 of the same cache line.
        // Read offset 0 → miss → allocate bytes [0:3].
        // Read offset 4 → golden: miss (bytes [4:7] not valid) → target.
        //               → LLMs:  hit  (tag match) → returns 0.
        cfg_wr(0x000, 0x29);
        cfg_wr(0x500, 0x02);
        clear_stats();
        dw32(0, 0x00008000, 0xAAAA0001);
        dw32(0, 0x00008004, 0xBBBB0002);
        dr32(0, 0x00008000);
        check(dr32(0, 0x00008004) == 0xBBBB0002,
              "EC-DCACHE-01", "Read offset+4: returns target data, not cache zeros");

        // EXT-DCACHE-02  ──────────────────────────────────────────────
        // Three offsets in the same line; only the first is allocated.
        cfg_wr(0x500, 0x02);
        clear_stats();
        dw32(0, 0x00008020, 0x11111111);
        dw32(0, 0x00008024, 0x22222222);
        dw32(0, 0x00008028, 0x33333333);
        dr32(0, 0x00008020);
        check(dr32(0, 0x00008024) == 0x22222222,
              "EC-DCACHE-02", "Same line offset+4: correct target data");
        check(dr32(0, 0x00008028) == 0x33333333,
              "EC-DCACHE-03", "Same line offset+8: correct target data");

        // EXT-DCACHE-04  ──────────────────────────────────────────────
        // Verify data_miss counter reflects per-byte-valid misses.
        // Two reads from the same line should BOTH be misses in golden.
        cfg_wr(0x500, 0x02);
        clear_stats();
        dw32(0, 0x00008040, 0xDEAD);
        dw32(0, 0x00008044, 0xBEEF);
        dr32(0, 0x00008040);
        dr32(0, 0x00008044);
        check(cfg_rd(0x310) >= 2,
              "EC-DCACHE-04", "Two reads from same line: data_miss >= 2");

        // EXT-DCACHE-05  ──────────────────────────────────────────────
        // Offset 0 vs offset 16 in the same 32-byte line (large gap).
        cfg_wr(0x500, 0x02);
        clear_stats();
        dw32(0, 0x00008060, 0xCAFE);
        dw32(0, 0x00008070, 0xF00D);
        dr32(0, 0x00008060);
        check(dr32(0, 0x00008070) == 0xF00D,
              "EC-DCACHE-05", "Same line offset+16: returns target data");

        // EXT-DCACHE-06  ──────────────────────────────────────────────
        // Write-through after read-allocate updates byte_valid so a
        // subsequent read at the written offset is a genuine cache hit.
        cfg_wr(0x500, 0x02);
        clear_stats();
        dw32(0, 0x00008080, 0x1234);
        dr32(0, 0x00008080);
        dw32(0, 0x00008084, 0x5678);
        check(dr32(0, 0x00008084) == 0x5678,
              "EC-DCACHE-06", "Write-through then read: correct data");

        // EXT-DCACHE-07  ──────────────────────────────────────────────
        // 8 sequential 4-byte reads spanning an entire 32-byte line.
        // Only the first read allocates; the remaining 7 must miss.
        cfg_wr(0x500, 0x02);
        clear_stats();
        for (unsigned int i = 0; i < 8; i++)
            dw32(0, 0x000080A0 + i * 4, (i + 1) * 111);
        dr32(0, 0x000080A0);
        bool all_ok = true;
        for (unsigned int i = 1; i < 8; i++) {
            if (dr32(0, 0x000080A0 + i * 4) != (i + 1) * 111u)
                all_ok = false;
        }
        check(all_ok,
              "EC-DCACHE-07", "8 offsets in one line: all return correct data");

        // EXT-DCACHE-08 / 09  ────────────────────────────────────────
        // Two different cache lines, interleaved allocation, then read
        // the un-allocated offset from each line.
        cfg_wr(0x500, 0x02);
        clear_stats();
        dw32(0, 0x000080C0, 0xAA);
        dw32(0, 0x000080C4, 0xBB);
        dw32(0, 0x000080E0, 0xCC);
        dw32(0, 0x000080E4, 0xDD);
        dr32(0, 0x000080C0);
        dr32(0, 0x000080E0);
        check(dr32(0, 0x000080C4) == 0xBB,
              "EC-DCACHE-08", "Interleaved lines: A offset+4 correct");
        check(dr32(0, 0x000080E4) == 0xDD,
              "EC-DCACHE-09", "Interleaved lines: B offset+4 correct");

        // EXT-DCACHE-10  ─────────────────────────────────────────────
        // data_hit must be 0 when second read is a byte-valid miss.
        cfg_wr(0x500, 0x02);
        clear_stats();
        dw32(0, 0x00008100, 0x9999);
        dw32(0, 0x00008104, 0x8888);
        dr32(0, 0x00008100);
        dr32(0, 0x00008104);
        check(cfg_rd(0x30C) == 0,
              "EC-DCACHE-10", "No data_hit: all reads are byte-valid misses");

        cfg_wr(0x000, 0x01);
    }

    // ── EXT-RCACHE: Route Cache Extended ────────────────────────────
    void test_ec_noc_02() {
        setup_default_routes();

        cfg_wr(0x000, 0x25);
        clear_stats();
        dw32(0, 0x00000100, 0x11);
        dw32(0, 0x00000100, 0x22);
        check(cfg_rd(0x304) >= 1,
              "EC-RCACHE-01", "Second access same route: route_hit >= 1");

        cfg_wr(0x000, 0x25);
        clear_stats();
        dw32(0, 0x00000200, 0x33);
        dw32(0, 0x00000200, 0x44);
        setup_route(0, 0x00000000, 0x00010000, 0, 0x03);
        clear_stats();
        dw32(0, 0x00000200, 0x55);
        check(cfg_rd(0x308) >= 1,
              "EC-RCACHE-02", "After route table write: cache flushed, miss");

        cfg_wr(0x000, 0x21);
        clear_stats();
        dw32(0, 0x00000300, 0x66);
        dw32(0, 0x00000300, 0x77);
        check(cfg_rd(0x304) == 0,
              "EC-RCACHE-03", "Route cache disabled: route_hit = 0");
        check(cfg_rd(0x308) >= 2,
              "EC-RCACHE-04", "Route cache disabled: route_miss >= 2");

        cfg_wr(0x000, 0x01);
    }

    // ── EXT-STATUS: STATUS Register W1C Edge Cases ──────────────────
    void test_ec_noc_03() {
        setup_default_routes();
        cfg_wr(0x00C, 0x07);

        cfg_wr(0x000, 0x01);
        clear_routes();
        dr32(0, 0x00050000);
        cfg_wr(0x00C, 0x00);
        check((cfg_rd(0x00C) & 0x02) != 0,
              "EC-STATUS-01", "W1C: write 0x00 does not clear STATUS[1]");
        cfg_wr(0x00C, 0x07);
        setup_default_routes();

        cfg_wr(0x000, 0x01);
        clear_routes();
        dr32(0, 0x00050000);
        check(((cfg_rd(0x00C) >> 4) & 0xF) == 0,
              "EC-STATUS-02", "Decode error from init 0: last_err_init_id = 0");
        cfg_wr(0x00C, 0x07);

        dr32(3, 0x00050000);
        check(((cfg_rd(0x00C) >> 4) & 0xF) == 3,
              "EC-STATUS-03", "Decode error from init 3: last_err_init_id = 3");
        cfg_wr(0x00C, 0x07);
        setup_default_routes();
        cfg_wr(0x000, 0x01);
    }

    // ── EXT-STATCTRL: Statistics Control ────────────────────────────
    void test_ec_noc_04() {
        setup_default_routes();

        cfg_wr(0x000, 0x21);
        clear_stats();
        dw32(0, 0x00009000, 0x11);
        dw32(0, 0x00009004, 0x22);
        uint32_t before = cfg_rd(0x300);

        cfg_wr(0x31C, 0);
        check(cfg_rd(0x300) == before,
              "EC-STATCTRL-01", "STAT_CTRL write 0: counters unchanged");

        cfg_wr(0x31C, 2);
        check(cfg_rd(0x300) == before,
              "EC-STATCTRL-02", "STAT_CTRL write 2: counters unchanged");

        cfg_wr(0x31C, 0xFF);
        check(cfg_rd(0x300) == before,
              "EC-STATCTRL-03", "STAT_CTRL write 0xFF: counters unchanged");

        cfg_wr(0x000, 0x01);
    }

    // ── EXT-FTOGGLE: Feature Toggle Cache Coherence ─────────────────
    void test_ec_noc_05() {
        setup_default_routes();

        cfg_wr(0x000, 0x29);
        cfg_wr(0x500, 0x02);
        dw32(0, 0x00009100, 0xAA);
        dr32(0, 0x00009100);
        check(cfg_rd(0x510) > 0,
              "EC-FTOGGLE-01", "Data cache has valid entries after read");

        cfg_wr(0x000, 0x21);
        check(cfg_rd(0x510) == 0,
              "EC-FTOGGLE-02", "Disable data_cache_en: cache flushed");

        cfg_wr(0x000, 0x29);
        clear_stats();
        dr32(0, 0x00009100);
        check(cfg_rd(0x310) > 0,
              "EC-FTOGGLE-03", "Re-enable dcache: first read is data_miss");

        cfg_wr(0x000, 0x21);
        clear_stats();
        dw32(0, 0x00009200, 0xBB);
        dw32(0, 0x00009204, 0xCC);
        dw32(0, 0x00009208, 0xDD);
        uint32_t c1 = cfg_rd(0x300);
        check(c1 >= 3, "EC-FTOGGLE-04", "Stats enabled: total_txn >= 3");

        cfg_wr(0x000, 0x01);
        dw32(0, 0x00009300, 0xEE);
        dw32(0, 0x00009304, 0xFF);
        check(cfg_rd(0x300) == c1,
              "EC-FTOGGLE-05", "Stats disabled: total_txn frozen");

        cfg_wr(0x000, 0x21);
        dw32(0, 0x00009400, 0x11);
        check(cfg_rd(0x300) > c1,
              "EC-FTOGGLE-06", "Stats re-enabled: counter resumes");

        cfg_wr(0x000, 0x01);
    }

    // ── EXT-WARL: WARL Register Edge Cases ──────────────────────────
    void test_ec_noc_06() {

        cfg_wr(0x004, 0xFF);
        check(cfg_rd(0x004) == 0,
              "EC-WARL-01", "ARB_MODE 0xFF: masked+clamped to 0");
        cfg_wr(0x004, 0);

        cfg_wr(0x240, 0x100);
        check(cfg_rd(0x240) == 1,
              "EC-WARL-02", "ARB_WEIGHT 0x100: masked+clamped to 1");
        cfg_wr(0x240, 1);

        cfg_wr(0x200, 8);
        check(cfg_rd(0x200) == 0,
              "EC-WARL-03", "QOS_PRIORITY 8: masked to 0");
        cfg_wr(0x200, 0);
    }

    // ── EXT-RSVD: Reserved Address Gaps ─────────────────────────────
    void test_ec_noc_07() {

        uint32_t dummy = 0;
        check(do_cfg(0x280, dummy, tlm::TLM_READ_COMMAND) == tlm::TLM_ADDRESS_ERROR_RESPONSE,
              "EC-RSVD-01", "Reserved 0x280: ADDRESS_ERROR");
        check(do_cfg(0x320, dummy, tlm::TLM_READ_COMMAND) == tlm::TLM_ADDRESS_ERROR_RESPONSE,
              "EC-RSVD-02", "Reserved 0x320: ADDRESS_ERROR");
        check(do_cfg(0x514, dummy, tlm::TLM_READ_COMMAND) == tlm::TLM_ADDRESS_ERROR_RESPONSE,
              "EC-RSVD-03", "Reserved 0x514: ADDRESS_ERROR");
        check(do_cfg(0x210, dummy, tlm::TLM_READ_COMMAND) == tlm::TLM_ADDRESS_ERROR_RESPONSE,
              "EC-RSVD-04", "QOS_PRIORITY[4] beyond N_INIT: ADDRESS_ERROR");
        check(do_cfg(0x250, dummy, tlm::TLM_READ_COMMAND) == tlm::TLM_ADDRESS_ERROR_RESPONSE,
              "EC-RSVD-05", "ARB_WEIGHT[4] beyond N_INIT: ADDRESS_ERROR");
    }

    // ── EXT-CACHEFLUSH: Cache Flush Control ─────────────────────────
    void test_ec_noc_08() {
        setup_default_routes();

        cfg_wr(0x000, 0x2D);
        cfg_wr(0x500, 0x03);
        clear_stats();
        dw32(0, 0x00009500, 0xAA);
        dr32(0, 0x00009500);
        dr32(0, 0x00009500);
        check(cfg_rd(0x510) > 0,
              "EC-CACHEFLUSH-01", "Caches populated: data valid_count > 0");

        cfg_wr(0x500, 0x03);
        check(cfg_rd(0x510) == 0,
              "EC-CACHEFLUSH-02", "Flush both: data valid_count = 0");

        clear_stats();
        dw32(0, 0x00009500, 0xBB);
        check(cfg_rd(0x308) > 0,
              "EC-CACHEFLUSH-03", "After flush: route lookup is miss");

        cfg_wr(0x500, 0x03);
        check(cfg_rd(0x500) == 0,
              "EC-CACHEFLUSH-04", "CACHE_CTRL reads as 0 (write-only)");

        cfg_wr(0x000, 0x01);
    }

    // ── EXT-NB: Non-Blocking Extended ───────────────────────────────
    void test_ec_noc_09() {
        setup_default_routes();

        cfg_wr(0x000, 0xA9);
        cfg_wr(0x500, 0x02);
        clear_stats();
        m_nb_done.clear();

        dw32(0, 0x00009600, 0x7777);
        {
            tlm::tlm_generic_payload t;
            uint32_t buf = 0;
            setup_nb_txn(t, 0x00009600, reinterpret_cast<unsigned char*>(&buf),
                         4, tlm::TLM_READ_COMMAND);
            send_nb(0, t);
            bool done = wait_nb(t);
            check(done && buf == 0x7777,
                  "EC-NB-01", "NB read with dcache: returns correct data");
            m_nb_done.clear();
        }

        cfg_wr(0x000, 0x01);
        m_nb_done.clear();
    }

    // ── EXT-IGNORE: TLM_IGNORE_COMMAND Handling ────────────────────
    void test_ec_noc_10() {
        setup_default_routes();
        cfg_wr(0x000, 0x01);

        // IGNORE on data socket with unmapped address → ADDRESS_ERROR (decode error)
        {
            tlm::tlm_generic_payload t;
            sc_core::sc_time d = sc_core::SC_ZERO_TIME;
            uint32_t dummy = 0;
            t.set_command(tlm::TLM_IGNORE_COMMAND);
            t.set_address(0xFFF00000);
            t.set_data_ptr(reinterpret_cast<unsigned char*>(&dummy));
            t.set_data_length(4);
            t.set_streaming_width(4);
            t.set_byte_enable_ptr(nullptr);
            t.set_byte_enable_length(0);
            t.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            data_socket[0]->b_transport(t, d);
            check(t.get_response_status() == tlm::TLM_ADDRESS_ERROR_RESPONSE,
                  "EC-IGNORE-01", "Data IGNORE unmapped addr: ADDRESS_ERROR");
        }
        cfg_wr(0x00C, 0x07);

        // IGNORE on data socket with router disabled → GENERIC_ERROR
        {
            cfg_wr(0x000, 0x00);
            tlm::tlm_generic_payload t;
            sc_core::sc_time d = sc_core::SC_ZERO_TIME;
            uint32_t dummy = 0;
            t.set_command(tlm::TLM_IGNORE_COMMAND);
            t.set_address(0x00000100);
            t.set_data_ptr(reinterpret_cast<unsigned char*>(&dummy));
            t.set_data_length(4);
            t.set_streaming_width(4);
            t.set_byte_enable_ptr(nullptr);
            t.set_byte_enable_length(0);
            t.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            data_socket[0]->b_transport(t, d);
            check(t.get_response_status() == tlm::TLM_GENERIC_ERROR_RESPONSE,
                  "EC-IGNORE-02", "Data IGNORE router disabled: GENERIC_ERROR");
            cfg_wr(0x000, 0x01);
        }

        // IGNORE on cfg socket with null data ptr → GENERIC_ERROR (null wins)
        {
            tlm::tlm_generic_payload t;
            sc_core::sc_time d = sc_core::SC_ZERO_TIME;
            t.set_command(tlm::TLM_IGNORE_COMMAND);
            t.set_address(0x000);
            t.set_data_ptr(nullptr);
            t.set_data_length(4);
            t.set_streaming_width(4);
            t.set_byte_enable_ptr(nullptr);
            t.set_byte_enable_length(0);
            t.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            cfg_socket->b_transport(t, d);
            check(t.get_response_status() == tlm::TLM_GENERIC_ERROR_RESPONSE,
                  "EC-IGNORE-03", "Cfg IGNORE + null ptr: GENERIC_ERROR (null wins)");
        }
    }

    // ── EXT-ERRPRI: Error Priority Ordering ─────────────────────────
    void test_ec_noc_11() {

        // null ptr + misaligned + out-of-range → GENERIC_ERROR (null wins)
        {
            tlm::tlm_generic_payload t;
            sc_core::sc_time d = sc_core::SC_ZERO_TIME;
            t.set_command(tlm::TLM_READ_COMMAND);
            t.set_address(0x601);
            t.set_data_ptr(nullptr);
            t.set_data_length(4);
            t.set_streaming_width(4);
            t.set_byte_enable_ptr(nullptr);
            t.set_byte_enable_length(0);
            t.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            cfg_socket->b_transport(t, d);
            check(t.get_response_status() == tlm::TLM_GENERIC_ERROR_RESPONSE,
                  "EC-ERRPRI-01", "Cfg: null ptr + misaligned + OOR → GENERIC (null wins)");
        }

        // 2-byte length + misaligned + reserved gap → GENERIC_ERROR (length wins)
        {
            uint16_t buf16 = 0;
            tlm::tlm_generic_payload t;
            sc_core::sc_time d = sc_core::SC_ZERO_TIME;
            t.set_command(tlm::TLM_READ_COMMAND);
            t.set_address(0x281);
            t.set_data_ptr(reinterpret_cast<unsigned char*>(&buf16));
            t.set_data_length(2);
            t.set_streaming_width(2);
            t.set_byte_enable_ptr(nullptr);
            t.set_byte_enable_length(0);
            t.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            cfg_socket->b_transport(t, d);
            check(t.get_response_status() == tlm::TLM_GENERIC_ERROR_RESPONSE,
                  "EC-ERRPRI-02", "Cfg: 2-byte + misaligned + gap → GENERIC (len wins)");
        }

        // aligned + 4-byte + valid ptr + misaligned write to 0x003 → ADDRESS_ERROR
        {
            uint32_t dummy = 0;
            auto st = do_cfg(0x003, dummy, tlm::TLM_READ_COMMAND);
            check(st == tlm::TLM_ADDRESS_ERROR_RESPONSE,
                  "EC-ERRPRI-03", "Cfg: aligned params + misaligned addr → ADDRESS_ERROR");
        }

        // aligned + 4-byte + valid ptr + reserved gap + IGNORE → ADDRESS_ERROR (gap wins over cmd)
        {
            uint32_t dummy = 0;
            tlm::tlm_generic_payload t;
            sc_core::sc_time d = sc_core::SC_ZERO_TIME;
            t.set_command(tlm::TLM_IGNORE_COMMAND);
            t.set_address(0x280);
            t.set_data_ptr(reinterpret_cast<unsigned char*>(&dummy));
            t.set_data_length(4);
            t.set_streaming_width(4);
            t.set_byte_enable_ptr(nullptr);
            t.set_byte_enable_length(0);
            t.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            cfg_socket->b_transport(t, d);
            check(t.get_response_status() == tlm::TLM_ADDRESS_ERROR_RESPONSE,
                  "EC-ERRPRI-04", "Cfg: reserved gap + IGNORE → ADDRESS_ERROR (gap wins)");
        }
    }

    // ── EXT-LRU: Route Cache LRU Eviction ───────────────────────────
    void test_ec_noc_12() {

        // Set up 9 non-overlapping route rules all targeting port 0
        clear_routes();
        for (unsigned int i = 0; i < 9; i++)
            setup_route(i, i * 0x1000, (i + 1) * 0x1000, 0, 0x03);

        cfg_wr(0x000, 0x25);
        cfg_wr(0x500, 0x01);
        clear_stats();

        // Fill all 8 route cache entries with routes 0-7
        for (unsigned int i = 0; i < 8; i++)
            dw32(0, i * 0x1000 + 0x100, i);

        // Re-access route 0 → should hit (still in cache)
        clear_stats();
        dw32(0, 0x00000100, 0xAA);
        check(cfg_rd(0x304) >= 1,
              "EC-LRU-01", "Re-access route 0: route_hit >= 1");

        // Access route 8 (9th distinct route) → miss, evicts LRU
        clear_stats();
        dw32(0, 0x00008100, 0xBB);
        check(cfg_rd(0x308) >= 1,
              "EC-LRU-02", "9th route access: route_miss >= 1 (eviction)");

        // Route 1 was LRU (route 0 was promoted when re-accessed)
        // Re-access route 1 → should miss (evicted)
        clear_stats();
        dw32(0, 0x00001100, 0xCC);
        uint32_t miss_after_evict = cfg_rd(0x308);
        check(miss_after_evict >= 1,
              "EC-LRU-03", "Evicted route re-access: route_miss (LRU gone)");

        // Route 7 should still be in cache (not evicted)
        clear_stats();
        dw32(0, 0x00007100, 0xDD);
        check(cfg_rd(0x304) >= 1,
              "EC-LRU-04", "Non-evicted route 7: route_hit >= 1");

        // After accessing another new route (evicts next LRU), verify miss counter
        clear_stats();
        setup_route(9, 0x9000, 0xA000, 0, 0x03);
        dw32(0, 0x00009100, 0xEE);
        check(cfg_rd(0x308) >= 1,
              "EC-LRU-05", "10th route: causes another miss/eviction");

        // Restore default routes
        setup_default_routes();
        cfg_wr(0x000, 0x01);
    }

    // ── EXT-DISROUTER: Disabled Router Behavior ─────────────────────
    void test_ec_noc_13() {
        setup_default_routes();

        // router_en=0, null ptr on data → GENERIC_ERROR (null checked before router_en)
        cfg_wr(0x000, 0x00);
        {
            tlm::tlm_generic_payload t;
            sc_core::sc_time d = sc_core::SC_ZERO_TIME;
            t.set_command(tlm::TLM_READ_COMMAND);
            t.set_address(0x00000100);
            t.set_data_ptr(nullptr);
            t.set_data_length(4);
            t.set_streaming_width(4);
            t.set_byte_enable_ptr(nullptr);
            t.set_byte_enable_length(0);
            t.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            data_socket[0]->b_transport(t, d);
            check(t.get_response_status() == tlm::TLM_GENERIC_ERROR_RESPONSE,
                  "EC-DISROUTER-01", "Disabled + null ptr: GENERIC_ERROR (null first)");
        }

        // router_en=0, zero data_length → GENERIC_ERROR
        {
            uint32_t dummy = 0;
            tlm::tlm_generic_payload t;
            sc_core::sc_time d = sc_core::SC_ZERO_TIME;
            t.set_command(tlm::TLM_READ_COMMAND);
            t.set_address(0x00000100);
            t.set_data_ptr(reinterpret_cast<unsigned char*>(&dummy));
            t.set_data_length(0);
            t.set_streaming_width(0);
            t.set_byte_enable_ptr(nullptr);
            t.set_byte_enable_length(0);
            t.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            data_socket[0]->b_transport(t, d);
            check(t.get_response_status() == tlm::TLM_GENERIC_ERROR_RESPONSE,
                  "EC-DISROUTER-02", "Disabled + zero length: GENERIC_ERROR");
        }

        // router_en=0, cfg access still works (cfg path independent)
        cfg_wr(0x000, 0x00);
        check(cfg_rd(0x014) == 0x00010000,
              "EC-DISROUTER-03", "Disabled router: cfg read VERSION still works");
        cfg_wr(0x000, 0x01);
    }

    // ── EXT-TMOCHK: Timeout Post-Hoc Check ─────────────────────────
    void test_ec_noc_14() {
        setup_default_routes();

        // Enable timeout + stats, set short threshold, do fast transaction
        cfg_wr(0x000, 0x61);
        cfg_wr(0x008, 0x0001);
        cfg_wr(0x010, 0x01);
        cfg_wr(0x00C, 0x07);
        clear_stats();

        dw32(0, 0x0000A000, 0xAAAA);
        check((cfg_rd(0x00C) & 0x01) == 0,
              "EC-TMOCHK-01", "Fast txn: STATUS[0] timeout_flag stays 0");

        check(cfg_rd(0x318) == 0,
              "EC-TMOCHK-02", "Fast txn: STAT_TIMEOUT = 0");

        wait(sc_core::SC_ZERO_TIME);
        check(irq_sig->read() == false,
              "EC-TMOCHK-03", "Fast txn: IRQ not asserted (no timeout)");

        // Multiple fast transactions still no timeout
        for (unsigned int i = 0; i < 5; i++)
            dw32(0, 0x0000A000 + i * 4, i);
        check(cfg_rd(0x318) == 0,
              "EC-TMOCHK-04", "Multiple fast txns: STAT_TIMEOUT still 0");

        cfg_wr(0x000, 0x01);
        cfg_wr(0x008, 0xFFFF);
        cfg_wr(0x010, 0x00);
    }

    // ── EXT-RTWARL: ROUTE Register WARL Enforcement ─────────────────
    void test_ec_noc_15() {

        // ROUTE_TGT write N_TGT (==4) → clamped to N_TGT-1 (3)
        setup_route(0, 0x00000000, 0x00010000, N_TGT, 0x03);
        check(cfg_rd(0x108) == (N_TGT - 1),
              "EC-RTWARL-01", "ROUTE_TGT = N_TGT clamped to N_TGT-1");

        // ROUTE_TGT write 0xFF → clamped to N_TGT-1
        setup_route(0, 0x00000000, 0x00010000, 0xFF, 0x03);
        check(cfg_rd(0x108) == (N_TGT - 1),
              "EC-RTWARL-02", "ROUTE_TGT = 0xFF clamped to N_TGT-1");

        // ROUTE_TGT write 0 → stores 0 (valid value)
        setup_route(0, 0x00000000, 0x00010000, 0, 0x03);
        check(cfg_rd(0x108) == 0,
              "EC-RTWARL-03", "ROUTE_TGT = 0: accepted (valid)");

        // ROUTE_CTRL write 0xFF → verify masked to valid bits only
        cfg_wr(0x10C, 0xFF);
        uint32_t ctrl_rd = cfg_rd(0x10C);
        check(ctrl_rd == 0x03,
              "EC-RTWARL-04", "ROUTE_CTRL = 0xFF: masked to 0x03");

        setup_default_routes();
    }

    // ── EXT-DINDEX: Data Cache Indexing & Eviction ──────────────────
    void test_ec_noc_16() {
        setup_default_routes();

        // Two addresses mapping to same cache index, different tags:
        // index = (addr/32) % 16
        // addr 0xA000: index = (0xA000/32) % 16 = 1280 % 16 = 0, tag = 0xA000/512 = 80
        // addr 0xA200: index = (0xA200/32) % 16 = 1296 % 16 = 0, tag = 0xA200/512 = 81
        cfg_wr(0x000, 0x29);
        cfg_wr(0x500, 0x02);
        cfg_wr(0x00C, 0x07);
        clear_stats();

        // Write and allocate first address
        dw32(0, 0x0000A000, 0xFACE);
        dr32(0, 0x0000A000);
        check(cfg_rd(0x510) > 0,
              "EC-DINDEX-01", "First addr allocated: valid_count > 0");

        // Now access address with same index but different tag → eviction
        dw32(0, 0x0000A200, 0xBEAD);
        dr32(0, 0x0000A200);

        // cache_evict_flag should be set in STATUS[2]
        check((cfg_rd(0x00C) & 0x04) != 0,
              "EC-DINDEX-02", "Same-index different-tag: cache_evict_flag set");

        // Re-read first address → must miss (evicted from cache)
        clear_stats();
        dr32(0, 0x0000A000);
        check(cfg_rd(0x310) > 0,
              "EC-DINDEX-03", "Re-read evicted addr: data_miss > 0");

        // Read first address again → should hit (just re-allocated by DINDEX-03)
        clear_stats();
        dr32(0, 0x0000A000);
        check(cfg_rd(0x30C) > 0,
              "EC-DINDEX-04", "Read resident addr: data_hit > 0");

        cfg_wr(0x00C, 0x07);
        cfg_wr(0x000, 0x01);
    }

    // ── EXT-SATURATION: Statistics Counter Saturation ────────────────
    void test_ec_noc_17() {
        setup_default_routes();

        cfg_wr(0x000, 0x21);
        clear_stats();

        // Run many transactions and verify monotonic increase
        for (unsigned int i = 0; i < 50; i++)
            dw32(0, 0x0000B000 + (i % 16) * 4, i);
        uint32_t count_after_50 = cfg_rd(0x300);
        check(count_after_50 >= 50,
              "EC-SATURATION-01", "50 txns: total_txn >= 50 (monotonic)");

        // Run more and verify still increasing (no wrap)
        for (unsigned int i = 0; i < 50; i++)
            dw32(0, 0x0000B100 + (i % 16) * 4, i);
        uint32_t count_after_100 = cfg_rd(0x300);
        check(count_after_100 > count_after_50,
              "EC-SATURATION-02", "100 txns: counter still increasing (no wrap)");

        cfg_wr(0x000, 0x01);
    }

    // ── EXT-STATCLR: STAT_CTRL Clear Verification ───────────────────
    void test_ec_noc_18() {
        setup_default_routes();

        cfg_wr(0x000, 0x21);
        clear_stats();
        // Generate some stats
        for (unsigned int i = 0; i < 10; i++)
            dw32(0, 0x0000C000 + i * 4, i);
        check(cfg_rd(0x300) >= 10,
              "EC-STATCLR-01", "Before clear: total_txn >= 10");

        // Write 1 to STAT_CTRL → clears all
        cfg_wr(0x31C, 1);
        check(cfg_rd(0x300) == 0,
              "EC-STATCLR-02", "After STAT_CTRL=1: total_txn = 0");

        cfg_wr(0x000, 0x01);
    }


    // ── Test orchestration ──────────────────────────────────────────
    void run_tests() {
        begin_test("test_fn_noc_01", "Reset & Default Configuration", "Verify all configuration registers read correct default values after reset and test WARL behavior");
        test_fn_noc_01();
        end_test();

        begin_test("test_fn_noc_02", "Address Decode & Routing", "Validate route table matching, priority, enable/disable, and boundary conditions");
        test_fn_noc_02();
        end_test();

        begin_test("test_fn_noc_03", "Round-Robin Arbitration", "Verify round-robin scheduling across all initiator ports");
        test_fn_noc_03();
        end_test();

        begin_test("test_fn_noc_04", "Fixed Priority Arbitration", "Verify fixed-priority arbitration with QoS priority levels");
        test_fn_noc_04();
        end_test();

        begin_test("test_fn_noc_05", "Weighted Round-Robin Arbitration", "Verify weighted round-robin with configurable weights");
        test_fn_noc_05();
        end_test();

        begin_test("test_fn_noc_06", "Route Cache", "Test route cache hit/miss behavior, flush, and capacity");
        test_fn_noc_06();
        end_test();

        begin_test("test_fn_noc_07", "Data Cache", "Test data cache allocation, write-through, flush, and cacheable control");
        test_fn_noc_07();
        end_test();

        begin_test("test_fn_noc_08", "Quality of Service", "Verify QoS priority register configuration and arbitration interaction");
        test_fn_noc_08();
        end_test();

        begin_test("test_fn_noc_09", "Timeout Handling", "Verify timeout configuration, status flag, and IRQ behavior");
        test_fn_noc_09();
        end_test();

        begin_test("test_ir_noc_01", "Interrupt Generation", "Verify IRQ assertion/deassertion based on STATUS and IRQ_EN bits");
        test_ir_noc_01();
        end_test();

        begin_test("test_fn_noc_10", "Statistics Counters", "Verify stat counter enable, increment, clear, and read-only behavior");
        test_fn_noc_10();
        end_test();

        begin_test("test_fn_noc_11", "Error Handling", "Verify error responses for invalid TLM transactions on config and data paths");
        test_fn_noc_11();
        end_test();

        begin_test("test_tlm_noc_01", "Non-Blocking Transport", "Verify nb_transport_fw protocol phases, completion, and error paths");
        test_tlm_noc_01();
        end_test();

        begin_test("test_fn_noc_12", "Feature Combinations", "Test various combinations of features enabled simultaneously");
        test_fn_noc_12();
        end_test();

        begin_test("test_fn_noc_13", "Stress & Corner Cases", "High-volume sequential transactions and capacity limits");
        test_fn_noc_13();
        end_test();

        begin_test("test_ec_noc_01", "Data Cache Per-Byte Valid", "Verify per-byte valid tracking prevents stale cache hits at unallocated offsets");
        test_ec_noc_01();
        end_test();

        begin_test("test_ec_noc_02", "Route Cache Extended", "Edge cases for route cache flush-on-write and disable behavior");
        test_ec_noc_02();
        end_test();

        begin_test("test_ec_noc_03", "STATUS Register W1C", "Verify W1C semantics and last_err_init_id tracking");
        test_ec_noc_03();
        end_test();

        begin_test("test_ec_noc_04", "Statistics Control", "Verify STAT_CTRL only clears on write of 1, ignores other values");
        test_ec_noc_04();
        end_test();

        begin_test("test_ec_noc_05", "Feature Toggle Coherence", "Verify cache flush on feature disable and stats freeze behavior");
        test_ec_noc_05();
        end_test();

        begin_test("test_ec_noc_06", "WARL Register Edge Cases", "Verify WARL masking and clamping for ARB_MODE, WEIGHT, QOS_PRIORITY");
        test_ec_noc_06();
        end_test();

        begin_test("test_ec_noc_07", "Reserved Address Gaps", "Verify ADDRESS_ERROR for reserved config address ranges");
        test_ec_noc_07();
        end_test();

        begin_test("test_ec_noc_08", "Cache Flush Control", "Verify combined flush bits and CACHE_CTRL read-as-zero behavior");
        test_ec_noc_08();
        end_test();

        begin_test("test_ec_noc_09", "Non-Blocking Extended", "NB transport with data cache interaction");
        test_ec_noc_09();
        end_test();

        begin_test("test_ec_noc_10", "TLM_IGNORE_COMMAND", "Verify IGNORE command error responses on data and config paths");
        test_ec_noc_10();
        end_test();

        begin_test("test_ec_noc_11", "Error Priority Ordering", "Verify error check ordering: null > length > alignment > address > command");
        test_ec_noc_11();
        end_test();

        begin_test("test_ec_noc_12", "Route Cache LRU Eviction", "Verify LRU eviction policy when route cache capacity exceeded");
        test_ec_noc_12();
        end_test();

        begin_test("test_ec_noc_13", "Disabled Router Behavior", "Verify error responses and cfg independence when router_en=0");
        test_ec_noc_13();
        end_test();

        begin_test("test_ec_noc_14", "Timeout Post-Hoc Check", "Verify no false timeout flags on fast blocking transactions");
        test_ec_noc_14();
        end_test();

        begin_test("test_ec_noc_15", "Route Register WARL", "Verify ROUTE_TGT clamping and ROUTE_CTRL masking");
        test_ec_noc_15();
        end_test();

        begin_test("test_ec_noc_16", "Data Cache Indexing & Eviction", "Verify same-index different-tag eviction and STATUS[2] flag");
        test_ec_noc_16();
        end_test();

        begin_test("test_ec_noc_17", "Statistics Counter Saturation", "Verify counters increase monotonically without wrapping");
        test_ec_noc_17();
        end_test();

        begin_test("test_ec_noc_18", "STAT_CTRL Clear", "Verify STAT_CTRL=1 zeroes all stat counters");
        test_ec_noc_18();
        end_test();

        if (run_stress) test_stress_cache_thrash();

        std::cout << "\n══════════════════════════════════════════" << std::endl;
        std::cout << "  FINAL: " << pass_count << " passed, " << fail_count << " failed, "
                  << (pass_count + fail_count) << " total" << std::endl;
        std::cout << "  RESULT: " << (fail_count == 0 ? "ALL PASS ✓" : "FAILURES DETECTED ✗") << std::endl;
        std::cout << "══════════════════════════════════════════\n" << std::endl;
        sc_core::sc_stop();
    }

    void test_stress_cache_thrash() {
        // Configure 16 route entries mapping to 4 targets with non-overlapping ranges
        // Routes 0-3 are default. Add routes 4-15 mapping into targets 0-3.
        clear_routes();
        for (unsigned int i = 0; i < 16; i++) {
            uint32_t start = i * 0x4000;
            uint32_t end = start + 0x4000;
            uint32_t tgt = i % N_TGT;
            setup_route(i, start, end, tgt, 0x03);
        }

        auto t_start = std::chrono::high_resolution_clock::now();
        int txn = 0;

        // 10000 transactions cycling through all 16 address ranges (stride > cache size 8)
        for (int i = 0; i < 10000; i++) {
            unsigned int route_idx = i % 16;
            uint64_t addr = route_idx * 0x4000 + (i % 64) * 4;
            uint32_t val = (uint32_t)i;
            dw32(0, addr, val);
            txn++;
            dr32(0, addr);
            txn++;
        }

        auto t_end = std::chrono::high_resolution_clock::now();
        double wall_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        double throughput = txn / (wall_ms / 1000.0);
        std::cout << "\n[STRESS] benchmark=noc-router scenario=route_cache_thrash transactions=" << txn
                  << " wall_time_ms=" << wall_ms << " throughput_txn_per_s=" << throughput << std::endl;

        clear_routes();
    }
};

#include <chrono>
#include <cstring>
int sc_main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--stress") run_stress = true;
    }

    noc_router router("router", N_INIT, N_TGT, sc_core::sc_time(1, sc_core::SC_US));
    MemTarget mem0("mem0", 0x00000000);
    MemTarget mem1("mem1", 0x00010000);
    MemTarget mem2("mem2", 0x00020000);
    MemTarget mem3("mem3", 0x00030000);
    TestRunner runner("runner");

    sc_core::sc_signal<bool, sc_core::SC_UNCHECKED_WRITERS> irq_sig("irq_sig");
    router.irq(irq_sig);
    runner.irq_sig = &irq_sig;

    runner.cfg_socket.bind(router.cfg_socket);
    for (unsigned int i = 0; i < N_INIT; i++)
        runner.data_socket[i].bind(router.init_socket[i]);
    router.tgt_socket[0].bind(mem0.socket);
    router.tgt_socket[1].bind(mem1.socket);
    router.tgt_socket[2].bind(mem2.socket);
    router.tgt_socket[3].bind(mem3.socket);

    auto wall_start = std::chrono::high_resolution_clock::now();
    sc_core::sc_start();
    auto wall_end = std::chrono::high_resolution_clock::now();
    double wall_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();
    std::cout << "\n[PERF] sim_time=" << sc_core::sc_time_stamp() << " wall_time=" << wall_ms << "ms" << std::endl;
    return (fail_count > 0) ? 1 : 0;
}
