# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**dgt2** — bridges a DGT digital chess board to a Lichess broadcast, running on an ESP32 (esp32dev, 4 MB flash). Moves are read from the physical board and pushed live as PGN to a Lichess broadcast round for spectators to follow. Built with PlatformIO + ESP-IDF 6.0.1, compiled by `xtensa-esp32-elf-gcc` in debug mode (`-Og -g2`).

## Build / Flash / Monitor

```sh
pio run                       # build (also regenerates compile_commands.json)
pio run -t upload             # flash via /dev/ttyACM1
pio run -t monitor            # serial console (115200 baud, raw mode)
pio run -t upload -t monitor  # flash then monitor
```

## Formatting

```sh
clang-format -i src/*.c include/*.h
```

Style: LLVM defaults (`IndentWidth: 2`). No `.clang-format` file is checked in and none was found on this machine — `clang-format` silently falls back to its built-in LLVM style, so `clang-format --style=file --dump-config` is the source of truth if this ever needs to be verified again.

## LSP setup (clangd)

`compile_commands.json` in the project root is a symlink to `.pio/build/esp32dev/compile_commands.json`. After a fresh clone it is dangling until the first `pio run`. `.clangd` strips Xtensa-specific flags (`-mlongcalls`, `-fno-*`) that clangd doesn't understand.

## Architecture

Entry point is `app_main()` in `src/main.c`. Startup sequence:

1. NVS flash init
2. `wifi_init_sta()` — blocking STA connect, retries up to 10 times
3. SNTP sync — polls `pool.ntp.org`, waits up to 30 s for a valid year ≥ 2024 (TLS cert validation needs a real clock)
4. Allocate `response_buffer` (`MAX_HTTP_RECV_BUFFER`, 4 KB) and `pgn_buf` (`PGN_BUF_SIZE`, 4 KB); init the HTTP client and `board_init()` the in-memory `Game`
5. `create_broadcast()` — `POST /api/broadcast/new` then `POST /api/broadcast/{tour}/new` to create a Lichess broadcast tournament + round; logs the spectator URL (`https://lichess.org/broadcast/-/{round_id}`)
6. Game loop, forever:
   - `scanb()` blocks until a physical move is detected (currently a stub — see below)
   - `move_to_san()` renders SAN *before* the move is applied (needs the piece still at `from`)
   - `board_apply_move()` updates the in-memory board (captures, en passant, castling, promotion) and reports via `skip_next` whether the next `scanb()` result is a rook's castling move to be silently consumed
   - the move is appended to `pgn_buf`, then pushed whole to the broadcast round with `push_pgn()` (`POST /api/broadcast/round/{round}/push`); each push re-sends the full PGN with a trailing `*` (ongoing-game marker)

**Modules:**

- `chess.h` — types only: `Piece`, `Move` (packed nibble squares), `Player`, `Game` (`Pieces[32]` + `board[8][8]` index grid), `Event`, `PendingMove`, enums (`Piece_type`, `Colour`, `Event_type`)
- `board.{h,c}` — board state and move semantics: `board_init()` sets the standard start position; `board_apply_move()` mutates `Game` for captures/en passant/castling/promotion (promotion is always to queen); `move_to_san()`/`move_to_uci()` render a move to text; `scanb()` is a stub (`TODO: implement hall-effect sensor scanning`) that must block until a physical move is read off the board
- `wifi.{h,c}` — blocking WiFi STA init
- `http.{h,c}` — `esp_http_client` wrapper (`http_init`, `http_get`, `http_post`); Lichess host/path/token constants; `http_event_handler` accumulates the response into a caller-supplied buffer
- `json.{h,c}` — thin wrapper around jsmn: `json_parse`, `json_get_value`, `json_free`
- `include/jsmn.h` — header-only JSON tokeniser; only `json.c` defines `JSMN_STATIC` to avoid duplicate symbols

`src/CMakeLists.txt` uses `FILE(GLOB_RECURSE)` — every `.c` under `src/` is compiled automatically; no manual registration needed when adding files.

## HTTP event handler

`http_event_handler` in `http.c` uses `static` local variables (`output_buffer`, `output_len`). This is intentional but means it is **not reentrant** — only one HTTP connection may be active at a time. The caller-supplied `user_data` buffer (`response_buffer`, 4 KB, `MAX_HTTP_RECV_BUFFER`/`MAX_HTTP_OUTPUT_BUFFER`) is where responses land; responses larger than 4 KB are silently truncated.

## Move encoding

`struct Move` uses a packed nibble format: `from`/`to` are `uint8_t` where the high nibble is the file (0–7, converted to `a–h` by `+ 97`/`'a'`) and the low nibble is the rank (0–7, converted to `1–8` by `+ 48`/`'1'`). `board.c`'s `game->board[file][rank]` grid holds an `int8_t` index into `Pieces[32]` (`-1` = empty), used to look up piece type/colour when applying moves or rendering SAN.

## Credentials

All credentials live in `src/secrets.h` (gitignored). Copy `src/secrets.h.example` to `src/secrets.h` and fill in your values before building:

```sh
cp src/secrets.h.example src/secrets.h
```

`src/wifi.h` and `src/http.h` include `secrets.h` and reference `WIFI_SSID`, `WIFI_PASSKEY`, and `LICHESS_TOKEN` from it. The token is sent as `Authorization: Bearer <LICHESS_TOKEN>` (`BEARER_TOKEN` in `http.h`).

## Constraints

- The ESP32 (original) only supports 2.4 GHz WiFi — point `WIFI_SSID` at your 2.4 GHz network.
- `scanb()` in `board.c` is unimplemented (returns a zeroed `Move` immediately) — the game loop currently has no real input source until hall-effect sensor scanning is written.
- `move_to_san()` does not append check (`+`)/checkmate (`#`) markers and does not disambiguate two same-type pieces that can reach the same square.
- Each `push_pgn()` call re-sends the *entire* PGN so far (Lichess broadcast round push replaces prior state, it doesn't append).
- No test framework is configured (`test/` contains only the PlatformIO placeholder README).
- `sdkconfig.esp32dev` is the active ESP-IDF SDK config; do not hand-edit unless you understand ESP-IDF Kconfig.
