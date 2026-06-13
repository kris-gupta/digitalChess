# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**dgt2** — bridges a DGT digital chess board to Lichess online play, running on an ESP32 (esp32dev, 4 MB flash). Built with PlatformIO + ESP-IDF 6.0.1, compiled by `xtensa-esp32-elf-gcc` in debug mode (`-Og -g2`).

## Build / Flash / Monitor

```sh
pio run                       # build
pio run -t upload             # flash via /dev/ttyACM0
pio run -t monitor            # serial console (115200 baud)
pio run -t upload -t monitor  # flash then monitor
```

`compile_commands.json` is stale after each build — regenerate with `pio run`.

## Formatting

```sh
clang-format -i src/*.c include/*.h
```

Style: LLVM base, `IndentWidth: 4`, `IndentCaseLabels: true` (see `.clang-format`).

## Architecture

Entry point is `app_main()` in `src/main.c`. Startup sequence:

1. NVS flash init
2. `wifi_init_sta()` — blocking STA connect, retries up to 10 times
3. HTTPS GET `/api/account` → parse player ID from JSON response
4. POST `/api/board/seek` — create a game seek
5. POST `/api/board/game/{id}/move/{move}` — submit a move

**Modules:**

- `chess.h` — types only: `Piece`, `Move`, `Player`, `Game`, `Event`, `PendingMove`, enums
- `wifi.{h,c}` — WiFi STA init
- `http.{h,c}` — `esp_http_client` wrapper; Lichess host/path/token constants; event handler that accumulates response into a caller-supplied buffer
- `json.{h,c}` — thin wrapper around jsmn: `json_parse`, `json_get_value`, `json_free`
- `include/jsmn.h` — header-only JSON tokeniser; only `json.c` defines `JSMN_STATIC` to avoid duplicate symbols

`src/CMakeLists.txt` uses `FILE(GLOB_RECURSE)` — every `.c` under `src/` is compiled automatically; no manual registration needed when adding files.

## Move encoding

`struct Move` uses a packed nibble format: `from`/`to` are `uint8_t` where the high nibble is the file (0–7, converted to `a–h` by `+ 97`) and the low nibble is the rank (0–7, converted to `1–8` by `+ 48`).

## Credentials

WiFi SSID/passkey are hardcoded in `src/wifi.h`; the Lichess Bearer token is hardcoded in `src/http.h`. Replace the placeholder `#####################` with a real token before flashing. Do not commit real credentials — `.gitignore` only covers `.pio/`.

## Constraints

- No test framework is configured.
- `sdkconfig.esp32dev` is the active ESP-IDF SDK config; do not hand-edit unless you understand ESP-IDF Kconfig.
- `.clangd` strips certain ESP-IDF flags (`-mlongcalls`, `-fno-*`) for clangd compatibility; this does not affect the actual build.
