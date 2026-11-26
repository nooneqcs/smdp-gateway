.PHONY: demo-pcap

demo-pcap:
	python3 benchmark/gen_ticks.py benchmark/ticks_simnow.bin
	cd benchmark && sudo ./wire_tx --no-pci --vdev=net_pcap0,tx_pcap=mirp_out.pcap -- ticks_simnow.bin
	cd benchmark && sudo ./pcap_rx_verify2 --no-pci --vdev=net_pcap0,rx_pcap=mirp_out.pcap --
