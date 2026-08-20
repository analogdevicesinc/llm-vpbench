#ifndef NOC_ARBITER_H
#define NOC_ARBITER_H
//
// noc_arbiter — Multi-mode arbitration engine for NoC router.
// Supports round-robin, fixed-priority (with optional QoS), and
// weighted-round-robin arbitration policies.
//

#include <cstdint>
#include <vector>
#include <algorithm>

class noc_arbiter {
  public:
    // Arbitration mode selectors (stored in REG_ARB_MODE).
    static constexpr uint8_t MODE_ROUND_ROBIN    = 0;
    static constexpr uint8_t MODE_FIXED_PRIORITY = 1;
    static constexpr uint8_t MODE_WEIGHTED_RR    = 2;

    // Construct an arbiter for n_init initiators competing for n_tgt targets.
    noc_arbiter(unsigned int n_init, unsigned int n_tgt)
        : m_n_init(n_init), m_n_tgt(n_tgt), m_rr_ptr(n_tgt, 0) // per-target round-robin pointer
          ,
          m_credits(n_tgt, std::vector<unsigned int>(n_init, 1)) // WRR credits
          ,
          m_wrr_ptr(n_tgt, 0) // per-target WRR pointer
    {}

    // Select a winning initiator for the given target port based on the
    // active arbitration mode and QoS settings.
    unsigned int arbitrate(unsigned int tgt_port, const std::vector<bool> &requests,
                           const std::vector<uint8_t> &priorities, const std::vector<uint8_t> &weights, uint8_t mode,
                           bool qos_en) {
        switch (mode) {
        case MODE_ROUND_ROBIN:
            return arb_round_robin(tgt_port, requests, priorities, qos_en);
        case MODE_FIXED_PRIORITY:
            return arb_fixed_priority(requests, priorities, qos_en);
        case MODE_WEIGHTED_RR:
            return arb_weighted_rr(tgt_port, requests, weights);
        default:
            return m_n_init;
        }
    }

    // Reset all pointers and credits to initial state.
    void reset() {
        for (unsigned int t = 0; t < m_n_tgt; ++t) {
            m_rr_ptr[t]  = 0;
            m_wrr_ptr[t] = 0;
            for (unsigned int i = 0; i < m_n_init; ++i)
                m_credits[t][i] = 1;
        }
    }

    // Reload WRR credits from the current weight configuration.
    void reset_credits(const std::vector<uint8_t> &weights) {
        for (unsigned int t = 0; t < m_n_tgt; ++t)
            for (unsigned int i = 0; i < m_n_init; ++i)
                m_credits[t][i] = weights[i];
    }

  private:
    unsigned int                           m_n_init;  // initiator count
    unsigned int                           m_n_tgt;   // target count
    std::vector<unsigned int>              m_rr_ptr;  // RR last-winner per target
    std::vector<std::vector<unsigned int>> m_credits; // WRR credit matrix [tgt][init]
    std::vector<unsigned int>              m_wrr_ptr; // WRR scan pointer per target

    // Round-robin: rotate from last winner; with QoS, prefer highest priority.
    unsigned int arb_round_robin(unsigned int tgt, const std::vector<bool> &requests,
                                 const std::vector<uint8_t> &priorities, bool qos_en) {
        if (!qos_en) {
            unsigned int start = (m_rr_ptr[tgt] + 1) % m_n_init;
            for (unsigned int i = 0; i < m_n_init; ++i) {
                unsigned int idx = (start + i) % m_n_init;
                if (requests[idx]) {
                    m_rr_ptr[tgt] = idx;
                    return idx;
                }
            }
            return m_n_init;
        }

        int max_prio = -1;
        for (unsigned int i = 0; i < m_n_init; ++i) {
            if (requests[i]) {
                int p = static_cast<int>(priorities[i]);
                if (p > max_prio) max_prio = p;
            }
        }
        if (max_prio < 0) return m_n_init;

        unsigned int start = (m_rr_ptr[tgt] + 1) % m_n_init;
        for (unsigned int i = 0; i < m_n_init; ++i) {
            unsigned int idx = (start + i) % m_n_init;
            if (requests[idx] && static_cast<int>(priorities[idx]) == max_prio) {
                m_rr_ptr[tgt] = idx;
                return idx;
            }
        }
        return m_n_init;
    }

    // Fixed-priority: lowest index wins; with QoS, highest priority wins.
    unsigned int arb_fixed_priority(const std::vector<bool> &requests, const std::vector<uint8_t> &priorities,
                                    bool qos_en) {
        unsigned int best      = m_n_init;
        int          best_prio = -1;
        for (unsigned int i = 0; i < m_n_init; ++i) {
            if (requests[i]) {
                int prio = qos_en ? static_cast<int>(priorities[i]) : static_cast<int>(m_n_init - 1 - i);
                if (prio > best_prio || (prio == best_prio && i < best)) {
                    best_prio = prio;
                    best      = i;
                }
            }
        }
        return best;
    }

    // Weighted round-robin: credit-based scheduling using per-initiator weights.
    unsigned int arb_weighted_rr(unsigned int tgt, const std::vector<bool> &requests,
                                 const std::vector<uint8_t> &weights) {
        for (unsigned int attempt = 0; attempt < m_n_init; ++attempt) {
            unsigned int idx = (m_wrr_ptr[tgt] + attempt) % m_n_init;
            if (requests[idx] && m_credits[tgt][idx] > 0) {
                m_credits[tgt][idx]--;
                if (m_credits[tgt][idx] == 0) {
                    m_credits[tgt][idx] = weights[idx];
                    m_wrr_ptr[tgt]      = (idx + 1) % m_n_init;
                }
                return idx;
            }
        }

        for (unsigned int i = 0; i < m_n_init; ++i)
            m_credits[tgt][i] = weights[i];

        for (unsigned int i = 0; i < m_n_init; ++i) {
            unsigned int idx = (m_wrr_ptr[tgt] + i) % m_n_init;
            if (requests[idx]) {
                m_credits[tgt][idx]--;
                if (m_credits[tgt][idx] == 0) {
                    m_credits[tgt][idx] = weights[idx];
                    m_wrr_ptr[tgt]      = (idx + 1) % m_n_init;
                }
                return idx;
            }
        }

        return m_n_init;
    }
};

#endif
