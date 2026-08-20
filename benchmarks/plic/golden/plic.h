#ifndef PLIC_H
#define PLIC_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <cstring>
#include <vector>

SC_MODULE(plic) {
    SC_HAS_PROCESS(plic);

    tlm_utils::simple_target_socket<plic> socket;
    sc_core::sc_vector<sc_core::sc_in<bool>> src_irq;
    sc_core::sc_out<bool> irq_out;

    plic(sc_core::sc_module_name name, unsigned int num_sources = 8)
        : sc_module(name)
        , socket("socket")
        , src_irq("src_irq", num_sources)
        , irq_out("irq_out")
        , m_num_sources(num_sources)
        , m_src_mask(((1u << (num_sources + 1)) - 1) & ~1u)
        , m_priority(num_sources, 0)
        , m_enable(0)
        , m_threshold(0)
        , m_int_type(0)
        , m_gw_state(num_sources, IDLE)
        , m_prev_irq(num_sources, false)
    {
        socket.register_b_transport(this, &plic::b_transport);
        SC_METHOD(irq_method);
        for (unsigned i = 0; i < m_num_sources; ++i)
            sensitive << src_irq[i];
        dont_initialize();
    }

private:
    enum GatewayState { IDLE, PENDING, CLAIMED };

    unsigned int m_num_sources;
    uint32_t m_src_mask; // valid source bits mask: bits 1..S
    std::vector<uint32_t> m_priority;
    uint32_t m_enable;
    uint32_t m_threshold;
    uint32_t m_int_type;
    std::vector<GatewayState> m_gw_state;
    std::vector<bool> m_prev_irq;

    void irq_method() {
        for (unsigned i = 0; i < m_num_sources; ++i) {
            bool cur = src_irq[i].read();
            unsigned bit = i + 1;
            bool is_edge = (m_int_type & (1u << bit)) != 0;

            switch (m_gw_state[i]) {
            case IDLE:
                if (is_edge) {
                    if (cur && !m_prev_irq[i])
                        m_gw_state[i] = PENDING;
                } else {
                    if (cur)
                        m_gw_state[i] = PENDING;
                }
                break;
            case PENDING:
                if (!is_edge) {
                    if (!cur)
                        m_gw_state[i] = IDLE;
                }
                // edge: latched, no change
                break;
            case CLAIMED:
                // ignore input entirely
                break;
            }
            m_prev_irq[i] = cur;
        }
        update_irq_out();
    }

    bool is_pending(unsigned i) const {
        return m_gw_state[i] == PENDING;
    }

    uint32_t pending_bits() const {
        uint32_t p = 0;
        for (unsigned i = 0; i < m_num_sources; ++i)
            if (m_gw_state[i] == PENDING)
                p |= (1u << (i + 1));
        return p;
    }

    unsigned resolve() const {
        unsigned best_id = 0;
        uint32_t best_pri = 0;
        for (unsigned i = 0; i < m_num_sources; ++i) {
            if (m_gw_state[i] != PENDING) continue;
            unsigned bit = i + 1;
            if (!(m_enable & (1u << bit))) continue;
            uint32_t pri = m_priority[i];
            if (pri == 0) continue;
            if (pri <= m_threshold) continue;
            if (pri > best_pri || (pri == best_pri && (best_id == 0 || bit < best_id))) {
                best_pri = pri;
                best_id = bit;
            }
        }
        return best_id;
    }

    void update_irq_out() {
        irq_out.write(resolve() != 0);
    }

    void evaluate_pending_for_source(unsigned i) {
        // Re-evaluate source i after INT_TYPE change
        unsigned bit = i + 1;
        bool is_edge = (m_int_type & (1u << bit)) != 0;
        if (m_gw_state[i] == PENDING && !is_edge) {
            // Now level-sensitive: if input low, clear pending
            if (!src_irq[i].read())
                m_gw_state[i] = IDLE;
        }
        // Edge sources in PENDING keep their state (latched)
        // IDLE level sources: if input high, go PENDING
        if (m_gw_state[i] == IDLE && !is_edge) {
            if (src_irq[i].read())
                m_gw_state[i] = PENDING;
        }
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        tlm::tlm_command cmd = trans.get_command();
        uint64_t addr = trans.get_address();
        unsigned char* ptr = trans.get_data_ptr();
        unsigned int len = trans.get_data_length();

        uint64_t max_addr = (uint64_t)m_num_sources * 4 + 23;

        if (cmd != tlm::TLM_READ_COMMAND && cmd != tlm::TLM_WRITE_COMMAND) {
            trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }
        if (addr > max_addr) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if ((addr & 0x3) != 0) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (len != 4) {
            trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }

        uint32_t offset = (uint32_t)addr;
        uint32_t wdata = 0;
        if (cmd == tlm::TLM_WRITE_COMMAND)
            std::memcpy(&wdata, ptr, 4);

        uint32_t rdata = 0;
        uint32_t prio_end = m_num_sources * 4; // exclusive: offsets 0..prio_end-4
        uint32_t pending_off  = prio_end;
        uint32_t enable_off   = prio_end + 4;
        uint32_t thresh_off   = prio_end + 8;
        uint32_t claim_off    = prio_end + 12;
        uint32_t complete_off = prio_end + 16;
        uint32_t inttype_off  = prio_end + 20;

        if (offset < prio_end) {
            // PRIORITY registers
            unsigned idx = offset / 4;
            if (cmd == tlm::TLM_READ_COMMAND) {
                rdata = m_priority[idx];
            } else {
                m_priority[idx] = wdata & 0x7;
                update_irq_out();
            }
        } else if (offset == pending_off) {
            // PENDING (RO) - writes silently ignored
            if (cmd == tlm::TLM_READ_COMMAND)
                rdata = pending_bits();
        } else if (offset == enable_off) {
            if (cmd == tlm::TLM_READ_COMMAND) {
                rdata = m_enable & m_src_mask;
            } else {
                m_enable = wdata & m_src_mask;
                update_irq_out();
            }
        } else if (offset == thresh_off) {
            if (cmd == tlm::TLM_READ_COMMAND) {
                rdata = m_threshold;
            } else {
                m_threshold = wdata & 0x7;
                update_irq_out();
            }
        } else if (offset == claim_off) {
            // CLAIM: read has side-effect, write silently ignored
            if (cmd == tlm::TLM_READ_COMMAND) {
                unsigned id = resolve();
                rdata = id;
                if (id != 0) {
                    m_gw_state[id - 1] = CLAIMED;
                    update_irq_out();
                }
            }
        } else if (offset == complete_off) {
            // COMPLETE: write-only, read returns 0
            if (cmd == tlm::TLM_WRITE_COMMAND) {
                if (wdata >= 1 && wdata <= m_num_sources && m_gw_state[wdata - 1] == CLAIMED) {
                    unsigned idx = wdata - 1;
                    m_gw_state[idx] = IDLE;
                    // Level-sensitive: if input still high, immediately re-pend
                    unsigned bit = idx + 1;
                    bool is_edge = (m_int_type & (1u << bit)) != 0;
                    if (!is_edge && src_irq[idx].read())
                        m_gw_state[idx] = PENDING;
                    update_irq_out();
                }
            } else {
                rdata = 0;
            }
        } else if (offset == inttype_off) {
            if (cmd == tlm::TLM_READ_COMMAND) {
                rdata = m_int_type & m_src_mask;
            } else {
                m_int_type = wdata & m_src_mask;
                for (unsigned i = 0; i < m_num_sources; ++i)
                    evaluate_pending_for_source(i);
                update_irq_out();
            }
        } else {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        if (cmd == tlm::TLM_READ_COMMAND)
            std::memcpy(ptr, &rdata, 4);

        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        delay = sc_core::SC_ZERO_TIME;
    }
};

#endif // PLIC_H
