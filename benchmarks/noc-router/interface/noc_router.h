// SPDX-License-Identifier: Apache-2.0
#ifndef NOC_ROUTER_H
#define NOC_ROUTER_H

// ============================================================================
// LLM-VPBench Interface Contract — NoC Router (4x4 TLM-2.0 Crossbar)
// DO NOT MODIFY this interface. Implement all TODO sections.
// ============================================================================

#include <cassert>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>

SC_MODULE(noc_router) {
    // === MANDATORY INTERFACE (do not rename) ===

    // Data-path: 4 upstream initiator ports (tagged by index)
    tlm_utils::simple_target_socket_tagged<noc_router>* init_socket[4];

    // Data-path: 4 downstream target ports (tagged by index)
    tlm_utils::simple_initiator_socket_tagged<noc_router>* tgt_socket[4];

    // Configuration: single untagged target socket for register access
    tlm_utils::simple_target_socket<noc_router> cfg_socket{"cfg_socket"};

    // Interrupt output (active-high, level-sensitive)
    sc_core::sc_out<bool> irq{"irq"};

    SC_HAS_PROCESS(noc_router);
    explicit noc_router(sc_core::sc_module_name name,
                        unsigned n_init = 4,
                        unsigned n_tgt = 4,
                        sc_core::sc_time tick = sc_core::sc_time(1, sc_core::SC_US))
        : sc_module(name), m_n_init(n_init), m_n_tgt(n_tgt), m_tick(tick)
    {
        assert(n_init <= 4 && "n_init must be <= 4");
        assert(n_tgt <= 4 && "n_tgt must be <= 4");
        for (unsigned i = 0; i < n_init; ++i) {
            char nm[32];
            std::snprintf(nm, sizeof(nm), "init_socket_%u", i);
            init_socket[i] = new tlm_utils::simple_target_socket_tagged<noc_router>(nm);
            init_socket[i]->register_b_transport(this, &noc_router::b_transport, i);
        }
        for (unsigned i = 0; i < n_tgt; ++i) {
            char nm[32];
            std::snprintf(nm, sizeof(nm), "tgt_socket_%u", i);
            tgt_socket[i] = new tlm_utils::simple_initiator_socket_tagged<noc_router>(nm);
        }
        cfg_socket.register_b_transport(this, &noc_router::cfg_b_transport);
        // TODO: Additional initialization (caches, arbitration state, registers)
    }

    ~noc_router() {
        for (unsigned i = 0; i < m_n_init; ++i) delete init_socket[i];
        for (unsigned i = 0; i < m_n_tgt; ++i) delete tgt_socket[i];
    }

    // === IMPLEMENT BELOW ===
private:
    unsigned m_n_init;
    unsigned m_n_tgt;
    sc_core::sc_time m_tick;

    void b_transport(int id, tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        // TODO: Implement data-path routing
        // 1. Address decode (descending rule priority, first match)
        // 2. Arbitration (round-robin or fixed-priority per GLOBAL_CTRL config)
        // 3. Data cache lookup (direct-mapped, byte-valid tracking per line)
        // 4. Forward to tgt_socket[target_id]->b_transport(trans, delay)
        // 5. Update statistics counters
    }

    void cfg_b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        // TODO: Implement register read/write (4-byte transactions only)
        // Register map: GLOBAL_CTRL, ROUTE_START/END/CTRL, ARB config, STATS, STATUS
    }
};

#endif // NOC_ROUTER_H
