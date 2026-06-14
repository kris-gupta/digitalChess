# AGENTS.md — dgt2

## Project

- **PlatformIO + ESP-IDF 6.0.1**, board `esp32dev` (4 MB flash), C (`xtensa-esp32-elf-gcc`), debug build (`-Og -g2`)
- **Purpose**: DGT digital chess board → Lichess online play
- **Single entrypoint**: `src/main.c` → `app_main()`
  - NVS flash init → blocking WiFi STA (retries up to 10) → HTTPS GET `/api/account` → parse `id` via jsmn → POST `/api/board/seek` → POST `/api/board/game/{id}/move/{move}`

## Build / Flash / Monitor

```
pio run                          # build (also regenerates compile_commands.json)
pio run -t upload                # flash via /dev/ttyACM1
pio run -t monitor               # serial console (115200 baud, raw mode)
pio run -t upload -t monitor     # flash then monitor
```

- `.pio/` is build output (gitignored)

## Modules

| File(s) | Role |
|---|---|
| `src/chess.h` | Types: `Piece`, `Move`, `Player`, `Game`, `Event`, `PendingMove`, enums |
| `src/wifi.{h,c}` | Blocking STA init (retry up to 10, hard timeout via event group) |
| `src/http.{h,c}` | `esp_http_client` wrapper; Lichess host/path/token constants; event handler accumulates response into caller buffer |
| `src/json.{h,c}` | jsmn wrapper: `json_parse`, `json_get_value`, `json_free` |
| `include/jsmn.h` | Header-only tokeniser; only `json.c` defines `JSMN_STATIC` to avoid duplicate symbols |

- `src/CMakeLists.txt` uses `FILE(GLOB_RECURSE)` — every `.c` under `src/` is auto-compiled
- `.clangd` strips ESP-IDF flags (`-mlongcalls`, `-fno-*`) for clangd compatibility (no effect on build)

## Secrets

Credentials live in `src/secrets.h` (gitignored). Template at `src/secrets.h.example`:

```c
#define WIFI_SSID     "your_wifi_ssid"
#define WIFI_PASSKEY  "your_wifi_password"
#define LICHESS_TOKEN "your_lichess_api_token"
```

Both `src/wifi.h` and `src/http.h` include `secrets.h`. The token is sent as `Authorization: Bearer <LICHESS_TOKEN>`. **Do not commit real credentials.** `.gitignore` covers `.pio/`, `src/secrets.h`, and `sdkconfig.esp32dev.old`.

## Move encoding

`struct Move` uses packed nibble format: `from`/`to` are `uint8_t` where the high nibble is file (0–7, mapped to `a–h` via `+ 97`) and the low nibble is rank (0–7, mapped to `1–8` via `+ 48`).

## Formatting

```
clang-format -i src/*.c include/*.h
```

Style: LLVM base, `IndentWidth: 4`, `IndentCaseLabels: true`. No `.clang-format` file in this repo — relies on defaults or a global config.

## Constraints

- No test framework configured; `test/` directory contains only a PlatformIO README.
- `sdkconfig.esp32dev` is the ESP-IDF SDK config; do not hand-edit unless you understand ESP-IDF Kconfig.
- `compile_commands.json` is stale after builds — regenerate via `pio run`.
