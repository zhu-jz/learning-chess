#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum Piece {
    EMPTY,
    WHITE_PAWN, WHITE_KNIGHT, WHITE_BISHOP, WHITE_ROOK, WHITE_QUEEN, WHITE_KING,
    BLACK_PAWN, BLACK_KNIGHT, BLACK_BISHOP, BLACK_ROOK, BLACK_QUEEN, BLACK_KING
};

enum Color { WHITE, BLACK };
enum CastlingRight { WHITE_KINGSIDE, WHITE_QUEENSIDE, BLACK_KINGSIDE, BLACK_QUEENSIDE };

/* Symbols follow the order of enum Piece. Uppercase pieces are White. */
static const char piece_symbols[] = ".PNBRQKpnbrqk";
static const char castling_symbols[] = "KQkq";

struct Position {
    /* Files run from a to h; ranks run from 1 to 8.
       Square = rank * 8 + file, with both rank and file counted from zero. */
    int board[64];
    int side_to_move;
    int castling[4];             /* One flag for each enum CastlingRight. */
    int en_passant_square;       /* -1 means there is no target square. */
    int halfmove_clock;          /* Plies since the last pawn move or capture. */
    int fullmove_number;         /* Starts at 1; increases after Black moves. */
};

struct Move {
    int from;
    int to;
    int promotion;              /* Colored piece to place, or EMPTY for none. */
    int en_passant_capture_square; /* Captured pawn's square, or -1. */
    int rook_from;              /* Rook squares for castling, or -1. */
    int rook_to;
};

/* Each pair of squares can produce at most four promotion choices. The
   generous bound also accommodates study positions with unusual piece counts. */
enum { MAX_MOVES = 64 * 64 * 4 };

/* Even 64 queens score only 57600 with our teaching material values. */
enum { MATE_SCORE = 100000, INFINITE_SCORE = 100001, MAX_SEARCH_DEPTH = 4 };

struct SearchResult {
    int score;
    int has_move;
    struct Move move;
};

static void set_start_position(struct Position *position)
{
    const int white_back_rank[8] = {
        WHITE_ROOK, WHITE_KNIGHT, WHITE_BISHOP, WHITE_QUEEN,
        WHITE_KING, WHITE_BISHOP, WHITE_KNIGHT, WHITE_ROOK
    };
    const int black_back_rank[8] = {
        BLACK_ROOK, BLACK_KNIGHT, BLACK_BISHOP, BLACK_QUEEN,
        BLACK_KING, BLACK_BISHOP, BLACK_KNIGHT, BLACK_ROOK
    };

    for (int square = 0; square < 64; square++)
        position->board[square] = EMPTY;

    for (int file = 0; file < 8; file++) {
        position->board[0 * 8 + file] = white_back_rank[file];
        position->board[1 * 8 + file] = WHITE_PAWN;
        position->board[6 * 8 + file] = BLACK_PAWN;
        position->board[7 * 8 + file] = black_back_rank[file];
    }

    position->side_to_move = WHITE;
    for (int right = 0; right < 4; right++)
        position->castling[right] = 1;
    position->en_passant_square = -1;
    position->halfmove_clock = 0;
    position->fullmove_number = 1;
}

static int read_pieces(struct Position *position, const char *text)
{
    int rank = 7;
    int file = 0;

    for (int square = 0; square < 64; square++)
        position->board[square] = EMPTY;

    for (int i = 0; text[i] != '\0'; i++) {
        char symbol = text[i];

        if (symbol == '/') {
            if (file != 8 || rank == 0)
                return 0;
            rank--;
            file = 0;
        } else if (symbol >= '1' && symbol <= '8') {
            if (i > 0 && text[i - 1] >= '1' && text[i - 1] <= '8')
                return 0;
            file += symbol - '0';
            if (file > 8)
                return 0;
        } else {
            int piece;
            for (piece = WHITE_PAWN; piece <= BLACK_KING; piece++) {
                if (piece_symbols[piece] == symbol)
                    break;
            }
            if (piece > BLACK_KING || file >= 8)
                return 0;
            position->board[rank * 8 + file] = piece;
            file++;
        }
    }

    return rank == 0 && file == 8;
}

static int read_number(const char *text, int *result)
{
    int number = 0;

    if (text[0] == '\0')
        return 0;
    for (int i = 0; text[i] != '\0'; i++) {
        if (text[i] < '0' || text[i] > '9')
            return 0;
        int digit = text[i] - '0';
        /* Check before multiplying: overflowing a signed int is not safe C. */
        if (number > (INT_MAX - digit) / 10)
            return 0;
        number = number * 10 + digit;
    }
    *result = number;
    return 1;
}

/* The caller supplies all six fields and a temporary position to fill. */
static int read_fen(struct Position *position, char *fields[6])
{
    if (!read_pieces(position, fields[0]))
        return 0;

    if (strcmp(fields[1], "w") == 0)
        position->side_to_move = WHITE;
    else if (strcmp(fields[1], "b") == 0)
        position->side_to_move = BLACK;
    else
        return 0;

    for (int right = 0; right < 4; right++)
        position->castling[right] = 0;
    if (strcmp(fields[2], "-") != 0) {
        int right = 0;
        for (int i = 0; fields[2][i] != '\0'; i++) {
            /* Advancing only forwards enforces KQkq order and no duplicates. */
            while (right < 4 && castling_symbols[right] != fields[2][i])
                right++;
            if (right == 4)
                return 0;
            position->castling[right] = 1;
            right++;
        }
    }

    position->en_passant_square = -1;
    if (strcmp(fields[3], "-") != 0) {
        const char *square = fields[3];
        if (strlen(square) != 2 || square[0] < 'a' || square[0] > 'h')
            return 0;
        if (position->side_to_move == WHITE && square[1] != '6')
            return 0;
        if (position->side_to_move == BLACK && square[1] != '3')
            return 0;
        position->en_passant_square = (square[1] - '1') * 8 + square[0] - 'a';
    }

    if (!read_number(fields[4], &position->halfmove_clock))
        return 0;
    if (!read_number(fields[5], &position->fullmove_number))
        return 0;
    return position->fullmove_number >= 1;
}

/* Defined after the move generator, which supplies the legal choices. */
static int play_uci_move(struct Position *position, const char *text);

/* Continue tokenizing the line after the main loop has read "position". */
static void read_position_command(struct Position *position)
{
    struct Position next;
    char *kind = strtok(NULL, " \t\r");

    if (kind == NULL) {
        puts("info string Invalid position command");
        return;
    }
    if (strcmp(kind, "startpos") == 0) {
        set_start_position(&next);
    } else if (strcmp(kind, "fen") == 0) {
        char *fields[6];
        for (int i = 0; i < 6; i++) {
            fields[i] = strtok(NULL, " \t\r");
            if (fields[i] == NULL) {
                puts("info string Invalid position command");
                return;
            }
        }
        if (!read_fen(&next, fields)) {
            puts("info string Invalid position command");
            return;
        }
    } else {
        puts("info string Invalid position command");
        return;
    }

    char *extra = strtok(NULL, " \t\r");
    if (extra != NULL) {
        if (strcmp(extra, "moves") != 0) {
            puts("info string Invalid position command");
            return;
        }
        char *text;
        while ((text = strtok(NULL, " \t\r")) != NULL) {
            if (!play_uci_move(&next, text)) {
                printf("info string Cannot play move %s\n", text);
                return;
            }
        }
    }

    /* Replace the old position only after the entire command succeeds. */
    *position = next;
}

/* Empty squares have no color. */
static int piece_color(int piece)
{
    if (piece == EMPTY)
        return -1;
    if (piece <= WHITE_KING)
        return WHITE;
    return BLACK;
}

/* Teaching values in centipawns: one pawn is worth 100. Kings do not have a
   material price; search will handle checkmate separately from evaluation. */
static int piece_value(int piece)
{
    if (piece == WHITE_PAWN || piece == BLACK_PAWN)
        return 100;
    if (piece == WHITE_KNIGHT || piece == BLACK_KNIGHT)
        return 300;
    if (piece == WHITE_BISHOP || piece == BLACK_BISHOP)
        return 300;
    if (piece == WHITE_ROOK || piece == BLACK_ROOK)
        return 500;
    if (piece == WHITE_QUEEN || piece == BLACK_QUEEN)
        return 900;
    return 0;
}

/* Positive means a material advantage for the side whose turn it is. */
static int evaluate(const struct Position *position)
{
    int score = 0;
    for (int square = 0; square < 64; square++) {
        int piece = position->board[square];
        if (piece_color(piece) == WHITE)
            score += piece_value(piece);
        else if (piece_color(piece) == BLACK)
            score -= piece_value(piece);
    }
    if (position->side_to_move == BLACK)
        score = -score;
    return score;
}

static void read_eval_command(const struct Position *position)
{
    if (strtok(NULL, " \t\r") != NULL) {
        puts("info string Invalid eval command");
        return;
    }
    printf("info string eval material cp %d\n", evaluate(position));
}

/* Test the piece's attack pattern, without considering its own king's safety.
   Both squares must be valid board indices. The target may be occupied by
   either color: a piece also defends a square occupied by a friendly piece. */
static int piece_attacks_square(const struct Position *position, int from, int to)
{
    int piece = position->board[from];
    int file_difference = to % 8 - from % 8;
    int rank_difference = to / 8 - from / 8;
    int file_distance = abs(file_difference);
    int rank_distance = abs(rank_difference);

    if (piece == EMPTY || from == to)
        return 0;

    /* Pawns attack diagonally, even when the target square is empty. */
    if (piece == WHITE_PAWN)
        return file_distance == 1 && rank_difference == 1;
    if (piece == BLACK_PAWN)
        return file_distance == 1 && rank_difference == -1;

    if (piece == WHITE_KNIGHT || piece == BLACK_KNIGHT)
        return (file_distance == 1 && rank_distance == 2)
            || (file_distance == 2 && rank_distance == 1);

    if (piece == WHITE_KING || piece == BLACK_KING)
        return file_distance <= 1 && rank_distance <= 1;

    if (piece == WHITE_BISHOP || piece == BLACK_BISHOP) {
        if (file_distance != rank_distance)
            return 0;
    } else if (piece == WHITE_ROOK || piece == BLACK_ROOK) {
        if (file_difference != 0 && rank_difference != 0)
            return 0;
    } else if (piece == WHITE_QUEEN || piece == BLACK_QUEEN) {
        if (file_difference != 0 && rank_difference != 0
            && file_distance != rank_distance)
            return 0;
    } else {
        return 0;
    }

    /* The target is on a sliding piece's line. Walk towards it, stopping
       before the target: only intervening pieces block the attack. */
    int file_step = 0;
    int rank_step = 0;
    if (file_difference > 0)
        file_step = 1;
    if (file_difference < 0)
        file_step = -1;
    if (rank_difference > 0)
        rank_step = 1;
    if (rank_difference < 0)
        rank_step = -1;

    int file = from % 8 + file_step;
    int rank = from / 8 + rank_step;
    while (rank * 8 + file != to) {
        if (position->board[rank * 8 + file] != EMPTY)
            return 0;
        file += file_step;
        rank += rank_step;
    }
    return 1;
}

static int is_square_attacked(const struct Position *position, int square, int by_color)
{
    for (int from = 0; from < 64; from++) {
        if (piece_color(position->board[from]) == by_color
            && piece_attacks_square(position, from, square))
            return 1;
    }
    return 0;
}

/* Return -1 unless there is exactly one king of the requested color. */
static int find_king(const struct Position *position, int color)
{
    int king;
    if (color == WHITE)
        king = WHITE_KING;
    else
        king = BLACK_KING;

    int king_square = -1;
    for (int square = 0; square < 64; square++) {
        if (position->board[square] == king) {
            if (king_square != -1)
                return -1;
            king_square = square;
        }
    }
    return king_square;
}

/* Return 1 for check, 0 for no check, or -1 for a missing or duplicate king.
   The caller must handle -1 separately rather than treating it as true. */
static int is_in_check(const struct Position *position, int color)
{
    int king_square = find_king(position, color);
    if (king_square == -1)
        return -1;

    int opponent;
    if (color == WHITE)
        opponent = BLACK;
    else
        opponent = WHITE;
    return is_square_attacked(position, king_square, opponent);
}

/* Change only piece placement. Move must come from the generator. */
static void move_pieces(struct Position *position, struct Move move)
{
    position->board[move.to] = position->board[move.from];
    position->board[move.from] = EMPTY;
    if (move.en_passant_capture_square != -1)
        position->board[move.en_passant_capture_square] = EMPTY;
    if (move.promotion != EMPTY)
        position->board[move.to] = move.promotion;
    if (move.rook_from != -1) {
        position->board[move.rook_to] = position->board[move.rook_from];
        position->board[move.rook_from] = EMPTY;
    }
}

/* Only piece placement matters here; turn and other metadata stay unchanged. */
static int move_keeps_king_safe(const struct Position *position, struct Move move)
{
    struct Position next = *position;
    move_pieces(&next, move);
    return is_in_check(&next, position->side_to_move) == 0;
}

static void remove_rook_castling_right(struct Position *position, int piece, int square)
{
    if (piece == WHITE_ROOK) {
        if (square == 0)
            position->castling[WHITE_QUEENSIDE] = 0;
        if (square == 7)
            position->castling[WHITE_KINGSIDE] = 0;
    } else if (piece == BLACK_ROOK) {
        if (square == 56)
            position->castling[BLACK_QUEENSIDE] = 0;
        if (square == 63)
            position->castling[BLACK_KINGSIDE] = 0;
    }
}

/* Apply a generated legal move. Refuse an overflowing counter before changing
   anything; the FEN reader permits counters all the way up to INT_MAX. */
static int apply_move(struct Position *position, struct Move move)
{
    int piece = position->board[move.from];
    int captured = position->board[move.to];
    int pawn_move = piece == WHITE_PAWN || piece == BLACK_PAWN;
    int capture = captured != EMPTY || move.en_passant_capture_square != -1;
    if (!pawn_move && !capture && position->halfmove_clock == INT_MAX)
        return 0;
    if (position->side_to_move == BLACK && position->fullmove_number == INT_MAX)
        return 0;

    if (piece == WHITE_KING) {
        position->castling[WHITE_KINGSIDE] = 0;
        position->castling[WHITE_QUEENSIDE] = 0;
    } else if (piece == BLACK_KING) {
        position->castling[BLACK_KINGSIDE] = 0;
        position->castling[BLACK_QUEENSIDE] = 0;
    }
    remove_rook_castling_right(position, piece, move.from);
    remove_rook_castling_right(position, captured, move.to);

    move_pieces(position, move);
    position->en_passant_square = -1;
    if (pawn_move && abs(move.to - move.from) == 16)
        position->en_passant_square = (move.from + move.to) / 2;

    if (pawn_move || capture)
        position->halfmove_clock = 0;
    else
        position->halfmove_clock++;

    if (position->side_to_move == WHITE) {
        position->side_to_move = BLACK;
    } else {
        position->fullmove_number++;
        position->side_to_move = WHITE;
    }
    return 1;
}

/* The caller has already excluded friendly targets and king captures.
   Unlike attacks, pawn moves include forward advances into empty squares. */
static int pawn_can_move(const struct Position *position, int from, int to)
{
    int from_rank = from / 8;
    int to_rank = to / 8;
    int direction;
    int starting_rank;
    if (position->board[from] == WHITE_PAWN) {
        direction = 1;
        starting_rank = 1;
    } else {
        direction = -1;
        starting_rank = 6;
    }

    /* A pawn already on an end rank is an invalid study-position piece. */
    if (from_rank == 0 || from_rank == 7)
        return 0;

    int target = position->board[to];
    if (from % 8 != to % 8)
        return target != EMPTY && piece_attacks_square(position, from, to);

    if (target != EMPTY)
        return 0;
    if (to_rank - from_rank == direction)
        return 1;
    if (from_rank == starting_rank && to_rank - from_rank == 2 * direction)
        return position->board[from + direction * 8] == EMPTY;
    return 0;
}

/* Return the captured pawn's square, or -1 if this is not an en passant move.
   The FEN target alone is not enough: the board must support the capture. */
static int find_en_passant_capture(const struct Position *position, int from, int to)
{
    if (to != position->en_passant_square || position->board[to] != EMPTY)
        return -1;

    int piece = position->board[from];
    int captured_square;
    int captured_pawn;
    if (piece == WHITE_PAWN) {
        if (from / 8 != 4 || to / 8 != 5)
            return -1;
        captured_square = to - 8;
        captured_pawn = BLACK_PAWN;
    } else if (piece == BLACK_PAWN) {
        if (from / 8 != 3 || to / 8 != 2)
            return -1;
        captured_square = to + 8;
        captured_pawn = WHITE_PAWN;
    } else {
        return -1;
    }

    if (!piece_attacks_square(position, from, to))
        return -1;
    if (position->board[captured_square] != captured_pawn)
        return -1;
    return captured_square;
}

/* Prepare standard castling after checking the right, path, and the king's
   starting and crossing squares. The caller tests the final board as usual. */
static int prepare_castling_move(const struct Position *position, struct Move *move)
{
    int home_rank;
    int rook;
    int kingside_right;
    int queenside_right;
    int king = position->board[move->from];
    if (king == WHITE_KING) {
        home_rank = 0;
        rook = WHITE_ROOK;
        kingside_right = WHITE_KINGSIDE;
        queenside_right = WHITE_QUEENSIDE;
    } else if (king == BLACK_KING) {
        home_rank = 7;
        rook = BLACK_ROOK;
        kingside_right = BLACK_KINGSIDE;
        queenside_right = BLACK_QUEENSIDE;
    } else {
        return 0;
    }

    int home = home_rank * 8;
    if (move->from != home + 4)
        return 0;

    int right;
    int rook_from;
    int rook_to;
    int step;
    if (move->to == home + 6) {
        right = kingside_right;
        rook_from = home + 7;
        rook_to = home + 5;
        step = 1;
    } else if (move->to == home + 2) {
        right = queenside_right;
        rook_from = home;
        rook_to = home + 3;
        step = -1;
    } else {
        return 0;
    }

    if (!position->castling[right] || position->board[rook_from] != rook)
        return 0;
    /* Queenside also requires b1/b8 to be empty, though the king skips it. */
    for (int square = move->from + step; square != rook_from; square += step) {
        if (position->board[square] != EMPTY)
            return 0;
    }
    if (is_in_check(position, position->side_to_move) != 0)
        return 0;

    /* Test the crossing square with the king moved and the rook still home. */
    struct Move crossing = *move;
    crossing.to = move->from + step;
    if (!move_keeps_king_safe(position, crossing))
        return 0;

    move->rook_from = rook_from;
    move->rook_to = rook_to;
    return 1;
}

/* The caller supplies room for MAX_MOVES entries. Return -1 if either color
   lacks exactly one king. This generator supports standard chess, not Chess960. */
static int generate_moves(const struct Position *position, struct Move moves[])
{
    const int white_promotions[4] = { WHITE_KNIGHT, WHITE_BISHOP, WHITE_ROOK, WHITE_QUEEN };
    const int black_promotions[4] = { BLACK_KNIGHT, BLACK_BISHOP, BLACK_ROOK, BLACK_QUEEN };

    if (find_king(position, WHITE) == -1 || find_king(position, BLACK) == -1)
        return -1;

    int count = 0;
    for (int from = 0; from < 64; from++) {
        int piece = position->board[from];
        if (piece_color(piece) != position->side_to_move)
            continue;
        for (int to = 0; to < 64; to++) {
            int target = position->board[to];
            if (piece_color(target) == position->side_to_move)
                continue;
            /* Kings are never captured; checkmate is determined from legal moves. */
            if (target == WHITE_KING || target == BLACK_KING)
                continue;
            struct Move move;
            move.from = from;
            move.to = to;
            move.promotion = EMPTY;
            move.en_passant_capture_square = -1;
            move.rook_from = -1;
            move.rook_to = -1;

            if (piece == WHITE_PAWN || piece == BLACK_PAWN) {
                move.en_passant_capture_square = find_en_passant_capture(position, from, to);
                if (!pawn_can_move(position, from, to) && move.en_passant_capture_square == -1)
                    continue;
            } else {
                if (!piece_attacks_square(position, from, to)) {
                    if (!prepare_castling_move(position, &move))
                        continue;
                }
            }

            if ((piece == WHITE_PAWN && to / 8 == 7)
                || (piece == BLACK_PAWN && to / 8 == 0)) {
                for (int choice = 0; choice < 4; choice++) {
                    if (piece == WHITE_PAWN)
                        move.promotion = white_promotions[choice];
                    else
                        move.promotion = black_promotions[choice];
                    if (move_keeps_king_safe(position, move)) {
                        moves[count] = move;
                        count++;
                    }
                }
                /* There is no fifth choice that leaves the piece a pawn. */
                continue;
            }

            if (move_keeps_king_safe(position, move)) {
                moves[count] = move;
                count++;
            }
        }
    }
    return count;
}

/* Six bytes hold four coordinate characters, an optional suffix, and NUL. */
static void move_to_string(struct Move move, char text[6])
{
    text[0] = 'a' + move.from % 8;
    text[1] = '1' + move.from / 8;
    text[2] = 'a' + move.to % 8;
    text[3] = '1' + move.to / 8;
    text[4] = '\0';
    if (move.promotion != EMPTY) {
        text[4] = (char)tolower(piece_symbols[move.promotion]);
        text[5] = '\0';
    }
}

/* Matching against generated moves also supplies promotion, rook, and en
   passant details, so the text parser does not need to reconstruct them. */
static int play_uci_move(struct Position *position, const char *text)
{
    if (strlen(text) != 4 && strlen(text) != 5)
        return 0;
    struct Move moves[MAX_MOVES];
    int count = generate_moves(position, moves);
    for (int i = 0; i < count; i++) {
        char candidate[6];
        move_to_string(moves[i], candidate);
        if (strcmp(text, candidate) == 0)
            return apply_move(position, moves[i]);
    }
    return 0;
}

static void read_moves_command(const struct Position *position)
{
    if (strtok(NULL, " \t\r") != NULL) {
        puts("info string Invalid moves command");
        return;
    }

    struct Move moves[MAX_MOVES];
    int count = generate_moves(position, moves);
    if (count == -1) {
        puts("info string Cannot generate moves: expected exactly one king of each color");
        return;
    }

    printf("info string moves legal");
    for (int i = 0; i < count; i++) {
        char text[6];
        move_to_string(moves[i], text);
        printf(" %s", text);
    }
    putchar('\n');
}

/* Count sequences of exactly depth moves. Return zero on failure, without
   publishing a partial count. Every child starts as a copy of its parent. */
static int perft(const struct Position *position, int depth, unsigned long long *nodes)
{
    if (depth == 0) {
        *nodes = 1;
        return 1;
    }

    /* A large local array at every recursive level would exhaust the stack. */
    struct Move *moves = malloc(MAX_MOVES * sizeof(struct Move));
    if (moves == NULL)
        return 0;
    int count = generate_moves(position, moves);
    if (count == -1) {
        free(moves);
        return 0;
    }

    unsigned long long total = 0;
    for (int i = 0; i < count; i++) {
        struct Position child = *position;
        unsigned long long child_nodes;
        if (!apply_move(&child, moves[i]) || !perft(&child, depth - 1, &child_nodes)) {
            free(moves);
            return 0;
        }
        if (ULLONG_MAX - total < child_nodes) {
            free(moves);
            return 0;
        }
        total += child_nodes;
    }
    free(moves);
    *nodes = total;
    return 1;
}

static void read_perft_command(const struct Position *position)
{
    char *text = strtok(NULL, " \t\r");
    char *extra = strtok(NULL, " \t\r");
    int depth;
    if (text == NULL || extra != NULL || !read_number(text, &depth) || depth > 6) {
        puts("info string Invalid perft command");
        return;
    }
    if (find_king(position, WHITE) == -1 || find_king(position, BLACK) == -1) {
        puts("info string Cannot run perft: expected exactly one king of each color");
        return;
    }

    /* Perft ignores draw counters. Reset them on the copy so even a FEN with
       maximum counters can be traversed without overflowing apply_move. */
    struct Position root = *position;
    root.halfmove_clock = 0;
    root.fullmove_number = 1;
    unsigned long long nodes;
    if (!perft(&root, depth, &nodes)) {
        puts("info string Cannot complete perft");
        return;
    }
    printf("info string perft depth %d nodes %llu\n", depth, nodes);
}

/* Search every legal move. Scores always belong to the side to move, so a
   child's score is negated when it is viewed from the parent's side. */
static int negamax(const struct Position *position, int depth, int ply,
                   unsigned long long *nodes, struct SearchResult *result)
{
    if (*nodes == ULLONG_MAX)
        return 0;
    (*nodes)++;
    result->has_move = 0;

    struct Move *moves = malloc(MAX_MOVES * sizeof(struct Move));
    if (moves == NULL)
        return 0;
    int count = generate_moves(position, moves);
    if (count == -1) {
        free(moves);
        return 0;
    }

    /* Check for terminal positions even at depth zero. Otherwise a mate on
       the last searched move would be mistaken for a material score. */
    if (count == 0) {
        result->score = 0;
        if (is_in_check(position, position->side_to_move) == 1)
            result->score = -MATE_SCORE + ply;
        free(moves);
        return 1;
    }
    if (depth == 0) {
        result->score = evaluate(position);
        free(moves);
        return 1;
    }

    result->score = -INFINITE_SCORE;
    for (int i = 0; i < count; i++) {
        struct Position child = *position;
        struct SearchResult reply;
        if (!apply_move(&child, moves[i]) || !negamax(&child, depth - 1, ply + 1, nodes, &reply)) {
            free(moves);
            return 0;
        }
        int score = -reply.score;
        /* Strict comparison keeps the first generated move when scores tie. */
        if (score > result->score) {
            result->score = score;
            result->move = moves[i];
            result->has_move = 1;
        }
    }
    free(moves);
    return 1;
}

static void read_go_command(const struct Position *position)
{
    char *limit = strtok(NULL, " \t\r");
    char *text = strtok(NULL, " \t\r");
    char *extra = strtok(NULL, " \t\r");
    int depth;
    if (limit == NULL || strcmp(limit, "depth") != 0 || text == NULL || extra != NULL
        || !read_number(text, &depth) || depth < 1 || depth > MAX_SEARCH_DEPTH) {
        puts("info string Invalid go command: expected go depth 1..4");
        return;
    }
    if (find_king(position, WHITE) == -1 || find_king(position, BLACK) == -1) {
        puts("info string Cannot search: expected exactly one king of each color");
        return;
    }

    /* Draw rules are a later lesson. Clocks do not affect this search, so
       normalize them on the copy to keep apply_move's counters in range. */
    struct Position root = *position;
    root.halfmove_clock = 0;
    root.fullmove_number = 1;
    unsigned long long nodes = 0;
    struct SearchResult result;
    if (!negamax(&root, depth, 0, &nodes, &result)) {
        puts("info string Cannot complete search");
        return;
    }

    printf("info depth %d score ", depth);
    if (abs(result.score) >= MATE_SCORE - MAX_SEARCH_DEPTH) {
        int plies = MATE_SCORE - abs(result.score);
        int moves_to_mate = (plies + 1) / 2;
        if (result.score < 0)
            moves_to_mate = -moves_to_mate;
        printf("mate %d", moves_to_mate);
    } else {
        printf("cp %d", result.score);
    }
    printf(" nodes %llu\n", nodes);
    if (result.has_move) {
        char text[6];
        move_to_string(result.move, text);
        printf("bestmove %s\n", text);
    } else {
        puts("bestmove 0000");
    }
}

static void read_check_command(const struct Position *position)
{
    char *color_name = strtok(NULL, " \t\r");
    char *extra = strtok(NULL, " \t\r");
    if (color_name == NULL || extra != NULL) {
        puts("info string Invalid check command");
        return;
    }

    int color;
    if (strcmp(color_name, "white") == 0)
        color = WHITE;
    else if (strcmp(color_name, "black") == 0)
        color = BLACK;
    else {
        puts("info string Invalid check command");
        return;
    }

    int check = is_in_check(position, color);
    if (check == -1) {
        printf("info string Cannot check %s: expected exactly one king\n", color_name);
        return;
    }
    printf("info string check %s ", color_name);
    if (check == 1)
        puts("yes");
    else
        puts("no");
}

/* A manual inspection command, independent of whose turn it is. */
static void read_attack_command(const struct Position *position)
{
    char *square_name = strtok(NULL, " \t\r");
    char *color_name = strtok(NULL, " \t\r");
    char *extra = strtok(NULL, " \t\r");

    if (square_name == NULL || color_name == NULL || extra != NULL) {
        puts("info string Invalid attacked command");
        return;
    }
    if (strlen(square_name) != 2
        || square_name[0] < 'a' || square_name[0] > 'h'
        || square_name[1] < '1' || square_name[1] > '8') {
        puts("info string Invalid attacked command");
        return;
    }

    int color;
    if (strcmp(color_name, "white") == 0)
        color = WHITE;
    else if (strcmp(color_name, "black") == 0)
        color = BLACK;
    else {
        puts("info string Invalid attacked command");
        return;
    }

    int square = (square_name[1] - '1') * 8 + square_name[0] - 'a';
    int attacked = is_square_attacked(position, square, color);
    printf("info string attacked %s %s ", square_name, color_name);
    if (attacked)
        puts("yes");
    else
        puts("no");
}

static void print_board(const struct Position *position)
{
    /* Print from rank 8 down to rank 1, as seen from White's side. */
    for (int rank = 7; rank >= 0; rank--) {
        printf("%d", rank + 1);
        for (int file = 0; file < 8; file++) {
            int square = rank * 8 + file;
            int piece = position->board[square];
            printf(" %c", piece_symbols[piece]);
        }
        putchar('\n');
    }
    puts("  a b c d e f g h");
    if (position->side_to_move == WHITE)
        puts("Side to move: White");
    else
        puts("Side to move: Black");

    printf("Castling: ");
    int has_castling = 0;
    for (int right = 0; right < 4; right++) {
        if (position->castling[right]) {
            putchar(castling_symbols[right]);
            has_castling = 1;
        }
    }
    if (!has_castling)
        putchar('-');
    putchar('\n');

    int square = position->en_passant_square;
    if (square == -1)
        puts("En passant: -");
    else
        printf("En passant: %c%d\n", 'a' + square % 8, square / 8 + 1);
    printf("Halfmove clock: %d\n", position->halfmove_clock);
    printf("Fullmove number: %d\n", position->fullmove_number);
}

/* Return zero when there is no more input. An oversized line is discarded
   in full so its remaining text cannot be mistaken for another command. */
static int read_command(char *line, int capacity)
{
    int length = 0;
    int too_long = 0;
    int character;

    while ((character = getchar()) != EOF && character != '\n') {
        if (length < capacity - 1) {
            line[length] = (char)character;
            length++;
        } else {
            too_long = 1;
        }
    }

    line[length] = '\0';
    if (too_long)
        line[0] = '\0';

    return character != EOF || length > 0;
}

int main(void)
{
    struct Position position;
    char line[4096];
    char *command;

    set_start_position(&position);

    while (read_command(line, sizeof(line))) {
        command = strtok(line, " \t\r");
        if (command == NULL)
            continue;

        if (strcmp(command, "uci") == 0) {
            puts("id name Learning Chess");
            puts("id author Learning Chess contributors");
            puts("uciok");
        } else if (strcmp(command, "isready") == 0) {
            puts("readyok");
        } else if (strcmp(command, "position") == 0) {
            read_position_command(&position);
        } else if (strcmp(command, "d") == 0) {
            print_board(&position);
        } else if (strcmp(command, "attacked") == 0) {
            read_attack_command(&position);
        } else if (strcmp(command, "check") == 0) {
            read_check_command(&position);
        } else if (strcmp(command, "moves") == 0) {
            read_moves_command(&position);
        } else if (strcmp(command, "perft") == 0) {
            read_perft_command(&position);
        } else if (strcmp(command, "eval") == 0) {
            read_eval_command(&position);
        } else if (strcmp(command, "go") == 0) {
            read_go_command(&position);
        } else if (strcmp(command, "quit") == 0) {
            break;
        }

        /* A GUI may wait for our reply before sending its next command. */
        fflush(stdout);
    }

    return 0;
}
