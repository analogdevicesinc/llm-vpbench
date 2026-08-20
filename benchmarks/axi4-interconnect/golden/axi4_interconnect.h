#ifndef AXI4_INTERCONNECT_H
#define AXI4_INTERCONNECT_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <vector>

class axi4_interconnect : public sc_core::sc_module {
public:
    sc_core::sc_vector<tlm_utils::simple_target_socket_tagged<axi4_interconnect>> master_socket;
    sc_core::sc_vector<tlm_utils::simple_initiator_socket_tagged<axi4_interconnect>> slave_socket;

    axi4_interconnect(sc_core::sc_module_name name,
                      unsigned int num_masters = 2,
                      unsigned int num_slaves = 2)
        : sc_core::sc_module(name)
        , master_socket("master_socket", num_masters)
        , slave_socket("slave_socket", num_slaves)
        , NUM_MASTERS(num_masters)
        , NUM_SLAVES(num_slaves)
    {
        // Build default address map: 256MB per slave
        for (unsigned int i = 0; i < NUM_SLAVES; i++) {
            address_region r;
            r.base_addr = (uint64_t)i * 0x10000000ULL;
            r.size = 0x10000000ULL;
            r.slave_index = i;
            regions.push_back(r);
        }

        // Allocate per-slave mutex and last_served
        for (unsigned int i = 0; i < NUM_SLAVES; i++) {
            std::string mname = std::string("slave_mutex_") + std::to_string(i);
            slave_mutex.push_back(new sc_core::sc_mutex(mname.c_str()));
        }
        last_served.resize(NUM_SLAVES, (int)NUM_MASTERS - 1);

        // Register callbacks on target sockets (facing masters)
        for (unsigned int i = 0; i < NUM_MASTERS; i++) {
            master_socket[i].register_b_transport(this, &axi4_interconnect::b_transport, i);
            master_socket[i].register_get_direct_mem_ptr(this, &axi4_interconnect::get_direct_mem_ptr, i);
            master_socket[i].register_transport_dbg(this, &axi4_interconnect::transport_dbg, i);
        }

        // Register callbacks on initiator sockets (facing slaves)
        for (unsigned int i = 0; i < NUM_SLAVES; i++) {
            slave_socket[i].register_invalidate_direct_mem_ptr(this, &axi4_interconnect::invalidate_direct_mem_ptr, i);
        }
    }

private:
    struct address_region {
        uint64_t base_addr;
        uint64_t size;
        unsigned int slave_index;
    };

    unsigned int NUM_MASTERS;
    unsigned int NUM_SLAVES;
    std::vector<address_region> regions;
    std::vector<sc_core::sc_mutex*> slave_mutex;
    std::vector<int> last_served;

    int decode(uint64_t addr) {
        for (size_t i = 0; i < regions.size(); i++) {
            if (addr >= regions[i].base_addr && addr < regions[i].base_addr + regions[i].size)
                return (int)regions[i].slave_index;
        }
        return -1;
    }

    uint64_t translate(uint64_t addr, int slave_id) {
        for (size_t i = 0; i < regions.size(); i++) {
            if ((int)regions[i].slave_index == slave_id &&
                addr >= regions[i].base_addr && addr < regions[i].base_addr + regions[i].size)
                return addr - regions[i].base_addr;
        }
        return addr;
    }

    const address_region* find_region(uint64_t addr) {
        for (size_t i = 0; i < regions.size(); i++) {
            if (addr >= regions[i].base_addr && addr < regions[i].base_addr + regions[i].size)
                return &regions[i];
        }
        return nullptr;
    }

    // b_transport callback
    void b_transport(int id, tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        uint64_t orig_addr = trans.get_address();
        int slave_id = decode(orig_addr);

        if (slave_id < 0) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        trans.set_address(translate(orig_addr, slave_id));
        slave_mutex[slave_id]->lock();
        last_served[slave_id] = id;
        slave_socket[slave_id]->b_transport(trans, delay);
        slave_mutex[slave_id]->unlock();
        trans.set_address(orig_addr);
    }

    // get_direct_mem_ptr callback
    bool get_direct_mem_ptr(int id, tlm::tlm_generic_payload& trans, tlm::tlm_dmi& dmi_data) {
        uint64_t orig_addr = trans.get_address();
        const address_region* reg = find_region(orig_addr);

        if (!reg) return false;

        int slave_id = (int)reg->slave_index;
        trans.set_address(orig_addr - reg->base_addr);
        bool ok = slave_socket[slave_id]->get_direct_mem_ptr(trans, dmi_data);
        trans.set_address(orig_addr);

        if (ok) {
            dmi_data.set_start_address(dmi_data.get_start_address() + reg->base_addr);
            dmi_data.set_end_address(dmi_data.get_end_address() + reg->base_addr);
        }
        return ok;
    }

    // transport_dbg callback
    unsigned int transport_dbg(int id, tlm::tlm_generic_payload& trans) {
        uint64_t orig_addr = trans.get_address();
        int slave_id = decode(orig_addr);

        if (slave_id < 0) return 0;

        trans.set_address(translate(orig_addr, slave_id));
        unsigned int n = slave_socket[slave_id]->transport_dbg(trans);
        trans.set_address(orig_addr);
        return n;
    }

    // invalidate_direct_mem_ptr callback (from slave)
    void invalidate_direct_mem_ptr(int id, sc_dt::uint64 start, sc_dt::uint64 end) {
        // id is the slave port index; translate to global
        uint64_t base = 0;
        for (size_t i = 0; i < regions.size(); i++) {
            if ((int)regions[i].slave_index == id) {
                base = regions[i].base_addr;
                break;
            }
        }
        sc_dt::uint64 global_start = start + base;
        sc_dt::uint64 global_end = end + base;

        // Broadcast to all masters
        for (unsigned int m = 0; m < NUM_MASTERS; m++) {
            master_socket[m]->invalidate_direct_mem_ptr(global_start, global_end);
        }
    }

};

#endif // AXI4_INTERCONNECT_H
