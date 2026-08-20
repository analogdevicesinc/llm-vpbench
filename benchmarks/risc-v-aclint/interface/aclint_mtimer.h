// SPDX-License-Identifier: Apache-2.0
#ifndef ACLINT_MTIMER_H
#define ACLINT_MTIMER_H

// ============================================================================
// LLM-VPBench Interface Contract — RISC-V ACLINT MTIMER
// DO NOT MODIFY this interface. Implement all TODO sections.
// ============================================================================

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>

SC_MODULE(aclint_mtimer) {
    tlm_utils::simple_target_socket<aclint_mtimer> socket{"socket"};
    sc_core::sc_vector<sc_core::sc_out<bool>> timer_irq;

    SC_HAS_PROCESS(aclint_mtimer);
    explicit aclint_mtimer(sc_core::sc_module_name name,
                           sc_core::sc_time tick = sc_core::sc_time(1, sc_core::SC_US),
                           unsigned int num_harts = 8)
        : sc_module(name), timer_irq("timer_irq", num_harts),
          m_tick(tick), m_num_harts(num_harts)
    {
        socket.register_b_transport(this, &aclint_mtimer::b_transport);
        SC_THREAD(timer_thread);
    }

private:
    sc_core::sc_time m_tick;
    unsigned int m_num_harts;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        // TODO: Implement register read/write
        // mtimecmp[i] at offset 8*i (64-bit), mtime at 0x7FF8 (64-bit)
        // Benchmark extensions: timer_enable at 0x7FE8, prescaler at 0x7FF0, overflow_flag at 0x7FE0
    }

    void timer_thread() {
        // TODO: Increment mtime every (prescaler * tick) when timer_enable is set
        // Assert timer_irq[i] when mtime >= mtimecmp[i]
    }
};

#endif
