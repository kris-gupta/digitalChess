#ifndef CHESS_H
#define CHESS_H

#include <stdbool.h>
#include <stdint.h>

enum Piece_type { PAWN, QUEEN, KING, KNIGHT, DARK_BISHOP, LIGHT_BISHOP, ROOK };
enum Colour { BLACK = 0, WHITE = 1 };
enum Event_type { PIECE_MOVED, PIECE_CAPTURED, CHECK, CHECKMATE };

struct vector_2D {
    int x;
    int y;
};

struct Piece {
    enum Piece_type type;
    enum Colour colour;
    struct vector_2D position;
    bool is_dead;
};

struct Move {
    uint8_t from;
    uint8_t to;
};

struct Event {
    enum Event_type type;
    struct Move move;
    struct Piece piece;
    struct Piece captured;
};

struct PendingMove {
    bool active;
    uint8_t lifted_from;
    struct Piece *piece;
};

struct Player {
    char player_id[32];
    uint8_t last_move;
};

struct Game {
    char id[32];
    struct Player players[2];
    int8_t current_turn;
    struct Piece Pieces[32];
    int8_t board[8][8];
};

#endif
