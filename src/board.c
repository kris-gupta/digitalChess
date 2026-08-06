#include "board.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "ansi.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pins.h"

// Piece index layout: 0-7  = white back rank (a1..h1)
//                    8-15  = white pawns     (a2..h2)
//                    16-23 = black back rank (a8..h8)
//                    24-31 = black pawns     (a7..h7)
static const struct {
  enum Piece_type type;
  enum Colour colour;
  uint8_t file; // 0=a … 7=h
  uint8_t rank; // 0=rank1 … 7=rank8
} start_pos[32] = {
    {ROOK, WHITE, 0, 0},         // a1
    {KNIGHT, WHITE, 1, 0},       // b1
    {DARK_BISHOP, WHITE, 2, 0},  // c1 — dark square (file+rank even)
    {QUEEN, WHITE, 3, 0},        // d1
    {KING, WHITE, 4, 0},         // e1
    {LIGHT_BISHOP, WHITE, 5, 0}, // f1 — light square (file+rank odd)
    {KNIGHT, WHITE, 6, 0},       // g1
    {ROOK, WHITE, 7, 0},         // h1
    {PAWN, WHITE, 0, 1},         // a2
    {PAWN, WHITE, 1, 1},         // b2
    {PAWN, WHITE, 2, 1},         // c2
    {PAWN, WHITE, 3, 1},         // d2
    {PAWN, WHITE, 4, 1},         // e2
    {PAWN, WHITE, 5, 1},         // f2
    {PAWN, WHITE, 6, 1},         // g2
    {PAWN, WHITE, 7, 1},         // h2
    {ROOK, BLACK, 0, 7},         // a8
    {KNIGHT, BLACK, 1, 7},       // b8
    {LIGHT_BISHOP, BLACK, 2, 7}, // c8 — light square (file+rank odd)
    {QUEEN, BLACK, 3, 7},        // d8
    {KING, BLACK, 4, 7},         // e8
    {DARK_BISHOP, BLACK, 5, 7},  // f8 — dark square (file+rank even)
    {KNIGHT, BLACK, 6, 7},       // g8
    {ROOK, BLACK, 7, 7},         // h8
    {PAWN, BLACK, 0, 6},         // a7
    {PAWN, BLACK, 1, 6},         // b7
    {PAWN, BLACK, 2, 6},         // c7
    {PAWN, BLACK, 3, 6},         // d7
    {PAWN, BLACK, 4, 6},         // e7
    {PAWN, BLACK, 5, 6},         // f7
    {PAWN, BLACK, 6, 6},         // g7
    {PAWN, BLACK, 7, 6},         // h7
};

void board_init(struct Game *game) {
  memset(game, 0, sizeof(*game));
  memset(game->board, -1, sizeof(game->board));
  game->current_turn = WHITE;
  for (int i = 0; i < 32; i++) {
    game->Pieces[i].type = start_pos[i].type;
    game->Pieces[i].colour = start_pos[i].colour;
    game->Pieces[i].position.x = start_pos[i].file;
    game->Pieces[i].position.y = start_pos[i].rank;
    game->Pieces[i].is_dead = false;
    game->board[start_pos[i].file][start_pos[i].rank] = (int8_t)i;
  }
}

#ifdef DEMO_MODE

struct Move scanb(void) {
  // Scripted opening (Italian/Ruy Lopez lines, no castling) played back one
  // move every DEMO_MOVE_INTERVAL_MS, so the broadcast can be watched live
  // on lichess.org without needing the physical sensor board wired up.
  static const struct Move demo_moves[] = {
      {0x41, 0x43}, // e2e4
      {0x46, 0x44}, // e7e5
      {0x60, 0x52}, // g1f3
      {0x17, 0x25}, // b8c6
      {0x50, 0x14}, // f1b5
      {0x06, 0x05}, // a7a6
      {0x14, 0x03}, // b5a4
      {0x67, 0x55}, // g8f6
      {0x31, 0x32}, // d2d3
      {0x57, 0x24}, // f8c5
      {0x21, 0x22}, // c2c3
      {0x36, 0x35}, // d7d6
  };
  static const int num_demo_moves = sizeof(demo_moves) / sizeof(demo_moves[0]);
  static int idx = 0;

#define DEMO_MOVE_INTERVAL_MS 5000
  vTaskDelay(pdMS_TO_TICKS(DEMO_MOVE_INTERVAL_MS));

  if (idx >= num_demo_moves) {
    vTaskDelay(portMAX_DELAY); // sequence exhausted; stop pushing
  }

  return demo_moves[idx++];
}

#elif defined(MANUAL_MODE)

static bool console_ready = false;

// By default UART0 (shared with esp_log output) uses a simple polling
// read that doesn't yield to the scheduler while blocked — fine for logs,
// but blocking on it here while waiting for a typed move would starve the
// idle task and trip the watchdog (the same class of bug fixed in
// SCAN_PASS_DELAY_MS above). Switching to the interrupt-driven UART driver
// makes stdin's blocking read actually sleep the task instead of spinning.
static void manual_console_init(void) {
  uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
  uart_vfs_dev_use_driver(UART_NUM_0);
  uart_vfs_dev_port_set_rx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CR);
  uart_vfs_dev_port_set_tx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CRLF);
  console_ready = true;
}

// Parses one square like "e4" into the packed nibble format used by struct
// Move (high nibble = file 0-7, low nibble = rank 0-7). Case-insensitive.
static bool parse_square(char file_ch, char rank_ch, uint8_t *out) {
  char f = (char)tolower((unsigned char)file_ch);
  if (f < 'a' || f > 'h' || rank_ch < '1' || rank_ch > '8')
    return false;
  *out = (uint8_t)(((f - 'a') << 4) | (rank_ch - '1'));
  return true;
}

// Blocks on a line typed into the serial console, in the same 4-character
// UCI format move_to_uci() writes (e.g. "e2e4"), and reprompts on anything
// that doesn't parse as two valid squares.
struct Move scanb(void) {
  if (!console_ready)
    manual_console_init();

  char line[32];
  while (1) {
    printf("\n" ANSI_CYAN "move (UCI, e.g. e2e4): " ANSI_RESET);
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin))
      continue;

    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      line[--len] = '\0';

    struct Move m;
    if (len == 4 && parse_square(line[0], line[1], &m.from) &&
        parse_square(line[2], line[3], &m.to)) {
      printf(ANSI_CYAN "-> %.4s" ANSI_RESET "\n", line);
      return m;
    }
    printf(ANSI_CYAN "invalid move '%s' - expected 4 characters like e2e4" ANSI_RESET
                      "\n",
           line);
  }
}

#else // real hardware: hall-effect sensor matrix

// Consecutive agreeing full-board scans required before a detected lift or
// place is trusted, to reject noise right at a sensor's switching threshold.
#define DEBOUNCE_SCANS 3

// Delay between full-board scan passes while waiting for the next event.
// Must round up to at least one FreeRTOS tick (pdMS_TO_TICKS truncates) or
// vTaskDelay() becomes a no-op that never yields to the idle task, starving
// the task watchdog. At the default 100 Hz tick rate that floor is 10 ms.
#define SCAN_PASS_DELAY_MS 10

// Settle time after latching a column select before the row readings are
// trusted (shift-register output driver + hall sensor propagation delay).
#define ROW_SETTLE_US 5

static bool sensors_initialized = false;
static bool sensor_active[8][8]; // [rank][file], last-known occupancy

static void configure_output(gpio_num_t pin, int initial_level) {
  gpio_reset_pin(pin); // detach from any default IOMUX/peripheral function
  gpio_set_direction(pin, GPIO_MODE_OUTPUT);
  gpio_set_level(pin, initial_level);
}

static void sensors_init_gpio(void) {
  configure_output(PIN_SER, 0);
  configure_output(PIN_SRCLK, 0);
  configure_output(PIN_RCLK, 0);

  for (int r = 0; r < 8; r++) {
    gpio_reset_pin(ROW_PINS[r]);
    gpio_set_direction(ROW_PINS[r], GPIO_MODE_INPUT);
    gpio_set_pull_mode(ROW_PINS[r], GPIO_PULLUP_ONLY);
  }
}

// Loads an 8-bit "walking one" pattern (bit `file` set, rest clear) into
// every row's shift register simultaneously. Bit 0 (QA) is assumed wired to
// file a, bit 7 (QH) to file h — flip the shift direction below if your
// board's QA is wired to file h instead.
static void shift_out_column(uint8_t file) {
  uint8_t pattern = (uint8_t)(1u << file);
  gpio_set_level(PIN_RCLK, 0);
  for (int i = 7; i >= 0; i--) {
    gpio_set_level(PIN_SRCLK, 0);
    gpio_set_level(PIN_SER, (pattern >> i) & 1);
    gpio_set_level(PIN_SRCLK, 1);
  }
  gpio_set_level(PIN_RCLK, 1);
}

// Reads all 64 sensors: for each file, load the column-select pattern once
// (each row's shift register then powers only that row's sensor for this
// file), let the readings settle, then read all 8 rows back in parallel off
// their dedicated pins. 8 shift-register loads total instead of 64 per-row
// selects.
static void scan_board_raw(bool out[8][8]) {
  for (uint8_t file = 0; file < 8; file++) {
    shift_out_column(file);
    esp_rom_delay_us(ROW_SETTLE_US);
    for (uint8_t rank = 0; rank < 8; rank++)
      out[rank][file] = (gpio_get_level(ROW_PINS[rank]) == SENSOR_ACTIVE_LEVEL);
  }
}

// Counts squares where `now` disagrees with `known` in the direction of
// `want_active` (e.g. want_active=false finds squares that just went
// empty). Reports the last one found via *rank/*file.
static int count_transitions(const bool now[8][8], const bool known[8][8],
                             bool want_active, uint8_t *rank, uint8_t *file) {
  int count = 0;
  for (uint8_t r = 0; r < 8; r++) {
    for (uint8_t f = 0; f < 8; f++) {
      if (now[r][f] == want_active && known[r][f] != want_active) {
        *rank = r;
        *file = f;
        count++;
      }
    }
  }
  return count;
}

// Blocks until exactly one square transitions to `want_active` relative to
// `known`, and that reading is stable for DEBOUNCE_SCANS consecutive passes.
static void wait_for_single_transition(const bool known[8][8], bool want_active,
                                       uint8_t *out_rank, uint8_t *out_file) {
  uint8_t stable_rank = 0, stable_file = 0;
  int stable_count = 0;

  while (1) {
    bool now[8][8];
    scan_board_raw(now);

    uint8_t rank, file;
    int transitions = count_transitions(now, known, want_active, &rank, &file);

    if (transitions == 1 && rank == stable_rank && file == stable_file) {
      if (++stable_count >= DEBOUNCE_SCANS) {
        *out_rank = rank;
        *out_file = file;
        return;
      }
    } else if (transitions == 1) {
      stable_rank = rank;
      stable_file = file;
      stable_count = 1;
    } else {
      // No change yet, or an ambiguous multi-square difference (e.g. a
      // capture's removed piece plus the capturing piece's origin both
      // going inactive at once) — keep scanning until it resolves to a
      // single square.
      stable_count = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(SCAN_PASS_DELAY_MS));
  }
}

struct Move scanb(void) {
  if (!sensors_initialized) {
    sensors_init_gpio();
    // Standard starting position: ranks 1, 2, 7, 8 occupied.
    memset(sensor_active, 0, sizeof(sensor_active));
    for (uint8_t f = 0; f < 8; f++) {
      sensor_active[0][f] = true;
      sensor_active[1][f] = true;
      sensor_active[6][f] = true;
      sensor_active[7][f] = true;
    }
    sensors_initialized = true;
  }

  uint8_t from_rank, from_file, to_rank, to_file;
  wait_for_single_transition(sensor_active, false, &from_rank, &from_file);
  sensor_active[from_rank][from_file] = false;

  wait_for_single_transition(sensor_active, true, &to_rank, &to_file);
  sensor_active[to_rank][to_file] = true;

  struct Move m;
  m.from = (uint8_t)((from_file << 4) | from_rank);
  m.to = (uint8_t)((to_file << 4) | to_rank);
  return m;
}

#endif // DEMO_MODE

bool board_apply_move(struct Game *game, struct Move m, bool *skip_next) {
  uint8_t ff = m.from >> 4;
  uint8_t fr = m.from & 0x0F;
  uint8_t tf = m.to >> 4;
  uint8_t tr = m.to & 0x0F;

  *skip_next = false;

  int8_t pi = game->board[ff][fr];
  if (pi < 0)
    return false;

  struct Piece *p = &game->Pieces[pi];

  // Regular capture
  int8_t ti = game->board[tf][tr];
  if (ti >= 0) {
    game->Pieces[ti].is_dead = true;
    game->board[tf][tr] = -1;
  }

  // En passant: pawn moves diagonally onto an empty square.
  // The captured pawn sits at (to-file, from-rank).
  if (p->type == PAWN && ff != tf && ti < 0) {
    int8_t ep = game->board[tf][fr];
    if (ep >= 0) {
      game->Pieces[ep].is_dead = true;
      game->board[tf][fr] = -1;
    }
  }

  // Update piece position
  game->board[ff][fr] = -1;
  game->board[tf][tr] = pi;
  p->position.x = tf;
  p->position.y = tr;

  // Castling: king moves exactly two files.
  // Also move the rook on the internal board and signal the caller to
  // consume the rook's physical movement without submitting it.
  if (p->type == KING) {
    int diff = (int)tf - (int)ff;
    if (diff == 2 || diff == -2) {
      uint8_t rook_from = (diff == 2) ? 7 : 0;
      uint8_t rook_to = (diff == 2) ? 5 : 3;
      int8_t ri = game->board[rook_from][fr];
      if (ri >= 0) {
        game->board[rook_from][fr] = -1;
        game->board[rook_to][fr] = ri;
        game->Pieces[ri].position.x = rook_to;
      }
      *skip_next = true;
      return false;
    }
  }

  // Promotion: pawn reaches the far rank — always promote to queen.
  if (p->type == PAWN && (tr == 7 || tr == 0)) {
    p->type = QUEEN;
    return true;
  }

  return false;
}

// Piece letter for each Piece_type enum value:
// PAWN=0, QUEEN=1, KING=2, KNIGHT=3, DARK_BISHOP=4, LIGHT_BISHOP=5, ROOK=6
static const char piece_letter[] = {' ', 'Q', 'K', 'N', 'B', 'B', 'R'};

void move_to_san(const struct Game *game, struct Move m, char *buf) {
  uint8_t ff = m.from >> 4;
  uint8_t fr = m.from & 0x0F;
  uint8_t tf = m.to >> 4;
  uint8_t tr = m.to & 0x0F;

  int8_t pi = game->board[ff][fr];
  if (pi < 0) {
    buf[0] = '\0';
    return;
  }
  enum Piece_type pt = game->Pieces[pi].type;

  // Castling: king moves exactly two files
  if (pt == KING && ((int)tf - (int)ff == 2 || (int)tf - (int)ff == -2)) {
    if (tf > ff)
      strcpy(buf, "O-O");
    else
      strcpy(buf, "O-O-O");
    return;
  }

  bool is_capture = (game->board[tf][tr] >= 0);
  // En passant: pawn moves diagonally to an empty square
  bool is_ep = (pt == PAWN && ff != tf && !is_capture);
  if (is_ep)
    is_capture = true;
  bool is_promo = (pt == PAWN && (tr == 7 || tr == 0));

  int i = 0;
  if (pt != PAWN) {
    buf[i++] = piece_letter[pt];
  } else if (is_capture) {
    // Pawn capture: prefix with from-file
    buf[i++] = 'a' + ff;
  }

  if (is_capture)
    buf[i++] = 'x';

  buf[i++] = 'a' + tf;
  buf[i++] = '1' + tr;

  if (is_promo) {
    buf[i++] = '=';
    buf[i++] = 'Q';
  }
  buf[i] = '\0';
}

void move_to_uci(struct Move m, bool is_promotion, char *buf) {
  buf[0] = (m.from >> 4) + 'a';
  buf[1] = (m.from & 0x0F) + '1';
  buf[2] = (m.to >> 4) + 'a';
  buf[3] = (m.to & 0x0F) + '1';
  if (is_promotion) {
    buf[4] = 'q';
    buf[5] = '\0';
  } else {
    buf[4] = '\0';
  }
}
