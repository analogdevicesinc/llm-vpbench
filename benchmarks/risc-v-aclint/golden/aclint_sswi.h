#ifndef ACLINT_SSWI_H
#define ACLINT_SSWI_H

// RISC-V ACLINT SSWI (Supervisor-level Software Interrupt) Device
// Implements per-hart setssip registers with WARL bit[0] semantics.
// Conforms to TLM-2.0 with blocking transport, DMI, and debug transport.

#include <cstdint>
#include <cstring>
#include <systemc>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>
#include <vector>

class aclint_sswi : public sc_core::sc_module {
public:
  // Register and address-space constants
  static constexpr uint32_t SETSSIP_MASK = 0x1;
  static constexpr uint64_t ADDR_SPACE_SIZE = 0x4000;
  static constexpr unsigned int REG_WIDTH = 4;
  static constexpr unsigned char BE_FULL = 0xFF;

  // TLM-2.0 target socket and per-hart interrupt outputs
  tlm_utils::simple_target_socket<aclint_sswi> socket;
  sc_core::sc_vector<sc_core::sc_out<bool>> ssw_irq;

  SC_HAS_PROCESS(aclint_sswi);

  // Constructor: num_harts configures the number of hart contexts (default 8)
  aclint_sswi(sc_core::sc_module_name name, unsigned int num_harts = 8)
      : sc_module(name), socket("socket"), ssw_irq("ssw_irq", num_harts),
        m_num_harts(num_harts), setssip(num_harts, 0) {
    socket.register_b_transport(this, &aclint_sswi::b_transport);
    socket.register_get_direct_mem_ptr(this, &aclint_sswi::get_direct_mem_ptr);
    socket.register_transport_dbg(this, &aclint_sswi::transport_dbg);
  }

  // Blocking transport: 32-bit reads/writes to per-hart setssip registers
  void b_transport(tlm::tlm_generic_payload &trans,
                   sc_core::sc_time & /*delay*/) {
    unsigned char *data_ptr = trans.get_data_ptr();
    sc_dt::uint64 addr = trans.get_address();
    unsigned int len = trans.get_data_length();
    tlm::tlm_command cmd = trans.get_command();

    // Validate data pointer
    if (data_ptr == nullptr) {
      trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
      return;
    }
    // Only 32-bit accesses are supported
    if (len != REG_WIDTH) {
      trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
      return;
    }
    // Reject partial byte enables
    if (!validate_byte_enable(trans, len))
      return;
    // Reject non-trivial streaming width
    unsigned int sw = trans.get_streaming_width();
    if (sw != 0 && sw != len) {
      trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
      return;
    }

    // Address decode: each hart occupies REG_WIDTH bytes
    if ((addr % REG_WIDTH) != 0 ||
        addr >= static_cast<uint64_t>(m_num_harts) * REG_WIDTH) {
      trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
      return;
    }
    unsigned int hart_id = static_cast<unsigned int>(addr / REG_WIDTH);

    if (cmd == tlm::TLM_WRITE_COMMAND) {
      uint32_t write_data;
      std::memcpy(&write_data, data_ptr, sizeof(uint32_t));
      setssip[hart_id] = write_data & SETSSIP_MASK; // WARL: only bit[0]
      update_irqs();
      trans.set_response_status(tlm::TLM_OK_RESPONSE);
    } else if (cmd == tlm::TLM_READ_COMMAND) {
      std::memcpy(data_ptr, &setssip[hart_id], sizeof(uint32_t));
      trans.set_response_status(tlm::TLM_OK_RESPONSE);
    } else {
      trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
    }
  }

  // DMI: exposes setssip array for direct read access
  bool get_direct_mem_ptr(tlm::tlm_generic_payload & /*trans*/,
                          tlm::tlm_dmi &dmi_data) {
    dmi_data.set_dmi_ptr(reinterpret_cast<unsigned char *>(setssip.data()));
    dmi_data.set_start_address(0);
    dmi_data.set_end_address(static_cast<uint64_t>(m_num_harts) * REG_WIDTH -
                             1);
    dmi_data.allow_read();
    dmi_data.set_read_latency(sc_core::SC_ZERO_TIME);
    dmi_data.set_write_latency(sc_core::SC_ZERO_TIME);
    return true;
  }

  // Debug transport: bulk read/write bypassing timing annotation
  unsigned int transport_dbg(tlm::tlm_generic_payload &trans) {
    unsigned char *data_ptr = trans.get_data_ptr();
    sc_dt::uint64 addr = trans.get_address();
    unsigned int len = trans.get_data_length();
    tlm::tlm_command cmd = trans.get_command();
    if (data_ptr == nullptr)
      return 0;

    uint64_t reg_end = static_cast<uint64_t>(m_num_harts) * REG_WIDTH;
    if (addr >= reg_end)
      return 0;

    unsigned int max_bytes = static_cast<unsigned int>(reg_end - addr);
    unsigned int actual = (len < max_bytes) ? len : max_bytes;
    unsigned char *base = reinterpret_cast<unsigned char *>(setssip.data());

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

private:
  unsigned int m_num_harts;      // configured hart count
  std::vector<uint32_t> setssip; // per-hart supervisor SW interrupt bits

  // Validate byte enables: all lanes must be fully enabled (0xFF)
  bool validate_byte_enable(tlm::tlm_generic_payload &trans, unsigned int len) {
    if (trans.get_byte_enable_ptr() == nullptr)
      return true;
    unsigned char *be = trans.get_byte_enable_ptr();
    unsigned int be_len = trans.get_byte_enable_length();
    for (unsigned int i = 0; i < len; ++i) {
      if (be[i % be_len] != BE_FULL) {
        trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        return false;
      }
    }
    return true;
  }

  // Drive interrupt output for each hart based on setssip bit[0]
  void update_irqs() {
    for (unsigned int i = 0; i < m_num_harts; ++i) {
      ssw_irq[i].write((setssip[i] & SETSSIP_MASK) != 0);
    }
  }
};
#endif