#ifndef BOARD_H
#define BOARD_H

#include "chess.h"
#include <stdbool.h>
#include <stdint.h>

// Initialise game to the standard starting position.
void board_init(struct Game *game);

// Scan the physical board for the next move.
// Blocks until a piece has been lifted and placed elsewhere.
// See pins.h: scans the 8x8 hall-effect sensor matrix via the row 74HC595
// shift registers, diffs against last-known occupancy, and returns the
// (from, to) square pair once a lift and a subsequent place have both been
// debounced. Known limitation: a capture recognized as two near-simultaneous
// lifts (captured piece removed, then capturing piece placed) is not
// specially handled yet — see project notes.
struct Move scanb(void);

// Apply move to the internal board state (captures, en passant, promotion,
// castling rook repositioning). Returns true if the move is a pawn promotion
// (always treated as queen). Sets *skip_next when the king has just castled so
// the caller can consume the rook's physical movement without submitting it.
bool board_apply_move(struct Game *game, struct Move m, bool *skip_next);

// Write the UCI move string into buf (minimum 6 bytes).
// Examples: "e2e4", "e7e8q" for queen promotion.
void move_to_uci(struct Move m, bool is_promotion, char *buf);

// Write the SAN move string into buf (minimum 9 bytes).
// Must be called BEFORE board_apply_move so the piece is still at m.from.
// Examples: "e4", "Nf3", "exd5", "O-O", "e8=Q"
// Note: does not append check (+) or checkmate (#) markers.
// Note: does not resolve ambiguity (two same-type pieces reaching the same
// square);
//       add from-file/rank disambiguation if needed for your games.
void move_to_san(const struct Game *game, struct Move m, char *buf);

#endif
