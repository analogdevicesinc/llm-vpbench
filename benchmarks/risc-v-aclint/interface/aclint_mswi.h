// SPDX-License-Identifier: Apache-2.0
#ifndef ACLINT_MSWI_H
#define ACLINT_MSWI_H

// ============================================================================
// LLM-VPBench Interface Contract — RISC-V ACLINT MSWI
// DO NOT MODIFY this interface. Implement all TODO sections.
// ============================================================================

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>

SC_MODULE(aclint_mswi) {
    tlm_utils::simple_target_socket<aclint_mswi> socket{"socket"};
    sc_core::sc_vector<sc_core::sc_out<bool>> sw_irq;
    explicit aclint_mswi(sc_core::sc_module_name name, unsigned int num_harts = 8)
        : sc_module(name), sw_irq("sw_irq", num_harts), m_num_harts(num_harts)
    {
        socket.register_b_transport(this, &aclint_mswi::b_transport);
    }

private:
    unsigned int m_num_harts;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        // TODO: Implement MSIP register read/write
        // Address: msip[i] at offset 4*i (i = 0..num_harts-1)
        // 32-bit WARL: only bit[0] writable
        // Write 1 to msip[i] → sw_irq[i] = true; Write 0 → sw_irq[i] = false
        // Out-of-range or misaligned → TLM_ADDRESS_ERROR_RESPONSE
    }
};

#endif
