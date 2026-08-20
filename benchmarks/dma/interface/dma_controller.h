#ifndef DMA_CONTROLLER_H
#define DMA_CONTROLLER_H

// ============================================================================
// LLM-VPBench Interface Contract — Multi-Channel DMA Controller
// DO NOT MODIFY this interface. Implement all TODO sections.
// ============================================================================

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>

SC_MODULE(dma_controller) {
    // === MANDATORY INTERFACE (do not rename) ===
    tlm_utils::simple_target_socket<dma_controller> target_socket{"target_socket"};
    tlm_utils::simple_initiator_socket<dma_controller> initiator_socket{"initiator_socket"};
    sc_core::sc_vector<sc_core::sc_out<bool>> irq{"irq"};
    sc_core::sc_out<bool> irq_global{"irq_global"};

    static constexpr unsigned NUM_CHANNELS = 4;

    SC_HAS_PROCESS(dma_controller);
    explicit dma_controller(sc_core::sc_module_name name)
        : sc_module(name), irq("irq", NUM_CHANNELS)
    {
        target_socket.register_b_transport(this, &dma_controller::reg_b_transport);
        for (unsigned i = 0; i < NUM_CHANNELS; ++i) {
            sc_core::sc_spawn(sc_bind(&dma_controller::channel_thread, this, i));
        }
        // TODO: Additional initialization
    }

    // === IMPLEMENT BELOW ===
private:
    void reg_b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        // TODO: Register access (zero delay)
        // Global regs: STATUS (0x000), INT_STATUS (0x004), INT_CLEAR (0x008),
        //              GLOBAL_CTRL (0x00C), VERSION (0x010) = 0x444D4102, INT_MASK (0x014)
        // Per-channel: base = 0x100 + ch*0x40
        //   CH_SRC_ADDR(+0x00), CH_DST_ADDR(+0x04), CH_XFER_SIZE(+0x08),
        //   CH_CTRL(+0x0C), CH_CONFIG(+0x10), CH_STATUS(+0x14),
        //   CH_CURR_SRC(+0x18), CH_CURR_DST(+0x1C), CH_REMAINING(+0x20),
        //   CH_WRAP_COUNT(+0x24), CH_LINK(+0x28)
    }

    void channel_thread(unsigned ch) {
        // TODO: Per-channel transfer engine
        // 6-state FSM: IDLE → CONFIGURED → RUNNING → COMPLETE (or SUSPENDED, ERROR)
        // Bus-master transfers via initiator_socket.b_transport()
        // Priority arbitration with starvation prevention (threshold = 8 bursts)
        // Supports: circular mode, linked mode, SUSPENDED live-reprogramming
    }
};

#endif // DMA_CONTROLLER_H
