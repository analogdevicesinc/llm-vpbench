// SPDX-License-Identifier: Apache-2.0
#ifndef ACLINT_SSWI_H
#define ACLINT_SSWI_H

// ============================================================================
// LLM-VPBench Interface Contract — RISC-V ACLINT SSWI
// DO NOT MODIFY this interface. Implement all TODO sections.
// ============================================================================

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>

SC_MODULE(aclint_sswi) {
    tlm_utils::simple_target_socket<aclint_sswi> socket{"socket"};
    sc_core::sc_vector<sc_core::sc_out<bool>> ssw_irq;
    explicit aclint_sswi(sc_core::sc_module_name name, unsigned int num_harts = 8)
        : sc_module(name), ssw_irq("ssw_irq", num_harts), m_num_harts(num_harts)
    {
        socket.register_b_transport(this, &aclint_sswi::b_transport);
    }

private:
    unsigned int m_num_harts;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        // TODO: Implement SETSSIP register read/write
        // Address: setssip[i] at offset 4*i (i = 0..num_harts-1)
        // 32-bit WARL: only bit[0] writable
        // Benchmark deviation: level-sensitive (not edge-triggered per official spec)
        // Write 1 → ssw_irq[i] = true; Write 0 → ssw_irq[i] = false
        // Reads return current register value
    }
};

#endif
