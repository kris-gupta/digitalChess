# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**dgt2** — bridges a DGT digital chess board to Lichess online play, running on an ESP32 (esp32dev, 4 MB flash). Built with PlatformIO + ESP-IDF 6.0.1, compiled by `xtensa-esp32-elf-gcc` in debug mode (`-Og -g2`).

## Build / Flash / Monitor

```sh
pio run                       # build
pio run -t upload             # flash via /dev/ttyACM1
pio run -t monitor            # serial console (115200 baud)
pio run -t upload -t monitor  # flash then monitor
```

## Formatting

```sh
clang-format -i src/*.c include/*.h
```

Style: LLVM base, `IndentWidth: 4`, `IndentCaseLabels: true`. No `.clang-format` file is checked in — relies on a global clang-format config.

## LSP setup (clangd)

`compile_commands.json` in the project root is a symlink to `.pio/build/esp32dev/compile_commands.json`. After a fresh clone it is dangling until the first `pio run`. `.clangd` strips Xtensa-specific flags (`-mlongcalls`, `-fno-*`) that clangd doesn't understand.

## Architecture

Entry point is `app_main()` in `src/main.c`. Startup sequence:

1. NVS flash init
2. `wifi_init_sta()` — blocking STA connect, retries up to 10 times
3. SNTP sync — polls `pool.ntp.org`, waits up to 30 s for a valid year ≥ 2024
4. HTTPS GET `/api/account` → parse player ID from JSON response
5. POST `/api/board/seek` — create a game seek (see streaming caveat below)
6. POST `/api/board/game/{id}/move/{move}` — submit a move

**Modules:**

- `chess.h` — types only: `Piece`, `Move`, `Player`, `Game`, `Event`, `PendingMove`, enums
- `wifi.{h,c}` — WiFi STA init
- `http.{h,c}` — `esp_http_client` wrapper; Lichess host/path/token constants; event handler that accumulates response into a caller-supplied buffer
- `json.{h,c}` — thin wrapper around jsmn: `json_parse`, `json_get_value`, `json_free`
- `include/jsmn.h` — header-only JSON tokeniser; only `json.c` defines `JSMN_STATIC` to avoid duplicate symbols

`src/CMakeLists.txt` uses `FILE(GLOB_RECURSE)` — every `.c` under `src/` is compiled automatically; no manual registration needed when adding files.

## HTTP event handler

`_http_event_handler` in `http.c` uses `static` local variables (`output_buffer`, `output_len`). This is intentional but means it is **not reentrant** — only one HTTP connection may be active at a time. The caller-supplied `user_data` buffer (4 KB, `MAX_HTTP_OUTPUT_BUFFER`) is where responses land; responses larger than 4 KB are silently truncated.

## Move encoding

`struct Move` uses a packed nibble format: `from`/`to` are `uint8_t` where the high nibble is the file (0–7, converted to `a–h` by `+ 97`) and the low nibble is the rank (0–7, converted to `1–8` by `+ 48`).

## Credentials

All credentials live in `src/secrets.h` (gitignored). Copy `src/secrets.h.example` to `src/secrets.h` and fill in your values before building:

```sh
cp src/secrets.h.example src/secrets.h
```

`src/wifi.h` and `src/http.h` include `secrets.h` and reference `WIFI_SSID`, `WIFI_PASSKEY`, and `LICHESS_TOKEN` from it.

## Constraints

- The ESP32 (original) only supports 2.4 GHz WiFi — point `WIFI_SSID` at your 2.4 GHz network.
- `POST /api/board/seek` is a Lichess streaming endpoint (ndjson). It holds the connection open until a game starts. The current `create_seek()` uses `esp_http_client_perform()` which will time out on long waits; the proper fix is `esp_http_client_open()` + `esp_http_client_read()` in a loop with `timeout_ms = 0` (infinite).
- The seek is hardcoded to `rated=true&variant=standard&time=15&increment=15&color=white` in `create_seek()`.
- No test framework is configured.
- `sdkconfig.esp32dev` is the active ESP-IDF SDK config; do not hand-edit unless you understand ESP-IDF Kconfig.
