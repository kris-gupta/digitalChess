# AGENTS.md — dgt2

## Project

- **PlatformIO + ESP-IDF** (6.0.1), board `esp32dev` (4 MB flash), C (`xtensa-esp32-elf-gcc`), debug build (`-Og -g2`)
- **Purpose**: DGT digital chess board → Lichess online play
- **Single entrypoint**: `src/main.c` → `app_main()` — NVS → WiFi STA → HTTPS GET `/api/account` with Bearer token → JSON parsed via `jsmn`
- **Modules** under `src/`:
  - `chess.h` — chess types (`Piece`, `Move`, `Player`, `Game`, enums)
  - `wifi.h` / `wifi.c` — WiFi STA init (blocking, retries up to 10)
  - `http.h` / `http.c` — HTTP(S) client, Lichess API config & event handler
  - `json.h` / `json.c` — jsmn wrapper (`json_parse`, `json_get_value`, `json_free`)
  - `main.c` — app lifecycle orchestration
- `src/CMakeLists.txt` uses `FILE(GLOB_RECURSE ...)` — every `.c` under `src/` is auto-compiled
- `include/jsmn.h` — header-only library with function bodies; only `json.c` defines `JSMN_STATIC` to avoid duplicate symbols
- `compile_commands.json` is stale (generated for prior build); regenerate via `pio run`
- `.clangd` strips certain ESP-IDF flags (`-mlongcalls`, `-fno-*` etc.) for clangd compatibility

## Build / Flash / Monitor

```
pio run                          # build
pio run -t upload                # flash via /dev/ttyACM1
pio run -t monitor               # serial console (115200 baud, raw mode)
pio run -t upload -t monitor     # flash then monitor
```

- `.pio/` is build output (gitignored)

## Formatting

```
clang-format -i src/*.c include/*.h
```

- LLVM base style, `IndentWidth: 4`, `IndentCaseLabels: true` — see `.clang-format`

## Constraints

- WiFi SSID/passkey (`src/wifi.h`) and Lichess Bearer token (`src/http.h`) are hardcoded — **do not commit real credentials**
- `.gitignore` only covers `.pio/` — everything else (`sdkconfig.*`, credentials, etc.) will be tracked unless explicitly ignored
- `sdkconfig.esp32dev` — ESP-IDF SDK config; `sdkconfig.esp32dev.old` is a stale backup; do not hand-edit unless you understand ESP-IDF Kconfig
- No tests exist; no test framework configured
