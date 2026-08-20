// SPDX-License-Identifier: Apache-2.0
#ifndef AXI4_INTERCONNECT_H
#define AXI4_INTERCONNECT_H

// ============================================================================
// LLM-VPBench Interface Contract — AMBA AXI4 Interconnect
// DO NOT MODIFY this interface. Implement all TODO sections.
// ============================================================================

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>

SC_MODULE(axi4_interconnect) {
    sc_core::sc_vector<tlm_utils::simple_target_socket_tagged<axi4_interconnect>> master_socket;
    sc_core::sc_vector<tlm_utils::simple_initiator_socket_tagged<axi4_interconnect>> slave_socket;

    SC_HAS_PROCESS(axi4_interconnect);
    explicit axi4_interconnect(sc_core::sc_module_name name,
                               unsigned int num_masters = 2,
                               unsigned int num_slaves = 2)
        : sc_module(name),
          master_socket("master_socket", num_masters),
          slave_socket("slave_socket", num_slaves),
          m_num_masters(num_masters),
          m_num_slaves(num_slaves)
    {
        for (unsigned i = 0; i < num_masters; ++i) {
            master_socket[i].register_b_transport(this, &axi4_interconnect::b_transport, i);
            master_socket[i].register_get_direct_mem_ptr(this, &axi4_interconnect::get_direct_mem_ptr, i);
            master_socket[i].register_transport_dbg(this, &axi4_interconnect::transport_dbg, i);
        }
        for (unsigned i = 0; i < num_slaves; ++i) {
            slave_socket[i].register_invalidate_direct_mem_ptr(this, &axi4_interconnect::invalidate_direct_mem_ptr, i);
        }
    }

private:
    unsigned int m_num_masters;
    unsigned int m_num_slaves;

    void b_transport(int id, tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        // TODO: Canonical routing pattern:
        // 1. Save original address
        // 2. Decode target from address map (configurable regions)
        // 3. Translate address (subtract region base)
        // 4. Acquire per-slave mutex (round-robin fairness)
        // 5. Forward via slave_socket[target]->b_transport(trans, delay)
        // 6. Restore original address (CRITICAL — even on error)
        // Unmatched address → TLM_ADDRESS_ERROR_RESPONSE without forwarding
    }

    bool get_direct_mem_ptr(int id, tlm::tlm_generic_payload& trans, tlm::tlm_dmi& dmi_data) {
        // TODO: DMI forwarding — same address decode/translate, NO mutex
    }

    unsigned int transport_dbg(int id, tlm::tlm_generic_payload& trans) {
        // TODO: Debug transport forwarding — no mutex, no timing
    }

    void invalidate_direct_mem_ptr(int id, sc_dt::uint64 start, sc_dt::uint64 end) {
        // TODO: Translate addresses back and broadcast to all masters
    }
};

#endif
