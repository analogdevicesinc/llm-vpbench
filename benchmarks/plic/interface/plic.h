// SPDX-License-Identifier: Apache-2.0
#ifndef PLIC_H
#define PLIC_H

// ============================================================================
// LLM-VPBench Interface Contract — RISC-V PLIC
// DO NOT MODIFY this interface. Implement all TODO sections.
// ============================================================================

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>

SC_MODULE(plic) {
    tlm_utils::simple_target_socket<plic> socket{"socket"};
    sc_core::sc_vector<sc_core::sc_in<bool>> src_irq{"src_irq"};
    sc_core::sc_out<bool> irq_out{"irq_out"};

    SC_HAS_PROCESS(plic);
    explicit plic(sc_core::sc_module_name name, unsigned int num_sources = 8)
        : sc_module(name), src_irq("src_irq", num_sources), m_num_sources(num_sources)
    {
        socket.register_b_transport(this, &plic::b_transport);
        SC_METHOD(irq_eval);
        for (unsigned i = 0; i < num_sources; ++i)
            sensitive << src_irq[i];
    }

private:
    unsigned int m_num_sources;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        // TODO: Memory-mapped register access (official PLIC memory map)
        //   0x000000: priority[1..S] (4 bytes each, WARL 0..7)
        //   0x001000: pending bits (read-only, 1 bit per source, packed 32-bit)
        //   0x002000: enable bits for context 0 (1 bit per source, packed 32-bit)
        //   0x200000: priority threshold for context 0 (WARL 0..7)
        //   0x200004: claim (read) / complete (write) for context 0
        // Benchmark extension:
        //   0x003000: INT_TYPE (1 bit per source: 0=level, 1=edge)
        // Errors: TLM_ADDRESS_ERROR_RESPONSE (out-of-range)
        //         TLM_GENERIC_ERROR_RESPONSE (wrong size/alignment)
    }

    void irq_eval() {
        // TODO: Re-evaluate irq_out
        // Qualify: source pending AND enabled AND priority > threshold (strictly greater)
        // Winner: highest priority among qualified; lowest source ID breaks ties
        // irq_out = true if any qualified source exists; false otherwise
    }
};

#endif
