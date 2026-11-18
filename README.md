# mrq

Stores Binance 1-minute candles in SQLite and serves them over a simple TCP protocol: send one line of JSON, get one line of JSON back.

## Build

Requires a C++23 compiler (Xcode 15+ Command Line Tools on macOS), CMake 3.20+, and SQLite (included with macOS). The only third-party code is `third_party/nlohmann/json.hpp`.

```bash
cmake -S . -B build && cmake --build build -j
```

Run all commands from the repo root.

## Get data

```bash
scripts/fetch.sh BTCUSDT 3
```

Downloads the last 3 days of BTCUSDT 1-minute candles into `data/`.

## Run the server

```bash
./build/mrq serve
```

Loads every CSV in `data/` into `candles.db`, then listens on `127.0.0.1:8080`. To add data, put new CSVs in `data/` and restart.

## Sending requests

Every request is a JSON object with a `"cmd"` field. Send one from a file or from stdin:

```bash
./build/mrq send examples/sma.json
echo '{"cmd":"symbols"}' | ./build/mrq send -
```

Times are epoch seconds (UTC). Ranges are `[from, to)`. Intervals: `1m 5m 15m 1h 4h 1d` (default `1m`). An optional `"id"` (string or integer) is echoed back in the response. The examples use data for 2026-11-14 to 2026-11-16. `fetch.sh` downloads the most recent days, so if you fetched on a different date, run `symbols` to see the loaded range and edit the `from`/`to` values in `examples/*.json` to fall inside it. To convert a UTC date to epoch seconds:

```bash
date -j -u -f "%Y-%m-%d %H:%M" "2026-11-16 00:00" +%s   # macOS
date -u -d "2026-11-16 00:00" +%s                       # Linux
```

### symbols

```bash
./build/mrq send examples/symbols.json
```

Request:

```json
{"cmd":"symbols"}
```

Response:

```json
{"ok":true,"symbols":[{"symbol":"BTCUSDT","from":1794614400,"to":1794873600,"rows":4320}]}
```

### candles

```bash
./build/mrq send examples/candles.json
```

Request:

```json
{"cmd":"candles","id":"example-candles","symbol":"BTCUSDT","from":1794787200,"to":1794794400,"interval":"1h"}
```

Response:

```json
{"ok":true,"id":"example-candles","symbol":"BTCUSDT","interval":"1h","from":1794787200,"to":1794794400,"columns":["ts","open","high","low","close","volume","trades"],"rows":[[1794787200,91234.5,91410.0,91102.3,91388.1,412.37,18234],[1794790800,91388.1,91520.7,91290.0,91301.4,380.02,16901]]}
```

### sma

```bash
./build/mrq send examples/sma.json
```

Request:

```json
{"cmd":"sma","id":"example-1","symbol":"BTCUSDT","from":1794787200,"to":1794873600,"window":20,"interval":"1h"}
```

Response:

```json
{"ok":true,"id":"example-1","symbol":"BTCUSDT","interval":"1h","from":1794787200,"to":1794873600,"window":20,"columns":["ts","close","sma"],"rows":[[1794787200,91388.1,91102.44],[1794790800,91301.4,91120.9]]}
```

### price

Returns the last 1-minute candle that had completed at or before `at` (never the candle still forming at `at`).

```bash
./build/mrq send examples/price.json
```

Request:

```json
{"cmd":"price","id":"example-price","symbol":"BTCUSDT","at":1794790830}
```

Response:

```json
{"ok":true,"id":"example-price","symbol":"BTCUSDT","at":1794790830,"candle":{"ts":1794790740,"open":91290.0,"high":91320.5,"low":91280.1,"close":91301.4,"volume":12.3,"trades":842}}
```

### Without the client

```bash
echo '{"cmd":"symbols"}' | nc 127.0.0.1 8080
```

Errors look like `{"ok":false,"code":"invalid_argument","error":"..."}`.
