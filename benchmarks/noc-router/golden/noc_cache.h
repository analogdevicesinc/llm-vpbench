#ifndef NOC_CACHE_H
#define NOC_CACHE_H
//
// noc_route_cache — Fully-associative LRU cache mapping address ranges to
// target ports, used to accelerate repeated route-table lookups.
//
// noc_data_cache — Direct-mapped write-through data cache with per-byte
// valid tracking, used to reduce downstream target accesses for read-heavy
// traffic patterns.
//

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

class noc_route_cache {
  public:
    // Default number of fully-associative route cache entries.
    static constexpr unsigned int DEFAULT_CAPACITY = 8;

    // Initialise all entries as invalid; LRU counter starts at zero.
    noc_route_cache(unsigned int capacity = DEFAULT_CAPACITY)
        : m_capacity(capacity), m_lru_tick(0), m_entries(capacity) {
        for (auto &e : m_entries)
            e.valid = false;
    }

    // Search for an entry covering addr; on hit, return target port and cacheable flag.
    bool lookup(uint64_t addr, unsigned int &tgt_port, bool &cacheable) {
        for (auto &e : m_entries) {
            if (e.valid && addr >= e.addr_start && addr < e.addr_end) {
                e.lru_counter = ++m_lru_tick;
                tgt_port      = e.tgt_port;
                cacheable     = e.cacheable;
                return true;
            }
        }
        return false;
    }

    // Insert or update a route entry; evicts LRU entry if cache is full.
    void update(uint64_t addr_start, uint64_t addr_end, unsigned int tgt_port, bool cacheable) {
        for (auto &e : m_entries) {
            if (e.valid && e.addr_start == addr_start && e.addr_end == addr_end) {
                e.tgt_port    = tgt_port;
                e.cacheable   = cacheable;
                e.lru_counter = ++m_lru_tick;
                return;
            }
        }

        for (auto &e : m_entries) {
            if (!e.valid) {
                e.valid       = true;
                e.addr_start  = addr_start;
                e.addr_end    = addr_end;
                e.tgt_port    = tgt_port;
                e.cacheable   = cacheable;
                e.lru_counter = ++m_lru_tick;
                return;
            }
        }

        auto it         = std::min_element(m_entries.begin(), m_entries.end(),
                                           [](const Entry &a, const Entry &b) { return a.lru_counter < b.lru_counter; });
        it->valid       = true;
        it->addr_start  = addr_start;
        it->addr_end    = addr_end;
        it->tgt_port    = tgt_port;
        it->cacheable   = cacheable;
        it->lru_counter = ++m_lru_tick;
    }

    // Invalidate all entries and reset the LRU counter.
    void flush() {
        for (auto &e : m_entries)
            e.valid = false;
        m_lru_tick = 0;
    }

  private:
    // Each entry maps an address range [addr_start, addr_end) to a target port.
    struct Entry {
        bool         valid;
        uint64_t     addr_start;
        uint64_t     addr_end;
        unsigned int tgt_port;
        bool         cacheable;
        uint64_t     lru_counter; // monotonic access timestamp for LRU eviction
    };

    unsigned int       m_capacity; // maximum number of entries
    uint64_t           m_lru_tick; // global monotonic counter (incremented on every access)
    std::vector<Entry> m_entries;
};

// Direct-mapped data cache: 16 lines × 32 bytes, write-through policy.
class noc_data_cache {
  public:
    static constexpr unsigned int DEFAULT_NUM_LINES = 16;
    static constexpr unsigned int DEFAULT_LINE_SIZE = 32;

    // Construct with all lines invalid and zero-filled.
    noc_data_cache(unsigned int num_lines = DEFAULT_NUM_LINES, unsigned int line_size = DEFAULT_LINE_SIZE)
        : m_num_lines(num_lines), m_line_size(line_size), m_lines(num_lines) {
        for (auto &l : m_lines) {
            l.valid      = false;
            l.tag        = 0;
            l.byte_valid = 0; // 32-bit mask: one bit per valid byte
            l.data.resize(line_size, 0);
        }
    }

    // Read from cache if the line is valid, tag matches, and all requested bytes are valid.
    bool read(uint64_t addr, unsigned char *data_out, unsigned int len) {
        unsigned int idx = line_index(addr);
        if (m_lines[idx].valid && m_lines[idx].tag == line_tag(addr)) {
            unsigned int ofs = line_offset(addr);
            if (ofs + len <= m_line_size) {
                uint32_t mask = byte_mask(ofs, len);
                if ((m_lines[idx].byte_valid & mask) == mask) {
                    std::memcpy(data_out, m_lines[idx].data.data() + ofs, len);
                    return true;
                }
            }
        }
        return false;
    }

    // Write-through update: merge bytes into an existing cache line.
    bool write(uint64_t addr, const unsigned char *data_in, unsigned int len) {
        unsigned int idx = line_index(addr);
        if (m_lines[idx].valid && m_lines[idx].tag == line_tag(addr)) {
            unsigned int ofs = line_offset(addr);
            if (ofs + len <= m_line_size) {
                std::memcpy(m_lines[idx].data.data() + ofs, data_in, len);
                m_lines[idx].byte_valid |= byte_mask(ofs, len);
            }
            return true;
        }
        return false;
    }

    // Allocate a new cache line (or evict the existing one at the same index).
    bool allocate(uint64_t addr, const unsigned char *data, unsigned int len) {
        unsigned int idx        = line_index(addr);
        bool         evicted    = m_lines[idx].valid;
        m_lines[idx].valid      = true;
        m_lines[idx].tag        = line_tag(addr);
        m_lines[idx].byte_valid = 0;
        std::memset(m_lines[idx].data.data(), 0, m_line_size);
        unsigned int ofs      = line_offset(addr);
        unsigned int copy_len = std::min(len, m_line_size - ofs);
        std::memcpy(m_lines[idx].data.data() + ofs, data, copy_len);
        m_lines[idx].byte_valid = byte_mask(ofs, copy_len);
        return evicted;
    }

    // Invalidate all data cache lines.
    void flush() {
        for (auto &l : m_lines)
            l.valid = false;
    }

    // Return the number of currently valid cache lines (for DATA_CACHE_STATUS).
    unsigned int valid_count() const {
        unsigned int count = 0;
        for (const auto &l : m_lines) {
            if (l.valid) ++count;
        }
        return count;
    }

  private:
    // Direct-mapped cache line: tag identifies the aligned block in memory.
    struct Line {
        bool                       valid;
        uint64_t                   tag;        // address tag (addr / (line_size * num_lines))
        uint32_t                   byte_valid; // per-byte valid bitmask (up to 32 bytes)
        std::vector<unsigned char> data;
    };

    unsigned int      m_num_lines; // number of cache lines (index bits)
    unsigned int      m_line_size; // bytes per line (offset bits)
    std::vector<Line> m_lines;

    // Build a bitmask covering 'len' bytes starting at byte offset 'ofs'.
    static uint32_t byte_mask(unsigned int ofs, unsigned int len) {
        if (len >= 32) return ~0u;
        return ((1u << len) - 1u) << ofs;
    }

    // Extract the cache line index from an address.
    unsigned int line_index(uint64_t addr) const {
        return static_cast<unsigned int>((addr / m_line_size) % m_num_lines);
    }

    // Extract the tag portion of an address for comparison.
    uint64_t line_tag(uint64_t addr) const { return addr / (static_cast<uint64_t>(m_line_size) * m_num_lines); }

    // Extract the byte offset within a cache line.
    unsigned int line_offset(uint64_t addr) const { return static_cast<unsigned int>(addr % m_line_size); }
};

#endif
