#ifndef NOC_ROUTER_H
#define NOC_ROUTER_H
//
// noc_router — Configurable NoC crossbar router with TLM-2.0 blocking and
// non-blocking transport, multi-mode arbitration, route/data caches, QoS
// priority, statistics counters, timeout monitoring, and interrupt support.
//

#include "noc_arbiter.h"
#include "noc_cache.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <systemc>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <vector>

// SC_MODULE implementing a parameterised N×M crossbar router.
// Supports round-robin, fixed-priority, and weighted-round-robin arbitration.
class noc_router : public sc_core::sc_module {
  public:
    // Total configuration address space size (1.5 KiB).
    static constexpr uint64_t CFG_ADDR_SPACE = 0x600;

    // ── Core control/status registers (0x000 – 0x01C) ──
    static constexpr uint64_t REG_GLOBAL_CTRL = 0x000;
    static constexpr uint64_t REG_ARB_MODE    = 0x004;
    static constexpr uint64_t REG_TIMEOUT_CFG = 0x008;
    static constexpr uint64_t REG_STATUS      = 0x00C;
    static constexpr uint64_t REG_IRQ_EN      = 0x010;
    static constexpr uint64_t REG_VERSION     = 0x014;
    static constexpr uint64_t REG_N_INIT_RO   = 0x018;
    static constexpr uint64_t REG_N_TGT_RO    = 0x01C;

    // ── Route table region (0x100 – 0x1FF): 16 entries × 16 bytes each ──
    static constexpr uint64_t     ROUTE_BASE       = 0x100;
    static constexpr uint64_t     ROUTE_REGION_END = 0x200;
    static constexpr unsigned int MAX_ROUTES       = 16;
    static constexpr unsigned int ROUTE_ENTRY_SIZE = 0x10;

    // Byte offsets within each 16-byte route table entry.
    static constexpr unsigned int ROUTE_OFF_START = 0x00;
    static constexpr unsigned int ROUTE_OFF_END   = 0x04;
    static constexpr unsigned int ROUTE_OFF_TGT   = 0x08;
    static constexpr unsigned int ROUTE_OFF_CTRL  = 0x0C;

    // ── QoS and weight regions (0x200 – 0x27F) ──
    static constexpr uint64_t QOS_BASE          = 0x200;
    static constexpr uint64_t QOS_REGION_END    = 0x240;
    static constexpr uint64_t WEIGHT_BASE       = 0x240;
    static constexpr uint64_t WEIGHT_REGION_END = 0x280;

    // ── Statistics counters (0x300 – 0x31F): 32-bit saturating, R/O ──
    static constexpr uint64_t STAT_BASE           = 0x300;
    static constexpr uint64_t REG_STAT_TOTAL_TXN  = 0x300;
    static constexpr uint64_t REG_STAT_ROUTE_HIT  = 0x304;
    static constexpr uint64_t REG_STAT_ROUTE_MISS = 0x308;
    static constexpr uint64_t REG_STAT_DATA_HIT   = 0x30C;
    static constexpr uint64_t REG_STAT_DATA_MISS  = 0x310;
    static constexpr uint64_t REG_STAT_DECODE_ERR = 0x314;
    static constexpr uint64_t REG_STAT_TIMEOUT    = 0x318;
    static constexpr uint64_t REG_STAT_CTRL       = 0x31C;
    static constexpr uint64_t STAT_REGION_END     = 0x320;

    // ── Cache management registers (0x500 – 0x513) ──
    static constexpr uint64_t CACHE_BASE              = 0x500;
    static constexpr uint64_t REG_CACHE_CTRL          = 0x500;
    static constexpr uint64_t REG_ROUTE_CACHE_ENTRIES = 0x504;
    static constexpr uint64_t REG_DATA_CACHE_LINES    = 0x508;
    static constexpr uint64_t REG_DATA_CACHE_LINE_SZ  = 0x50C;
    static constexpr uint64_t REG_DATA_CACHE_STATUS   = 0x510;
    static constexpr uint64_t CACHE_REGION_END        = 0x514;

    // ── GLOBAL_CTRL bit fields (each bit enables/disables a feature) ──
    static constexpr uint32_t CTRL_ROUTER_EN      = 0x01;
    static constexpr uint32_t CTRL_ARB_EN         = 0x02;
    static constexpr uint32_t CTRL_ROUTE_CACHE_EN = 0x04;
    static constexpr uint32_t CTRL_DATA_CACHE_EN  = 0x08;
    static constexpr uint32_t CTRL_QOS_EN         = 0x10;
    static constexpr uint32_t CTRL_STATS_EN       = 0x20;
    static constexpr uint32_t CTRL_TIMEOUT_EN     = 0x40;
    static constexpr uint32_t CTRL_NB_MODE_EN     = 0x80;
    static constexpr uint32_t CTRL_WRITABLE_MASK  = 0xFF;

    // Status register bit definitions
    static constexpr uint32_t STATUS_TIMEOUT_BIT     = 0x01;
    static constexpr uint32_t STATUS_DECODE_ERR_BIT  = 0x02;
    static constexpr uint32_t STATUS_CACHE_EVICT_BIT = 0x04;
    static constexpr uint32_t STATUS_W1C_MASK        = 0x07;
    static constexpr uint32_t STATUS_INIT_ID_SHIFT   = 4;
    static constexpr uint32_t STATUS_INIT_ID_MASK    = 0x0F;

    // Field masks for WARL registers
    static constexpr uint32_t ARB_MODE_MASK        = 0x03;
    static constexpr uint32_t ARB_MODE_INVALID     = 3;
    static constexpr uint32_t TIMEOUT_CFG_MASK     = 0xFFFF;
    static constexpr uint32_t IRQ_EN_MASK          = 0x07;
    static constexpr uint32_t QOS_PRIORITY_MASK    = 0x07;
    static constexpr uint32_t ARB_WEIGHT_MASK      = 0xFF;
    static constexpr uint32_t ROUTE_CTRL_MASK      = 0x03;
    static constexpr uint32_t ROUTE_CTRL_ENABLE    = 0x01;
    static constexpr uint32_t ROUTE_CTRL_CACHEABLE = 0x02;

    // Register width
    static constexpr unsigned int REG_WIDTH_BYTES = 4;

    // Stat counter saturation
    static constexpr uint32_t STAT_COUNTER_MAX = 0xFFFFFFFF;

    // Cache control bits
    static constexpr uint32_t CACHE_CTRL_FLUSH_ROUTE = 0x01;
    static constexpr uint32_t CACHE_CTRL_FLUSH_DATA  = 0x02;

    // Stat control: write 1 to clear all
    static constexpr uint32_t STAT_CTRL_CLEAR = 1;

    // Firmware-readable version: major.minor packed as 16.16.
    static constexpr uint32_t VERSION_VALUE = 0x00010000;

    // ── Public TLM-2.0 sockets and interrupt output ──
    sc_core::sc_vector<tlm_utils::simple_target_socket_tagged<noc_router>>    init_socket;
    sc_core::sc_vector<tlm_utils::simple_initiator_socket_tagged<noc_router>> tgt_socket;
    tlm_utils::simple_target_socket<noc_router>                               cfg_socket;
    sc_core::sc_out<bool>                                                     irq; // active-high interrupt output

    SC_HAS_PROCESS(noc_router);

    // Construct a router with n_init initiator ports, n_tgt target ports,
    // and a base clock period 'tick' used for timeout monitoring.
    noc_router(sc_core::sc_module_name name, unsigned int n_init = 4, unsigned int n_tgt = 4,
               sc_core::sc_time tick = sc_core::sc_time(1, sc_core::SC_US))
        : sc_module(name), init_socket("init_socket", n_init), tgt_socket("tgt_socket", n_tgt),
          cfg_socket("cfg_socket"), irq("irq"), m_n_init(n_init), m_n_tgt(n_tgt), m_tick(tick),
          m_arbiter(n_init, n_tgt), m_route_cache(), m_data_cache(), m_global_ctrl(0x01), m_arb_mode(0),
          m_timeout_cfg(0xFFFF), m_status(0), m_irq_en(0), m_qos_priority(n_init, 0), m_arb_weight(n_init, 1),
          m_stat_total_txn(0), m_stat_route_hit(0), m_stat_route_miss(0), m_stat_data_hit(0), m_stat_data_miss(0),
          m_stat_decode_err(0), m_stat_timeout(0), m_nb_queues(n_tgt), m_nb_events(n_tgt), m_nb_inflight() {
        // Zero-initialise the route table (all entries disabled).
        for (unsigned int i = 0; i < MAX_ROUTES; ++i) {
            m_route_start[i] = 0;
            m_route_end[i]   = 0;
            m_route_tgt[i]   = 0;
            m_route_ctrl[i]  = 0;
        }

        // Register TLM-2.0 transport callbacks for config and data paths.
        cfg_socket.register_b_transport(this, &noc_router::cfg_b_transport);

        for (unsigned int i = 0; i < n_init; ++i) {
            init_socket[i].register_b_transport(this, &noc_router::data_b_transport, static_cast<int>(i));
            init_socket[i].register_nb_transport_fw(this, &noc_router::data_nb_transport_fw, static_cast<int>(i));
        }

        for (unsigned int i = 0; i < n_tgt; ++i) {
            tgt_socket[i].register_nb_transport_bw(this, &noc_router::data_nb_transport_bw, static_cast<int>(i));
        }

        // Spawn one SC_THREAD per target port for NB queue processing.
        for (unsigned int i = 0; i < n_tgt; ++i) {
            sc_core::sc_spawn(sc_bind(&noc_router::tgt_thread_func, this, i),
                              sc_core::sc_gen_unique_name("tgt_thread"));
        }

        // Global timeout monitor thread (checks all in-flight NB txns).
        SC_THREAD(timeout_thread);
    }

    // ── Configuration port: 32-bit register read/write via b_transport ──
    void cfg_b_transport(tlm::tlm_generic_payload &trans, sc_core::sc_time & /*delay*/) {
        unsigned char *ptr = trans.get_data_ptr();
        if (!ptr) {
            trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }
        if (trans.get_data_length() != REG_WIDTH_BYTES) {
            trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }

        uint64_t addr = trans.get_address();
        if (addr % REG_WIDTH_BYTES != 0 || addr >= CFG_ADDR_SPACE || is_reserved_gap(addr)) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        tlm::tlm_command cmd = trans.get_command();
        if (cmd != tlm::TLM_READ_COMMAND && cmd != tlm::TLM_WRITE_COMMAND) {
            trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }

        if (cmd == tlm::TLM_READ_COMMAND) {
            uint32_t val = cfg_read(addr);
            std::memcpy(ptr, &val, REG_WIDTH_BYTES);
        } else {
            uint32_t val;
            std::memcpy(&val, ptr, REG_WIDTH_BYTES);
            cfg_write(addr, val);
        }
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    // ── Data path: blocking transport through decoded target port ──
    void data_b_transport(int id, tlm::tlm_generic_payload &trans, sc_core::sc_time &delay) {
        unsigned char *ptr = trans.get_data_ptr();
        if (!ptr) {
            trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }
        // Reject transaction if the router is globally disabled.
        if (!(m_global_ctrl & CTRL_ROUTER_EN)) {
            trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }

        // Decode the target port from the transaction address.
        unsigned int tgt_port  = 0;
        bool         cacheable = false;
        if (!decode_address(trans.get_address(), static_cast<unsigned int>(id), tgt_port, cacheable)) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        // Run arbitration if enabled (single-initiator request vector).
        if (m_global_ctrl & CTRL_ARB_EN) {
            std::vector<bool> reqs(m_n_init, false);
            reqs[static_cast<unsigned int>(id)] = true;
            m_arbiter.arbitrate(tgt_port, reqs, m_qos_priority, m_arb_weight, static_cast<uint8_t>(m_arb_mode),
                                (m_global_ctrl & CTRL_QOS_EN) != 0);
        }

        tlm::tlm_command cmd = trans.get_command();

        // Attempt data-cache read hit before forwarding downstream.
        if ((m_global_ctrl & CTRL_DATA_CACHE_EN) && cacheable && cmd == tlm::TLM_READ_COMMAND) {
            if (m_data_cache.read(trans.get_address(), ptr, trans.get_data_length())) {
                inc_stat(m_stat_data_hit);
                inc_stat(m_stat_total_txn);
                trans.set_response_status(tlm::TLM_OK_RESPONSE);
                return;
            }
            inc_stat(m_stat_data_miss);
        }

        // Write-through: update cache line if it exists.
        if ((m_global_ctrl & CTRL_DATA_CACHE_EN) && cmd == tlm::TLM_WRITE_COMMAND) {
            m_data_cache.write(trans.get_address(), ptr, trans.get_data_length());
        }

        tgt_socket[tgt_port]->b_transport(trans, delay);

        if (trans.get_response_status() == tlm::TLM_OK_RESPONSE) {
            inc_stat(m_stat_total_txn);
            if ((m_global_ctrl & CTRL_DATA_CACHE_EN) && cacheable && cmd == tlm::TLM_READ_COMMAND) {
                bool evicted = m_data_cache.allocate(trans.get_address(), ptr, trans.get_data_length());
                if (evicted) {
                    m_status |= STATUS_CACHE_EVICT_BIT;
                    update_irq();
                }
            }
        }
    }

    // ── Data path: non-blocking forward transport (initiator → router) ──
    tlm::tlm_sync_enum data_nb_transport_fw(int id, tlm::tlm_generic_payload &trans, tlm::tlm_phase &phase,
                                            sc_core::sc_time &delay) {
        if (phase == tlm::END_RESP) {
            m_nb_inflight.erase(&trans);
            return tlm::TLM_COMPLETED;
        }

        if (phase != tlm::BEGIN_REQ) return tlm::TLM_ACCEPTED;

        if (!(m_global_ctrl & CTRL_NB_MODE_EN)) return tlm::TLM_ACCEPTED;

        unsigned char *ptr = trans.get_data_ptr();
        if (!ptr) {
            trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return tlm::TLM_COMPLETED;
        }
        if (!(m_global_ctrl & CTRL_ROUTER_EN)) {
            trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return tlm::TLM_COMPLETED;
        }

        unsigned int tgt_port  = 0;
        bool         cacheable = false;
        if (!decode_address(trans.get_address(), static_cast<unsigned int>(id), tgt_port, cacheable)) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return tlm::TLM_COMPLETED;
        }

        NbEntry entry;
        entry.trans      = &trans;
        entry.init_id    = static_cast<unsigned int>(id);
        entry.tgt_port   = tgt_port;
        entry.cacheable  = cacheable;
        entry.start_time = sc_core::sc_time_stamp() + delay;
        m_nb_queues[tgt_port].push_back(entry);
        m_nb_inflight[&trans] = entry;
        m_nb_events[tgt_port].notify(sc_core::SC_ZERO_TIME);

        return tlm::TLM_ACCEPTED;
    }

    // ── Data path: non-blocking backward transport (target → router → initiator) ──
    tlm::tlm_sync_enum data_nb_transport_bw(int /*id*/, tlm::tlm_generic_payload &trans, tlm::tlm_phase &phase,
                                            sc_core::sc_time &delay) {
        if (phase == tlm::END_REQ) return tlm::TLM_ACCEPTED;

        if (phase == tlm::BEGIN_RESP) {
            auto it = m_nb_inflight.find(&trans);
            if (it != m_nb_inflight.end()) {
                unsigned int init_id   = it->second.init_id;
                bool         cacheable = it->second.cacheable;

                if (trans.get_response_status() == tlm::TLM_OK_RESPONSE) {
                    inc_stat(m_stat_total_txn);
                    if ((m_global_ctrl & CTRL_DATA_CACHE_EN) && cacheable &&
                        trans.get_command() == tlm::TLM_READ_COMMAND) {
                        bool evicted =
                            m_data_cache.allocate(trans.get_address(), trans.get_data_ptr(), trans.get_data_length());
                        if (evicted) {
                            m_status |= STATUS_CACHE_EVICT_BIT;
                            update_irq();
                        }
                    }
                }

                tlm::tlm_phase   bw_phase = tlm::BEGIN_RESP;
                sc_core::sc_time bw_delay = delay;
                init_socket[init_id]->nb_transport_bw(trans, bw_phase, bw_delay);
                m_nb_inflight.erase(it);
            }
            return tlm::TLM_COMPLETED;
        }

        return tlm::TLM_ACCEPTED;
    }

  private:
    // ── Topology parameters ──
    unsigned int     m_n_init; // number of initiator ports
    unsigned int     m_n_tgt;  // number of target ports
    sc_core::sc_time m_tick;   // base clock period for timeout monitoring

    // ── Sub-modules: arbitration and caching ──
    noc_arbiter     m_arbiter;
    noc_route_cache m_route_cache;
    noc_data_cache  m_data_cache;

    // ── Configuration registers (directly memory-mapped) ──
    uint32_t m_global_ctrl; // feature enable bit-field (CTRL_* bits)
    uint32_t m_arb_mode;    // arbitration policy selector (0/1/2)
    uint32_t m_timeout_cfg; // NB timeout threshold in ticks (16-bit)
    uint32_t m_status;      // W1C status flags + initiator ID field
    uint32_t m_irq_en;      // per-source interrupt enable mask

    // ── Route table (16 entries, priority by descending index) ──
    uint32_t m_route_start[MAX_ROUTES];
    uint32_t m_route_end[MAX_ROUTES];
    uint32_t m_route_tgt[MAX_ROUTES];
    uint32_t m_route_ctrl[MAX_ROUTES];

    // ── Per-initiator QoS and weight arrays ──
    std::vector<uint8_t> m_qos_priority;
    std::vector<uint8_t> m_arb_weight;

    // ── Statistics counters (32-bit saturating, cleared via STAT_CTRL) ──
    uint32_t m_stat_total_txn;
    uint32_t m_stat_route_hit;
    uint32_t m_stat_route_miss;
    uint32_t m_stat_data_hit;
    uint32_t m_stat_data_miss;
    uint32_t m_stat_decode_err;
    uint32_t m_stat_timeout;

    // ── Non-blocking transport bookkeeping ──
    struct NbEntry {
        tlm::tlm_generic_payload *trans;
        unsigned int              init_id;
        unsigned int              tgt_port;
        bool                      cacheable;
        sc_core::sc_time          start_time; // for timeout detection
    };

    std::vector<std::deque<NbEntry>>              m_nb_queues;   // per-target pending queue
    std::vector<sc_core::sc_event>                m_nb_events;   // per-target wake event
    std::map<tlm::tlm_generic_payload *, NbEntry> m_nb_inflight; // active NB txns

    // Increment a saturating 32-bit statistics counter (only when stats enabled).
    void inc_stat(uint32_t &counter) {
        if ((m_global_ctrl & CTRL_STATS_EN) && counter < STAT_COUNTER_MAX) counter++;
    }

    // Recompute the IRQ output from IRQ_EN and STATUS register bits.
    void update_irq() {
        bool val = ((m_irq_en & STATUS_TIMEOUT_BIT) && (m_status & STATUS_TIMEOUT_BIT)) ||
                   ((m_irq_en & STATUS_DECODE_ERR_BIT) && (m_status & STATUS_DECODE_ERR_BIT)) ||
                   ((m_irq_en & STATUS_CACHE_EVICT_BIT) && (m_status & STATUS_CACHE_EVICT_BIT));
        irq.write(val);
    }

    // Return true if addr falls in an unimplemented gap in the config space.
    bool is_reserved_gap(uint64_t addr) const {
        if (addr >= REG_N_TGT_RO + REG_WIDTH_BYTES && addr < ROUTE_BASE) return true;
        if (addr >= ROUTE_REGION_END && addr < QOS_BASE) return true;
        if (addr >= QOS_BASE && addr < QOS_REGION_END) {
            uint64_t off = addr - QOS_BASE;
            if (off / REG_WIDTH_BYTES >= m_n_init) return true;
            return false;
        }
        if (addr >= QOS_REGION_END && addr < WEIGHT_BASE) return true;
        if (addr >= WEIGHT_BASE && addr < WEIGHT_REGION_END) {
            uint64_t off = addr - WEIGHT_BASE;
            if (off / REG_WIDTH_BYTES >= m_n_init) return true;
            return false;
        }
        if (addr >= WEIGHT_REGION_END && addr < STAT_BASE) return true;
        if (addr >= STAT_REGION_END && addr < CACHE_BASE) return true;
        if (addr >= CACHE_REGION_END && addr < CFG_ADDR_SPACE) return true;
        return false;
    }

    // Register read: decode config address and return the 32-bit register value.
    uint32_t cfg_read(uint64_t addr) {
        if (addr == REG_GLOBAL_CTRL) return m_global_ctrl;
        if (addr == REG_ARB_MODE) return m_arb_mode;
        if (addr == REG_TIMEOUT_CFG) return m_timeout_cfg;
        if (addr == REG_STATUS) return m_status;
        if (addr == REG_IRQ_EN) return m_irq_en;
        if (addr == REG_VERSION) return VERSION_VALUE;
        if (addr == REG_N_INIT_RO) return m_n_init;
        if (addr == REG_N_TGT_RO) return m_n_tgt;

        // Route table: 16 entries × 4 fields (start, end, tgt, ctrl).
        if (addr >= ROUTE_BASE && addr < ROUTE_REGION_END) {
            unsigned int idx = static_cast<unsigned int>((addr - ROUTE_BASE) / ROUTE_ENTRY_SIZE);
            unsigned int off = static_cast<unsigned int>((addr - ROUTE_BASE) % ROUTE_ENTRY_SIZE);
            if (idx < MAX_ROUTES) {
                switch (off) {
                case ROUTE_OFF_START:
                    return m_route_start[idx];
                case ROUTE_OFF_END:
                    return m_route_end[idx];
                case ROUTE_OFF_TGT:
                    return m_route_tgt[idx];
                case ROUTE_OFF_CTRL:
                    return m_route_ctrl[idx];
                }
            }
        }

        // Per-initiator QoS priority registers (3-bit, one per initiator).
        if (addr >= QOS_BASE && addr < QOS_REGION_END) {
            unsigned int i = static_cast<unsigned int>((addr - QOS_BASE) / REG_WIDTH_BYTES);
            if (i < m_n_init) return m_qos_priority[i];
        }

        // Per-initiator arbitration weight registers (8-bit, one per initiator).
        if (addr >= WEIGHT_BASE && addr < WEIGHT_REGION_END) {
            unsigned int i = static_cast<unsigned int>((addr - WEIGHT_BASE) / REG_WIDTH_BYTES);
            if (i < m_n_init) return m_arb_weight[i];
        }

        // Statistics counters (read-only, 32-bit saturating).
        if (addr == REG_STAT_TOTAL_TXN) return m_stat_total_txn;
        if (addr == REG_STAT_ROUTE_HIT) return m_stat_route_hit;
        if (addr == REG_STAT_ROUTE_MISS) return m_stat_route_miss;
        if (addr == REG_STAT_DATA_HIT) return m_stat_data_hit;
        if (addr == REG_STAT_DATA_MISS) return m_stat_data_miss;
        if (addr == REG_STAT_DECODE_ERR) return m_stat_decode_err;
        if (addr == REG_STAT_TIMEOUT) return m_stat_timeout;
        // STAT_CTRL is write-only (reads as 0).
        if (addr == REG_STAT_CTRL) return 0;

        // Cache management registers (CACHE_CTRL is write-only).
        if (addr == REG_CACHE_CTRL) return 0;
        if (addr == REG_ROUTE_CACHE_ENTRIES) return noc_route_cache::DEFAULT_CAPACITY;
        if (addr == REG_DATA_CACHE_LINES) return noc_data_cache::DEFAULT_NUM_LINES;
        if (addr == REG_DATA_CACHE_LINE_SZ) return noc_data_cache::DEFAULT_LINE_SIZE;
        if (addr == REG_DATA_CACHE_STATUS) return m_data_cache.valid_count();

        return 0;
    }

    // Register write: decode config address and apply WARL/W1C/RO semantics.
    void cfg_write(uint64_t addr, uint32_t val) {
        // GLOBAL_CTRL: all 8 bits writable; disabling a cache flushes it.
        if (addr == REG_GLOBAL_CTRL) {
            uint32_t old  = m_global_ctrl;
            m_global_ctrl = val & CTRL_WRITABLE_MASK;
            if ((old & CTRL_ROUTE_CACHE_EN) && !(m_global_ctrl & CTRL_ROUTE_CACHE_EN)) m_route_cache.flush();
            if ((old & CTRL_DATA_CACHE_EN) && !(m_global_ctrl & CTRL_DATA_CACHE_EN)) m_data_cache.flush();
            update_irq();
            return;
        }
        // ARB_MODE: WARL 2-bit field; value 3 maps to 0 (round-robin).
        if (addr == REG_ARB_MODE) {
            uint32_t v = val & ARB_MODE_MASK;
            m_arb_mode = (v == ARB_MODE_INVALID) ? 0 : v;
            return;
        }
        // TIMEOUT_CFG: 16-bit WARL threshold (upper bits ignored).
        if (addr == REG_TIMEOUT_CFG) {
            m_timeout_cfg = val & TIMEOUT_CFG_MASK;
            return;
        }
        // STATUS: W1C — writing 1 to a bit clears it.
        if (addr == REG_STATUS) {
            m_status = m_status & ~(val & STATUS_W1C_MASK);
            update_irq();
            return;
        }
        // IRQ_EN: 3-bit mask controlling which STATUS bits trigger irq.
        if (addr == REG_IRQ_EN) {
            m_irq_en = val & IRQ_EN_MASK;
            update_irq();
            return;
        }
        // VERSION, N_INIT, N_TGT are read-only; writes are silently dropped.
        if (addr == REG_VERSION || addr == REG_N_INIT_RO || addr == REG_N_TGT_RO) return;

        // Route table write: ROUTE_TGT is WARL-clamped to m_n_tgt-1.
        if (addr >= ROUTE_BASE && addr < ROUTE_REGION_END) {
            unsigned int idx = static_cast<unsigned int>((addr - ROUTE_BASE) / ROUTE_ENTRY_SIZE);
            unsigned int off = static_cast<unsigned int>((addr - ROUTE_BASE) % ROUTE_ENTRY_SIZE);
            if (idx < MAX_ROUTES) {
                switch (off) {
                case ROUTE_OFF_START:
                    m_route_start[idx] = val;
                    break;
                case ROUTE_OFF_END:
                    m_route_end[idx] = val;
                    break;
                case ROUTE_OFF_TGT:
                    m_route_tgt[idx] = (val >= m_n_tgt) ? (m_n_tgt - 1) : val;
                    break;
                case ROUTE_OFF_CTRL:
                    m_route_ctrl[idx] = val & ROUTE_CTRL_MASK;
                    break;
                }
                // Any route table modification invalidates the route cache.
                m_route_cache.flush();
            }
            return;
        }

        // QoS priority: 3-bit WARL per initiator.
        if (addr >= QOS_BASE && addr < QOS_REGION_END) {
            unsigned int i = static_cast<unsigned int>((addr - QOS_BASE) / REG_WIDTH_BYTES);
            if (i < m_n_init) m_qos_priority[i] = static_cast<uint8_t>(val & QOS_PRIORITY_MASK);
            return;
        }

        // Arbitration weight: 8-bit WARL; zero clamps to 1.
        if (addr >= WEIGHT_BASE && addr < WEIGHT_REGION_END) {
            unsigned int i = static_cast<unsigned int>((addr - WEIGHT_BASE) / REG_WIDTH_BYTES);
            if (i < m_n_init) {
                uint8_t w       = static_cast<uint8_t>(val & ARB_WEIGHT_MASK);
                m_arb_weight[i] = (w == 0) ? 1 : w;
            }
            return;
        }

        // Stats region: only STAT_CTRL is writable (write 1 → clear all).
        if (addr >= STAT_BASE && addr < STAT_REGION_END) {
            if (addr == REG_STAT_CTRL && val == STAT_CTRL_CLEAR) {
                m_stat_total_txn  = 0;
                m_stat_route_hit  = 0;
                m_stat_route_miss = 0;
                m_stat_data_hit   = 0;
                m_stat_data_miss  = 0;
                m_stat_decode_err = 0;
                m_stat_timeout    = 0;
            }
            return;
        }

        if (addr == REG_CACHE_CTRL) {
            if (val & CACHE_CTRL_FLUSH_ROUTE) m_route_cache.flush();
            if (val & CACHE_CTRL_FLUSH_DATA) m_data_cache.flush();
            return;
        }
    }

    // Decode a data-path address through the route table (highest-index match wins).
    // Populates tgt_port and cacheable; returns false on decode error.
    bool decode_address(uint64_t txn_addr, unsigned int init_id, unsigned int &tgt_port, bool &cacheable) {
        if (m_global_ctrl & CTRL_ROUTE_CACHE_EN) {
            if (m_route_cache.lookup(txn_addr, tgt_port, cacheable)) {
                inc_stat(m_stat_route_hit);
                return true;
            }
            inc_stat(m_stat_route_miss);
        } else {
            inc_stat(m_stat_route_miss);
        }

        for (int i = static_cast<int>(MAX_ROUTES) - 1; i >= 0; --i) {
            if (m_route_ctrl[i] & ROUTE_CTRL_ENABLE) {
                if (txn_addr >= m_route_start[i] && txn_addr < m_route_end[i]) {
                    tgt_port  = m_route_tgt[i];
                    cacheable = (m_route_ctrl[i] & ROUTE_CTRL_CACHEABLE) != 0;
                    if (m_global_ctrl & CTRL_ROUTE_CACHE_EN) {
                        m_route_cache.update(m_route_start[i], m_route_end[i], tgt_port, cacheable);
                    }
                    return true;
                }
            }
        }

        m_status = (m_status & ~(STATUS_INIT_ID_MASK << STATUS_INIT_ID_SHIFT)) |
                   (static_cast<uint32_t>(init_id & STATUS_INIT_ID_MASK) << STATUS_INIT_ID_SHIFT);
        m_status |= STATUS_DECODE_ERR_BIT;
        inc_stat(m_stat_decode_err);
        update_irq();
        return false;
    }

    // Per-target SC_THREAD: drains the NB queue, applies arbitration, forwards
    // transactions to the downstream target via nb_transport_fw.
    void tgt_thread_func(unsigned int tgt_id) {
        while (true) {
            wait(m_nb_events[tgt_id]);
            while (!m_nb_queues[tgt_id].empty()) {
                NbEntry entry = m_nb_queues[tgt_id].front();
                m_nb_queues[tgt_id].pop_front();

                if (m_global_ctrl & CTRL_ARB_EN) {
                    std::vector<bool> reqs(m_n_init, false);
                    reqs[entry.init_id] = true;
                    for (const auto &other : m_nb_queues[tgt_id])
                        reqs[other.init_id] = true;

                    unsigned int winner =
                        m_arbiter.arbitrate(tgt_id, reqs, m_qos_priority, m_arb_weight,
                                            static_cast<uint8_t>(m_arb_mode), (m_global_ctrl & CTRL_QOS_EN) != 0);

                    if (winner != entry.init_id) {
                        m_nb_queues[tgt_id].push_back(entry);
                        bool found = false;
                        for (auto it = m_nb_queues[tgt_id].begin(); it != m_nb_queues[tgt_id].end(); ++it) {
                            if (it->init_id == winner) {
                                entry = *it;
                                m_nb_queues[tgt_id].erase(it);
                                found = true;
                                break;
                            }
                        }
                        if (!found) continue;
                    }
                }

                tlm::tlm_generic_payload &trans = *entry.trans;
                tlm::tlm_command          cmd   = trans.get_command();

                if ((m_global_ctrl & CTRL_DATA_CACHE_EN) && entry.cacheable && cmd == tlm::TLM_READ_COMMAND) {
                    if (m_data_cache.read(trans.get_address(), trans.get_data_ptr(), trans.get_data_length())) {
                        inc_stat(m_stat_data_hit);
                        inc_stat(m_stat_total_txn);
                        trans.set_response_status(tlm::TLM_OK_RESPONSE);
                        tlm::tlm_phase   resp_phase = tlm::BEGIN_RESP;
                        sc_core::sc_time resp_delay = sc_core::SC_ZERO_TIME;
                        init_socket[entry.init_id]->nb_transport_bw(trans, resp_phase, resp_delay);
                        m_nb_inflight.erase(&trans);
                        continue;
                    }
                    inc_stat(m_stat_data_miss);
                }

                if ((m_global_ctrl & CTRL_DATA_CACHE_EN) && cmd == tlm::TLM_WRITE_COMMAND) {
                    m_data_cache.write(trans.get_address(), trans.get_data_ptr(), trans.get_data_length());
                }

                tlm::tlm_phase   fw_phase = tlm::BEGIN_REQ;
                sc_core::sc_time fw_delay = sc_core::SC_ZERO_TIME;
                tgt_socket[tgt_id]->nb_transport_fw(trans, fw_phase, fw_delay);
            }
        }
    }

    // Periodic SC_THREAD: checks in-flight NB transactions for timeout.
    void timeout_thread() {
        while (true) {
            wait(m_tick);
            if (!(m_global_ctrl & CTRL_TIMEOUT_EN)) continue;

            sc_core::sc_time now       = sc_core::sc_time_stamp();
            sc_core::sc_time threshold = m_tick * m_timeout_cfg;

            for (auto &kv : m_nb_inflight) {
                if ((now - kv.second.start_time) > threshold) {
                    m_status |= STATUS_TIMEOUT_BIT;
                    inc_stat(m_stat_timeout);
                    update_irq();
                    break;
                }
            }
        }
    }
};

#endif
