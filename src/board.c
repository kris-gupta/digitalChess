#include "board.h"
#include <string.h>

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
    {ROOK,         WHITE, 0, 0}, // a1
    {KNIGHT,       WHITE, 1, 0}, // b1
    {DARK_BISHOP,  WHITE, 2, 0}, // c1 — dark square (file+rank even)
    {QUEEN,        WHITE, 3, 0}, // d1
    {KING,         WHITE, 4, 0}, // e1
    {LIGHT_BISHOP, WHITE, 5, 0}, // f1 — light square (file+rank odd)
    {KNIGHT,       WHITE, 6, 0}, // g1
    {ROOK,         WHITE, 7, 0}, // h1
    {PAWN,         WHITE, 0, 1}, // a2
    {PAWN,         WHITE, 1, 1}, // b2
    {PAWN,         WHITE, 2, 1}, // c2
    {PAWN,         WHITE, 3, 1}, // d2
    {PAWN,         WHITE, 4, 1}, // e2
    {PAWN,         WHITE, 5, 1}, // f2
    {PAWN,         WHITE, 6, 1}, // g2
    {PAWN,         WHITE, 7, 1}, // h2
    {ROOK,         BLACK, 0, 7}, // a8
    {KNIGHT,       BLACK, 1, 7}, // b8
    {LIGHT_BISHOP, BLACK, 2, 7}, // c8 — light square (file+rank odd)
    {QUEEN,        BLACK, 3, 7}, // d8
    {KING,         BLACK, 4, 7}, // e8
    {DARK_BISHOP,  BLACK, 5, 7}, // f8 — dark square (file+rank even)
    {KNIGHT,       BLACK, 6, 7}, // g8
    {ROOK,         BLACK, 7, 7}, // h8
    {PAWN,         BLACK, 0, 6}, // a7
    {PAWN,         BLACK, 1, 6}, // b7
    {PAWN,         BLACK, 2, 6}, // c7
    {PAWN,         BLACK, 3, 6}, // d7
    {PAWN,         BLACK, 4, 6}, // e7
    {PAWN,         BLACK, 5, 6}, // f7
    {PAWN,         BLACK, 6, 6}, // g7
    {PAWN,         BLACK, 7, 6}, // h7
};

void board_init(struct Game *game) {
    memset(game, 0, sizeof(*game));
    memset(game->board, -1, sizeof(game->board));
    game->current_turn = WHITE;
    for (int i = 0; i < 32; i++) {
        game->Pieces[i].type       = start_pos[i].type;
        game->Pieces[i].colour     = start_pos[i].colour;
        game->Pieces[i].position.x = start_pos[i].file;
        game->Pieces[i].position.y = start_pos[i].rank;
        game->Pieces[i].is_dead    = false;
        game->board[start_pos[i].file][start_pos[i].rank] = (int8_t)i;
    }
}

struct Move scanb(void) {
    // TODO: implement hall-effect sensor scanning
    struct Move m = {0};
    return m;
}

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
        game->Pieces[ti].is_dead  = true;
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
            uint8_t rook_to   = (diff == 2) ? 5 : 3;
            int8_t  ri        = game->board[rook_from][fr];
            if (ri >= 0) {
                game->board[rook_from][fr] = -1;
                game->board[rook_to][fr]   = ri;
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
