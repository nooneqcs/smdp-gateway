#!/usr/bin/env bash
# stage.sh <rate> <limit> [dmac]   limit<=0 表示只起收端、不发包
set -u
RATE=${1:-0}; LIM=${2:-0}; DMAC=${3:-}
cd "$(dirname "$0")"
sudo pkill -f dpdk_mirp_v10 2>/dev/null; sleep 1
rm -f /tmp/rx.log
sudo ./dpdk_mirp_v10 -l 1-3 -n 4 -a 0000:01:00.0 -- 1000000 > /tmp/rx.log 2>&1 &
sleep 3
grep "main lcore" /tmp/rx.log
if [ "$LIM" -gt 0 ]; then
  echo "--- send rate=$RATE limit=$LIM dmac=${DMAC:-broadcast} ---"
  if [ -n "$DMAC" ]; then
    sudo ./bin2wire ../ticks_multi_20261127_8060.bin --dev=enp2s0 --dmac=$DMAC --rate=$RATE --limit=$LIM
  else
    sudo ./bin2wire ../ticks_multi_20261127_8060.bin --dev=enp2s0 --rate=$RATE --limit=$LIM
  fi
  sleep 3
fi
echo "--- result ---"
grep -E "mon|first" /tmp/rx.log | tail -n 3
sudo pkill -f dpdk_mirp_v10 2>/dev/null
