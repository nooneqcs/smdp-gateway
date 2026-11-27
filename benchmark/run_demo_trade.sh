#!/usr/bin/env bash
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
python3 benchmark/gen_ticks.py benchmark/ticks_simnow.bin
cd benchmark
sudo ./wire_tx --no-pci --vdev=net_pcap0,tx_pcap=mirp_out.pcap -- ticks_simnow.bin
SRC=$(ls ../src/*.cpp | grep -v /market.cpp)
g++ -O2 -march=native -std=c++17 -pthread -I../src -I. $(pkg-config --cflags libdpdk) demo_trade4.cpp $SRC -o demo_trade4 $(pkg-config --libs libdpdk)
sudo ./demo_trade4 --no-pci --vdev=net_pcap0,rx_pcap=mirp_out.pcap --
