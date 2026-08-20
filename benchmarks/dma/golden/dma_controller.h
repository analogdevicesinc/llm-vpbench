#ifndef DMA_CONTROLLER_H
#define DMA_CONTROLLER_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cstring>
#include <vector>
#include <algorithm>

SC_MODULE(dma_controller) {
    tlm_utils::simple_target_socket<dma_controller> target_socket;
    tlm_utils::simple_initiator_socket<dma_controller> initiator_socket;
    sc_core::sc_vector<sc_core::sc_out<bool>> irq;
    sc_core::sc_out<bool> irq_global;

    enum State { IDLE, CONFIGURED, RUNNING, SUSPENDED, COMPLETE, ERROR_ST };

    struct Channel {
        State state;
        uint32_t src_addr, dst_addr, xfer_size;
        uint32_t ctrl; // persistent bits: CIRCULAR(1), WIDTH(7:6), BURST(9:8), PRIORITY(11:10)
        uint32_t config;
        uint32_t curr_src, curr_dst, remaining;
        uint32_t wrap_count;
        uint32_t link;
        bool half_fired;
        bool pause_pending;
        bool abort_pending;
    };

    unsigned int m_num_channels;
    std::vector<Channel> m_channels;
    uint32_t m_global_ctrl;
    uint32_t m_int_status;
    uint32_t m_int_mask;
    sc_core::sc_event m_start_event;
    sc_core::sc_event m_reset_event;
    sc_core::sc_event m_irq_update_event;
    std::vector<bool> m_irq_state;
    bool m_irq_global_state;
    bool m_reset_flag;

    // Starvation prevention
    int m_last_priority;
    int m_consec_same_priority;
    int m_rr_pointer; // global round-robin pointer

    SC_HAS_PROCESS(dma_controller);

    dma_controller(sc_core::sc_module_name name, unsigned int num_channels = 4)
        : sc_module(name), target_socket("target_socket"), initiator_socket("initiator_socket"),
          irq("irq", num_channels), irq_global("irq_global"),
          m_num_channels(num_channels), m_channels(num_channels),
          m_global_ctrl(0), m_int_status(0), m_int_mask(0x00000FFF),
          m_irq_state(num_channels, false), m_irq_global_state(false),
          m_reset_flag(false), m_last_priority(-1), m_consec_same_priority(0), m_rr_pointer(0)
    {
        target_socket.register_b_transport(this, &dma_controller::b_transport);
        for (unsigned i = 0; i < m_num_channels; i++) reset_channel(i);
        SC_THREAD(transfer_thread);
        SC_METHOD(irq_method);
        sensitive << m_irq_update_event;
        dont_initialize();
    }

    void irq_method() {
        for (unsigned n = 0; n < m_num_channels; n++)
            irq[n].write(m_irq_state[n]);
        irq_global.write(m_irq_global_state);
    }



    void reset_channel(unsigned i) {
        Channel& ch = m_channels[i];
        ch.state = IDLE;
        ch.src_addr = ch.dst_addr = ch.xfer_size = 0;
        ch.ctrl = 0;
        ch.config = 0;
        ch.curr_src = ch.curr_dst = ch.remaining = 0;
        ch.wrap_count = 0;
        ch.link = 0x00000000;
        ch.half_fired = false;
        ch.pause_pending = false;
        ch.abort_pending = false;
    }

    void do_global_reset() {
        m_global_ctrl = 0;
        m_int_status = 0;
        m_int_mask = 0x00000FFF;
        for (unsigned i = 0; i < m_num_channels; i++) reset_channel(i);
        m_last_priority = -1;
        m_consec_same_priority = 0;
        m_rr_pointer = 0;
        m_reset_flag = true;
        update_irqs();
        m_reset_event.notify(sc_core::SC_ZERO_TIME);
    }

    void update_irqs() {
        bool global = false;
        for (unsigned n = 0; n < m_num_channels; n++) {
            bool ch_irq = false;
            for (int k = 0; k < 3; k++) {
                int bit = n * 3 + k;
                if ((m_int_status & (1u << bit)) && !(m_int_mask & (1u << bit)))
                    ch_irq = true;
            }
            m_irq_state[n] = ch_irq;
            if (ch_irq) global = true;
        }
        m_irq_global_state = global;
        m_irq_update_event.notify();
    }

    uint32_t get_status_reg() {
        uint32_t v = 0;
        for (unsigned i = 0; i < m_num_channels; i++)
            if (m_channels[i].state == RUNNING || m_channels[i].state == SUSPENDED)
                v |= (1u << i);
        return v;
    }

    uint32_t get_ch_status(unsigned i) {
        Channel& ch = m_channels[i];
        uint32_t v = 0;
        if (ch.state == RUNNING || ch.state == SUSPENDED) v |= 1; // BUSY
        if (ch.state == ERROR_ST) v |= 2; // ERROR
        if (ch.state == COMPLETE) v |= 4; // COMPLETE
        // STATE encoding
        switch (ch.state) {
            case IDLE: break;
            case CONFIGURED: v |= (1u << 3); break;
            case RUNNING: v |= (2u << 3); break;
            case SUSPENDED: v |= (3u << 3); break;
            case COMPLETE: break; // IDLE + COMPLETE bit
            case ERROR_ST: break; // IDLE + ERROR bit
        }
        return v;
    }

    unsigned get_width(unsigned i) {
        unsigned w = (m_channels[i].ctrl >> 6) & 3;
        return (w == 0) ? 1 : (w == 1) ? 2 : 4;
    }

    unsigned get_burst(unsigned i) {
        unsigned b = (m_channels[i].ctrl >> 8) & 3;
        static const unsigned tbl[] = {1, 4, 8, 16};
        return tbl[b];
    }

    unsigned get_priority(unsigned i) { return (m_channels[i].ctrl >> 10) & 3; }

    void start_channel(unsigned i, bool from_link = false) {
        Channel& ch = m_channels[i];
        // If RUNNING/SUSPENDED, ignore
        if (ch.state == RUNNING || ch.state == SUSPENDED) return;
        // If COMPLETE, implicit ack to IDLE then CONFIGURED
        if (ch.state == COMPLETE) ch.state = CONFIGURED;
        if (ch.state == IDLE) ch.state = CONFIGURED;
        // Check global enable (link bypasses)
        if (!from_link && !(m_global_ctrl & 1)) return;
        // Validation
        unsigned w_field = (ch.ctrl >> 6) & 3;
        if (w_field == 3) { ch.state = ERROR_ST; return; }
        unsigned width = (w_field == 0) ? 1 : (w_field == 1) ? 2 : 4;
        if (ch.xfer_size == 0) { ch.state = ERROR_ST; return; }
        if (ch.xfer_size > 65536) { ch.state = ERROR_ST; return; }
        if (ch.xfer_size % width != 0) { ch.state = ERROR_ST; return; }
        if (ch.src_addr % width != 0) { ch.state = ERROR_ST; return; }
        if (ch.dst_addr % width != 0) { ch.state = ERROR_ST; return; }
        // Success
        ch.state = RUNNING;
        ch.curr_src = ch.src_addr;
        ch.curr_dst = ch.dst_addr;
        ch.remaining = ch.xfer_size;
        ch.wrap_count = 0;
        ch.half_fired = false;
        ch.pause_pending = false;
        ch.abort_pending = false;
        m_start_event.notify(sc_core::SC_ZERO_TIME);
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        unsigned char* ptr = trans.get_data_ptr();
        uint64_t addr = trans.get_address();
        unsigned int len = trans.get_data_length();
        unsigned int sw = trans.get_streaming_width();
        tlm::tlm_command cmd = trans.get_command();

        // Basic checks
        if (cmd != tlm::TLM_READ_COMMAND && cmd != tlm::TLM_WRITE_COMMAND) {
            trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE); return;
        }
        if (len != 4) { trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE); return; }
        if (sw != 0 && sw != 4) { trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE); return; }
        if (addr % 4 != 0) { trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE); return; }
        if (addr > 0x1FF) { trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }

        uint32_t wdata = 0;
        if (cmd == tlm::TLM_WRITE_COMMAND) std::memcpy(&wdata, ptr, 4);
        uint32_t rdata = 0;
        bool valid = false;

        if (addr < 0x018) {
            valid = handle_global(addr, cmd, wdata, rdata);
        } else if (addr < 0x100) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return;
        } else {
            unsigned ch_idx = (unsigned)(addr - 0x100) / 0x40;
            unsigned offset = (unsigned)(addr - 0x100) % 0x40;
            if (ch_idx >= m_num_channels) {
                trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return;
            }
            valid = handle_channel(ch_idx, offset, cmd, wdata, rdata);
        }

        if (!valid) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return;
        }
        if (cmd == tlm::TLM_READ_COMMAND) std::memcpy(ptr, &rdata, 4);
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    bool handle_global(uint64_t addr, tlm::tlm_command cmd, uint32_t wdata, uint32_t& rdata) {
        switch (addr) {
            case 0x000: rdata = get_status_reg(); return true;
            case 0x004: rdata = m_int_status; return true;
            case 0x008:
                rdata = 0;
                if (cmd == tlm::TLM_WRITE_COMMAND) {
                    m_int_status &= ~(wdata & 0xFFF);
                    update_irqs();
                }
                return true;
            case 0x00C:
                if (cmd == tlm::TLM_WRITE_COMMAND) {
                    if (wdata & 2) { do_global_reset(); }
                    else { m_global_ctrl = wdata & 1; }
                } 
                rdata = m_global_ctrl & 1;
                return true;
            case 0x010: rdata = 0x444D4102; return true;
            case 0x014:
                if (cmd == tlm::TLM_WRITE_COMMAND) { m_int_mask = wdata & 0xFFF; update_irqs(); }
                rdata = m_int_mask;
                return true;
            default: return false;
        }
    }

    bool handle_channel(unsigned idx, unsigned offset, tlm::tlm_command cmd, uint32_t wdata, uint32_t& rdata) {
        Channel& ch = m_channels[idx];

        // COMPLETE state: any write is implicit ack
        if (cmd == tlm::TLM_WRITE_COMMAND && ch.state == COMPLETE && offset != 0x14) {
            ch.state = IDLE;
            // If writing to src/dst/size/config/ctrl, transition to CONFIGURED
        }

        switch (offset) {
            case 0x00: // SRC_ADDR
                rdata = ch.src_addr;
                if (cmd == tlm::TLM_WRITE_COMMAND) {
                    if (ch.state == IDLE || ch.state == CONFIGURED) {
                        ch.src_addr = wdata; ch.state = CONFIGURED;
                    } else if (ch.state == SUSPENDED) {
                        ch.src_addr = wdata; ch.curr_src = wdata;
                    }
                }
                return true;
            case 0x04: // DST_ADDR
                rdata = ch.dst_addr;
                if (cmd == tlm::TLM_WRITE_COMMAND) {
                    if (ch.state == IDLE || ch.state == CONFIGURED) {
                        ch.dst_addr = wdata; ch.state = CONFIGURED;
                    } else if (ch.state == SUSPENDED) {
                        ch.dst_addr = wdata; ch.curr_dst = wdata;
                    }
                }
                return true;
            case 0x08: // XFER_SIZE
                rdata = ch.xfer_size;
                if (cmd == tlm::TLM_WRITE_COMMAND) {
                    uint32_t val = wdata & 0x1FFFF;
                    if (val > 0x10000) val = val & 0xFFFF;
                    if (ch.state == IDLE || ch.state == CONFIGURED) {
                        ch.xfer_size = val; ch.state = CONFIGURED;
                    } else if (ch.state == SUSPENDED) {
                        uint32_t transferred = ch.xfer_size - ch.remaining;
                        ch.xfer_size = val;
                        int32_t new_rem = (int32_t)val - (int32_t)transferred;
                        uint32_t rem = (new_rem <= 0) ? 0 : (uint32_t)new_rem;
                        unsigned w = get_width(idx);
                        if (w > 1 && rem % w != 0) rem = (rem / w) * w;
                        ch.remaining = rem;
                    }
                }
                return true;
            case 0x0C: // CTRL
                rdata = ch.ctrl & 0x0FC2; // readable: CIRCULAR(1), WIDTH(7:6), BURST(9:8), PRIORITY(11:10)
                if (cmd == tlm::TLM_WRITE_COMMAND) {
                    handle_ctrl_write(idx, wdata);
                }
                return true;
            case 0x10: // CONFIG
                rdata = ch.config & 0xF;
                if (cmd == tlm::TLM_WRITE_COMMAND) {
                    if (ch.state == IDLE || ch.state == CONFIGURED) {
                        ch.config = wdata & 0xF;
                        if (ch.state == IDLE) ch.state = CONFIGURED;
                    }
                }
                return true;
            case 0x14: rdata = get_ch_status(idx); return true;
            case 0x18: rdata = ch.curr_src; return true;
            case 0x1C: rdata = ch.curr_dst; return true;
            case 0x20: rdata = ch.remaining; return true;
            case 0x24: rdata = ch.wrap_count; return true;
            case 0x28: // LINK
                rdata = ch.link & 0x83; // bit7 + bits[1:0]
                if (cmd == tlm::TLM_WRITE_COMMAND) {
                    ch.link = wdata & 0x83;
                }
                return true;
            case 0x2C: case 0x30: case 0x34: case 0x38: case 0x3C:
                rdata = 0; return true;
            default: return false;
        }
    }

    void handle_ctrl_write(unsigned idx, uint32_t wdata) {
        Channel& ch = m_channels[idx];

        if (ch.state == IDLE || ch.state == CONFIGURED) {
            ch.ctrl = wdata & 0xFC2; // CIRCULAR, WIDTH, BURST, PRIORITY (mask out action bits)
            if (ch.state == IDLE) ch.state = CONFIGURED;
        } else if (ch.state == RUNNING || ch.state == SUSPENDED) {
            // Only CIRCULAR, PRIORITY writable
            ch.ctrl = (ch.ctrl & ~0xC02) | (wdata & 0x002) | (wdata & 0xC00);
        } else if (ch.state == ERROR_ST) {
            // Only CLR_ERR
            if (wdata & 0x20) { ch.state = IDLE; }
            return;
        }

        // Process action bits
        bool do_start = (wdata & 1) != 0;
        bool do_pause = (wdata & 4) != 0;
        bool do_resume = (wdata & 8) != 0;
        bool do_abort = (wdata & 0x10) != 0;
        bool do_clr = (wdata & 0x20) != 0;

        if (do_start) {
            start_channel(idx);
        }
        if (do_pause && m_channels[idx].state == RUNNING) {
            m_channels[idx].pause_pending = true;
        }
        if (do_resume && m_channels[idx].state == SUSPENDED) {
            m_channels[idx].state = RUNNING;
            m_channels[idx].pause_pending = false;
            m_start_event.notify(sc_core::SC_ZERO_TIME);
        }
        if (do_abort && (m_channels[idx].state == RUNNING || m_channels[idx].state == SUSPENDED)) {
            m_channels[idx].abort_pending = true;
            if (m_channels[idx].state == SUSPENDED) {
                m_channels[idx].state = ERROR_ST;
                m_int_status |= (1u << (idx * 3 + 2));
                update_irqs();
            }
        }
        if (do_clr && m_channels[idx].state == ERROR_ST) {
            m_channels[idx].state = IDLE;
        }
    }

    void transfer_thread() {
        while (true) {
            // Check for any RUNNING channel
            int sel = select_channel();
            if (sel < 0) {
                wait(m_start_event | m_reset_event);
                if (m_reset_flag) { m_reset_flag = false; continue; }
                continue;
            }

            Channel& ch = m_channels[sel];
            unsigned width = get_width(sel);
            unsigned burst = get_burst(sel);
            unsigned beats = std::min(burst, ch.remaining / width);
            if (beats == 0) beats = 1; // safety

            unsigned src_mode = ch.config & 3;
            unsigned dst_mode = (ch.config >> 2) & 3;

            for (unsigned b = 0; b < beats; b++) {
                if (m_reset_flag) { m_reset_flag = false; break; }
                if (ch.abort_pending) {
                    ch.abort_pending = false;
                    ch.state = ERROR_ST;
                    ch.pause_pending = false;
                    m_int_status |= (1u << (sel * 3 + 2));
                    update_irqs();
                    goto next_arb;
                }

                // Read
                {
                    tlm::tlm_generic_payload txn;
                    sc_core::sc_time d = sc_core::SC_ZERO_TIME;
                    uint8_t buf[4] = {};
                    txn.set_command(tlm::TLM_READ_COMMAND);
                    txn.set_address(ch.curr_src);
                    txn.set_data_ptr(buf);
                    txn.set_data_length(width);
                    txn.set_streaming_width(width);
                    txn.set_byte_enable_ptr(nullptr);
                    txn.set_byte_enable_length(0);
                    txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
                    initiator_socket->b_transport(txn, d);
                    if (txn.get_response_status() != tlm::TLM_OK_RESPONSE) {
                        ch.state = ERROR_ST; ch.pause_pending = false;
                        m_int_status |= (1u << (sel * 3 + 2));
                        update_irqs();
                        goto next_arb;
                    }
                    // Write
                    tlm::tlm_generic_payload wtxn;
                    sc_core::sc_time wd = sc_core::SC_ZERO_TIME;
                    wtxn.set_command(tlm::TLM_WRITE_COMMAND);
                    wtxn.set_address(ch.curr_dst);
                    wtxn.set_data_ptr(buf);
                    wtxn.set_data_length(width);
                    wtxn.set_streaming_width(width);
                    wtxn.set_byte_enable_ptr(nullptr);
                    wtxn.set_byte_enable_length(0);
                    wtxn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
                    initiator_socket->b_transport(wtxn, wd);
                    if (wtxn.get_response_status() != tlm::TLM_OK_RESPONSE) {
                        ch.state = ERROR_ST; ch.pause_pending = false;
                        m_int_status |= (1u << (sel * 3 + 2));
                        update_irqs();
                        goto next_arb;
                    }
                }

                // Update addresses
                if (src_mode == 0) ch.curr_src += width;
                else if (src_mode == 1) ch.curr_src -= width;
                if (dst_mode == 0) ch.curr_dst += width;
                else if (dst_mode == 1) ch.curr_dst -= width;

                ch.remaining -= width;

                // Half-transfer check
                if (!ch.half_fired && ch.remaining <= ch.xfer_size / 2) {
                    ch.half_fired = true;
                    m_int_status |= (1u << (sel * 3 + 1));
                    update_irqs();
                }

                // Completion
                if (ch.remaining == 0) {
                    if (ch.ctrl & 2) { // circular
                        ch.wrap_count++;
                        ch.curr_src = ch.src_addr;
                        ch.curr_dst = ch.dst_addr;
                        ch.remaining = ch.xfer_size;
                        ch.half_fired = false;
                        m_int_status |= (1u << (sel * 3));
                        update_irqs();
                    } else {
                        ch.state = COMPLETE;
                        ch.pause_pending = false;
                        m_int_status |= (1u << (sel * 3));
                        update_irqs();
                        // Linked mode
                        if ((ch.link & 0x80) && ch.state == COMPLETE) {
                            unsigned target = ch.link & 3;
                            if (target < m_num_channels) start_channel(target, true);
                        }
                        goto next_arb;
                    }
                    break; // circular: end burst after wrap
                }
            }

            // Post-burst: check pause
            if (ch.state == RUNNING && ch.pause_pending) {
                ch.pause_pending = false;
                ch.state = SUSPENDED;
            }

            next_arb:
            wait(10, sc_core::SC_NS);
            if (m_reset_flag) { m_reset_flag = false; }
        }
    }

    int select_channel() {
        // Find max priority among RUNNING channels
        int max_pri = -1;
        for (unsigned i = 0; i < m_num_channels; i++)
            if (m_channels[i].state == RUNNING)
                max_pri = std::max(max_pri, (int)get_priority(i));
        if (max_pri < 0) return -1;

        // Starvation prevention
        bool force_lower = false;
        if (max_pri == m_last_priority) {
            m_consec_same_priority++;
            if (m_consec_same_priority >= 8) {
                // Check if lower priority channel exists
                for (unsigned i = 0; i < m_num_channels; i++) {
                    if (m_channels[i].state == RUNNING && (int)get_priority(i) < max_pri) {
                        force_lower = true; break;
                    }
                }
                if (force_lower) m_consec_same_priority = 0;
            }
        } else {
            m_last_priority = max_pri;
            m_consec_same_priority = 1;
        }

        int target_pri = max_pri;
        if (force_lower) {
            target_pri = -1;
            for (unsigned i = 0; i < m_num_channels; i++)
                if (m_channels[i].state == RUNNING && (int)get_priority(i) < max_pri)
                    target_pri = std::max(target_pri, (int)get_priority(i));
        }

        // Round-robin within target priority
        for (unsigned i = 0; i < m_num_channels; i++) {
            unsigned idx = (m_rr_pointer + i) % m_num_channels;
            if (m_channels[idx].state == RUNNING && (int)get_priority(idx) == target_pri) {
                m_rr_pointer = (idx + 1) % m_num_channels;
                return (int)idx;
            }
        }
        return -1;
    }
};

#endif // DMA_CONTROLLER_H
