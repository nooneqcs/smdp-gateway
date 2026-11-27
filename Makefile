.PHONY: demo-pcap trade-core demo-trade

demo-pcap:
	python3 benchmark/gen_ticks.py benchmark/ticks_simnow.bin
	cd benchmark && sudo ./wire_tx --no-pci --vdev=net_pcap0,tx_pcap=mirp_out.pcap -- ticks_simnow.bin
	cd benchmark && sudo ./pcap_rx_verify2 --no-pci --vdev=net_pcap0,rx_pcap=mirp_out.pcap --

trade-core:
	g++ -O2 -std=c++17 benchmark/trade_core.cpp -o benchmark/trade_core
	./benchmark/trade_core 200000

demo-trade:
	bash benchmark/run_demo_trade.sh
