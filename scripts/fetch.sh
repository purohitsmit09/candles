#!/usr/bin/env bash
# Usage: scripts/fetch.sh [SYMBOL] [DAYS]     defaults: BTCUSDT 3
set -euo pipefail

SYMBOL="${1:-BTCUSDT}"
DAYS="${2:-3}"

mkdir -p data

for i in $(seq 1 "$DAYS"); do
  if [ "$(uname)" = "Darwin" ]; then
    date=$(date -u -v-${i}d +%F)
  else
    date=$(date -u -d "-${i} day" +%F)
  fi
  name="${SYMBOL}-1m-${date}"
  if [ -e "data/${name}.csv" ]; then
    echo "skip ${name}.csv (exists)"
    continue
  fi
  tmpzip=$(mktemp)
  if ! curl -fsSL -o "$tmpzip" "https://data.binance.vision/data/spot/daily/klines/${SYMBOL}/1m/${name}.zip"; then
    echo "warning: ${date} not available"
    rm -f "$tmpzip"
    continue
  fi
  unzip -p "$tmpzip" > "data/${name}.csv.part"
  mv "data/${name}.csv.part" "data/${name}.csv"
  rm -f "$tmpzip"
  echo "fetched ${name}.csv"
done
