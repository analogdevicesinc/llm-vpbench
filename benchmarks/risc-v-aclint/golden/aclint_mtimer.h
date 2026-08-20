#ifndef ACLINT_MTIMER_H
#define ACLINT_MTIMER_H

// RISC-V ACLINT MTIMER (Machine-level Timer) Device
// Implements shared mtime counter and per-hart mtimecmp comparators.
// Extended with prescaler, timer_enable, and overflow_flag control registers.
// Conforms to TLM-2.0 with blocking transport, DMI, and debug transport.

#include <cstdint>
#include <cstring>
#include <systemc>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>
#include <vector>

class aclint_mtimer : public sc_core::sc_module {
public:
  // Address map offsets
  static constexpr uint64_t MTIMECMP_BASE = 0x0000;
  static constexpr uint64_t OVERFLOW_FLAG_OFFSET = 0x7FE0;
  static constexpr uint64_t TIMER_ENABLE_OFFSET = 0x7FE8;
  static constexpr uint64_t PRESCALER_OFFSET = 0x7FF0;
  static constexpr uint64_t MTIME_OFFSET = 0x7FF8;
  static constexpr uint64_t ADDR_SPACE_SIZE = 0x8000;

  // WARL limits and masks
  static constexpr uint32_t MAX_PRESCALER = 65535;
  static constexpr uint32_t TIMER_ENABLE_MASK = 0x1;

  // Register width constants
  static constexpr unsigned int REG_WIDTH_32 = 4;
  static constexpr unsigned int REG_WIDTH_64 = 8;
  static constexpr unsigned int HIGH_WORD_SHIFT = 32;
  static constexpr unsigned int HIGH_WORD_BYTE_OFS = 4;
  static constexpr unsigned char BE_FULL = 0xFF;

  // 64-bit split-access masks
  static constexpr uint64_t LOW_WORD_MASK = 0x00000000FFFFFFFFULL;
  static constexpr uint64_t HIGH_WORD_MASK = 0xFFFFFFFF00000000ULL;
  static constexpr uint64_t MTIME_MAX = 0xFFFFFFFFFFFFFFFFULL;

  // Alignment masks for address validation
  static constexpr uint64_t ALIGN_MASK_32 = 0x3;
  static constexpr uint64_t ALIGN_MASK_64 = 0x7;

  // TLM-2.0 target socket and per-hart timer interrupt outputs
  tlm_utils::simple_target_socket<aclint_mtimer> socket;
  sc_core::sc_vector<sc_core::sc_out<bool>> timer_irq;

  SC_HAS_PROCESS(aclint_mtimer);

  // Constructor: configurable tick period and hart count
  aclint_mtimer(sc_core::sc_module_name name,
                sc_core::sc_time tick = sc_core::sc_time(1, sc_core::SC_US),
                unsigned int num_harts = 8)
      : sc_module(name), socket("socket"), timer_irq("timer_irq", num_harts),
        m_num_harts(num_harts), m_tick_period(tick), mtime(0), m_prescaler(1),
        m_timer_enable(1), m_overflow_flag(0), mtimecmp(num_harts, MTIME_MAX) {
    socket.register_b_transport(this, &aclint_mtimer::b_transport);
    socket.register_get_direct_mem_ptr(this,
                                       &aclint_mtimer::get_direct_mem_ptr);
    socket.register_transport_dbg(this, &aclint_mtimer::transport_dbg);
    SC_THREAD(timer_thread);
  }

  // Blocking transport: validates then dispatches to per-register handlers
  void b_transport(tlm::tlm_generic_payload &trans,
                   sc_core::sc_time & /*delay*/) {
    // Common validation (null ptr, width, byte-enable, alignment)
    if (!validate_transaction(trans))
      return;

    uint64_t addr = trans.get_address();
    unsigned char *data_ptr = trans.get_data_ptr();
    unsigned int data_len = trans.get_data_length();
    tlm::tlm_command cmd = trans.get_command();

    // Compute end of the per-hart mtimecmp region
    uint64_t mtimecmp_end =
        static_cast<uint64_t>(m_num_harts) * REG_WIDTH_64 - 1;

    // Route to the appropriate register handler
    if (addr <= mtimecmp_end && addr + data_len - 1 <= mtimecmp_end) {
      handle_mtimecmp(addr, data_ptr, data_len, cmd, trans);
    } else if (addr == OVERFLOW_FLAG_OFFSET && data_len == REG_WIDTH_32) {
      handle_overflow_flag(data_ptr, cmd, trans);
    } else if (addr == TIMER_ENABLE_OFFSET && data_len == REG_WIDTH_32) {
      handle_timer_enable(data_ptr, cmd, trans);
    } else if (addr == PRESCALER_OFFSET && data_len == REG_WIDTH_32) {
      handle_prescaler(data_ptr, cmd, trans);
    } else if (addr >= MTIME_OFFSET &&
               addr + data_len - 1 <= MTIME_OFFSET + REG_WIDTH_64 - 1) {
      handle_mtime(addr, data_ptr, data_len, cmd, trans);
    } else {
      trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
  }

  // DMI: grants direct pointer access to the mtime register
  bool get_direct_mem_ptr(tlm::tlm_generic_payload & /*trans*/,
                          tlm::tlm_dmi &dmi_data) {
    dmi_data.set_dmi_ptr(reinterpret_cast<unsigned char *>(&mtime));
    dmi_data.set_start_address(MTIME_OFFSET);
    dmi_data.set_end_address(MTIME_OFFSET + REG_WIDTH_64 - 1);
    dmi_data.allow_read_write();
    dmi_data.set_read_latency(sc_core::SC_ZERO_TIME);
    dmi_data.set_write_latency(sc_core::SC_ZERO_TIME);
    return true;
  }

  // Debug transport: untimed bulk read/write for mtime and mtimecmp regions
  unsigned int transport_dbg(tlm::tlm_generic_payload &trans) {
    unsigned char *data_ptr = trans.get_data_ptr();
    uint64_t addr = trans.get_address();
    unsigned int len = trans.get_data_length();
    tlm::tlm_command cmd = trans.get_command();
    if (data_ptr == nullptr)
      return 0;

    // mtime region: MTIME_OFFSET .. MTIME_OFFSET+7
    if (addr >= MTIME_OFFSET && addr < MTIME_OFFSET + REG_WIDTH_64) {
      unsigned int offset = static_cast<unsigned int>(addr - MTIME_OFFSET);
      unsigned int avail = REG_WIDTH_64 - offset;
      unsigned int actual = (len < avail) ? len : avail;
      unsigned char *base = reinterpret_cast<unsigned char *>(&mtime);
      if (cmd == tlm::TLM_READ_COMMAND) {
        std::memcpy(data_ptr, base + offset, actual);
      } else if (cmd == tlm::TLM_WRITE_COMMAND) {
        std::memcpy(base + offset, data_ptr, actual);
        update_irqs();
      } else {
        return 0;
      }
      return actual;
    }

    // mtimecmp region: 0 .. num_harts*8-1
    uint64_t mtimecmp_end_addr =
        static_cast<uint64_t>(m_num_harts) * REG_WIDTH_64;
    if (addr < mtimecmp_end_addr) {
      unsigned int avail = static_cast<unsigned int>(mtimecmp_end_addr - addr);
      unsigned int actual = (len < avail) ? len : avail;
      unsigned char *base = reinterpret_cast<unsigned char *>(mtimecmp.data());
      if (cmd == tlm::TLM_READ_COMMAND) {
        std::memcpy(data_ptr, base + addr, actual);
      } else if (cmd == tlm::TLM_WRITE_COMMAND) {
        std::memcpy(base + addr, data_ptr, actual);
        update_irqs();
      } else {
        return 0;
      }
      return actual;
    }
    return 0;
  }

private:
  unsigned int m_num_harts;       // configured hart count
  sc_core::sc_time m_tick_period; // base tick period before prescaling
  uint64_t mtime;                 // shared 64-bit free-running counter
  uint32_t m_prescaler;           // tick divider, WARL [1..65535]
  uint32_t m_timer_enable;        // bit[0]: 0=frozen, 1=counting
  uint32_t m_overflow_flag;       // set on mtime wrap, read-to-clear
  std::vector<uint64_t> mtimecmp; // per-hart 64-bit comparators

  // Validate common transaction properties: null ptr, data width,
  // byte enables, and address alignment. Returns false on error.
  bool validate_transaction(tlm::tlm_generic_payload &trans) {
    unsigned char *data_ptr = trans.get_data_ptr();
    unsigned int data_len = trans.get_data_length();

    if (data_ptr == nullptr) {
      trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
      return false;
    }
    // Only 32-bit or 64-bit accesses are supported
    if (data_len != REG_WIDTH_32 && data_len != REG_WIDTH_64) {
      trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
      return false;
    }
    // Reject partial byte enables — all lanes must be 0xFF
    if (trans.get_byte_enable_ptr() != nullptr) {
      unsigned char *be = trans.get_byte_enable_ptr();
      unsigned int be_len = trans.get_byte_enable_length();
      for (unsigned int i = 0; i < data_len; ++i) {
        if (be[i % be_len] != BE_FULL) {
          trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
          return false;
        }
      }
    }
    // Reject non-trivial streaming width
    unsigned int sw = trans.get_streaming_width();
    if (sw != 0 && sw != data_len) {
      trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
      return false;
    }
    // Enforce natural alignment
    uint64_t addr = trans.get_address();
    if (data_len == REG_WIDTH_32 && (addr & ALIGN_MASK_32) != 0) {
      trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
      return false;
    }
    if (data_len == REG_WIDTH_64 && (addr & ALIGN_MASK_64) != 0) {
      trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
      return false;
    }
    return true;
  }

  // Handle mtimecmp register access (split 32-bit halves or full 64-bit)
  void handle_mtimecmp(uint64_t addr, unsigned char *data_ptr,
                       unsigned int data_len, tlm::tlm_command cmd,
                       tlm::tlm_generic_payload &trans) {
    int hart_id;
    bool is_high;
    if (data_len == REG_WIDTH_32) {
      // Mask off bit[2] to find the 8-byte-aligned base, then divide
      hart_id = static_cast<int>(
          (addr & ~static_cast<uint64_t>(HIGH_WORD_BYTE_OFS)) / REG_WIDTH_64);
      is_high = (addr & HIGH_WORD_BYTE_OFS) != 0;
    } else {
      hart_id = static_cast<int>(addr / REG_WIDTH_64);
      is_high = false;
    }
    if (hart_id < 0 || hart_id >= static_cast<int>(m_num_harts)) {
      trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
      return;
    }
    if (cmd == tlm::TLM_READ_COMMAND) {
      read_reg_split(mtimecmp[hart_id], data_ptr, data_len, is_high);
    } else if (cmd == tlm::TLM_WRITE_COMMAND) {
      write_reg_split(mtimecmp[hart_id], data_ptr, data_len, is_high);
      update_irqs();
    } else {
      trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
      return;
    }
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
  }

  // Overflow flag: set on mtime wrap, read-to-clear, writes silently ignored
  void handle_overflow_flag(unsigned char *data_ptr, tlm::tlm_command cmd,
                            tlm::tlm_generic_payload &trans) {
    if (cmd == tlm::TLM_READ_COMMAND) {
      uint32_t val = m_overflow_flag;
      std::memcpy(data_ptr, &val, REG_WIDTH_32);
      m_overflow_flag = 0; // read-to-clear semantics
    } else if (cmd == tlm::TLM_WRITE_COMMAND) {
      // writes are silently ignored per spec
    } else {
      trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
      return;
    }
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
  }

  // Timer enable: WARL bit[0] only; 0=frozen, 1=counting
  void handle_timer_enable(unsigned char *data_ptr, tlm::tlm_command cmd,
                           tlm::tlm_generic_payload &trans) {
    if (cmd == tlm::TLM_READ_COMMAND) {
      std::memcpy(data_ptr, &m_timer_enable, REG_WIDTH_32);
    } else if (cmd == tlm::TLM_WRITE_COMMAND) {
      uint32_t val;
      std::memcpy(&val, data_ptr, REG_WIDTH_32);
      m_timer_enable = val & TIMER_ENABLE_MASK;
    } else {
      trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
      return;
    }
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
  }

  // Prescaler: WARL clamped to [1, MAX_PRESCALER]
  void handle_prescaler(unsigned char *data_ptr, tlm::tlm_command cmd,
                        tlm::tlm_generic_payload &trans) {
    if (cmd == tlm::TLM_READ_COMMAND) {
      std::memcpy(data_ptr, &m_prescaler, REG_WIDTH_32);
    } else if (cmd == tlm::TLM_WRITE_COMMAND) {
      uint32_t val;
      std::memcpy(&val, data_ptr, REG_WIDTH_32);
      if (val == 0)
        val = 1; // WARL: clamp minimum to 1
      if (val > MAX_PRESCALER)
        val = MAX_PRESCALER; // WARL: clamp max
      m_prescaler = val;
    } else {
      trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
      return;
    }
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
  }

  // Handle mtime register access (split 32-bit halves or full 64-bit)
  void handle_mtime(uint64_t addr, unsigned char *data_ptr,
                    unsigned int data_len, tlm::tlm_command cmd,
                    tlm::tlm_generic_payload &trans) {
    bool is_high;
    if (data_len == REG_WIDTH_32) {
      is_high = (addr & HIGH_WORD_BYTE_OFS) != 0;
    } else {
      if (addr != MTIME_OFFSET) {
        trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return;
      }
      is_high = false;
    }
    if (cmd == tlm::TLM_READ_COMMAND) {
      read_reg_split(mtime, data_ptr, data_len, is_high);
    } else if (cmd == tlm::TLM_WRITE_COMMAND) {
      write_reg_split(mtime, data_ptr, data_len, is_high);
      update_irqs();
    } else {
      trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
      return;
    }
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
  }

  // Read the low 32-bit half, high 32-bit half, or full 64-bit register
  void read_reg_split(uint64_t reg, unsigned char *data, unsigned int len,
                      bool is_high) {
    if (len == REG_WIDTH_32 && !is_high) {
      uint32_t lo = static_cast<uint32_t>(reg);
      std::memcpy(data, &lo, REG_WIDTH_32);
    } else if (len == REG_WIDTH_32 && is_high) {
      uint32_t hi = static_cast<uint32_t>(reg >> HIGH_WORD_SHIFT);
      std::memcpy(data, &hi, REG_WIDTH_32);
    } else {
      std::memcpy(data, &reg, REG_WIDTH_64);
    }
  }

  // Write the low 32-bit half, high 32-bit half, or full 64-bit register
  void write_reg_split(uint64_t &reg, unsigned char *data, unsigned int len,
                       bool is_high) {
    if (len == REG_WIDTH_32 && !is_high) {
      uint32_t lo;
      std::memcpy(&lo, data, REG_WIDTH_32);
      reg = (reg & HIGH_WORD_MASK) | lo;
    } else if (len == REG_WIDTH_32 && is_high) {
      uint32_t hi;
      std::memcpy(&hi, data, REG_WIDTH_32);
      reg = (reg & LOW_WORD_MASK) |
            (static_cast<uint64_t>(hi) << HIGH_WORD_SHIFT);
    } else {
      std::memcpy(&reg, data, REG_WIDTH_64);
    }
  }

  // Compare mtime against all mtimecmp values and drive interrupt outputs
  void update_irqs() {
    for (unsigned int i = 0; i < m_num_harts; ++i)
      timer_irq[i].write(mtime >= mtimecmp[i]);
  }

  // Timer thread: increments mtime at tick_period * prescaler rate
  void timer_thread() {
    while (true) {
      uint32_t ps = m_prescaler;
      if (ps == 0)
        ps = 1; // safety clamp
      wait(m_tick_period * ps);
      if (m_timer_enable & TIMER_ENABLE_MASK) {
        if (mtime == MTIME_MAX) {
          mtime = 0;
          m_overflow_flag = 1; // wrap detected
        } else {
          mtime++;
        }
        update_irqs();
      }
    }
  }
};
#endif