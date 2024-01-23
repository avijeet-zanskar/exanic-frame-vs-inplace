#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <exanic/exanic.h>
#include <exanic/fifo_rx.h>

#include <x86intrin.h>

#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include "util.hpp"

struct StreamHeader {
    uint16_t msg_len;
    uint16_t stream_id;
    uint32_t seq_no;
} __attribute__((packed));

struct Frame {
    ether_header eh;
    iphdr ih;
    udphdr uh;
    StreamHeader stream_header;
} __attribute__((packed));

constexpr bool print_stream_stats = false;

int main() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(16, &cpuset);
    int cpu_set_err = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    if (cpu_set_err != 0) {
        std::cerr << "Failed to set CPU affinity\n";
        return EXIT_FAILURE;
    }

    const char* device = "exanic0";
    int stream_one_buffer = 1, stream_two_buffer = 2;
    int stream_one_port = 0, stream_two_port = 0;

    exanic_t* exanic = exanic_acquire_handle(device);
    if (!exanic) {
        std::cerr << "exanic_acquire_handle error: " << exanic_get_last_error() << '\n';
        return EXIT_FAILURE;
    }

    exanic_rx_t* stream_one_rx = exanic_acquire_rx_buffer(exanic, stream_one_port, stream_one_buffer);
    if (!stream_one_rx) {
        std::cerr << "exanic_acquire_rx_buffer: " << exanic_get_last_error() << '\n';
    }
    exanic_rx_t* stream_two_rx = exanic_acquire_rx_buffer(exanic, stream_two_port, stream_two_buffer);
    if (!stream_two_rx) {
        std::cerr << "exanic_acquire_rx_buffer: " << exanic_get_last_error() << '\n';
    }

    Frame *stream_one_ptr;
    int64_t count = 10000000;
    std::vector<Stats> stats;
    stats.reserve(count);
    volatile char c;
    rx_chunk_info rx_info;
    int64_t index = 0;

    while (count) {
        ssize_t sz = 0;
        uint32_t chunk_id;
        int more_chunks;
        sz = exanic_receive_chunk_inplace_ex(stream_one_rx, reinterpret_cast<char**>(&stream_one_ptr), &chunk_id, &more_chunks, &rx_info);
        c = reinterpret_cast<char*>(stream_one_ptr)[0];
        if constexpr (print_stream_stats) {
            std::cout << "Message length: " << stream_one_ptr[0].stream_header.msg_len << '\n';
            std::cout << "Stream ID: " << stream_one_ptr[0].stream_header.stream_id << '\n';
            std::cout << "Sequence No: " << stream_one_ptr[0].stream_header.seq_no << '\n';
        }

        if (sz > 0) {
            --count;
            stats.push_back(Stats{index, sz, exanic_cycles_to_ns(exanic, exanic_expand_timestamp(exanic, rx_info.timestamp)), std::chrono::system_clock::now().time_since_epoch().count(), stream_one_ptr[0].stream_header.stream_id, stream_one_ptr[0].stream_header.seq_no});
        }
        ++index;
    }
    std::cout << "exanic_drop_early\n";
    dump_csv(stats, "inplace.csv");
}