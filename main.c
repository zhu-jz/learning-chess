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
};

/* Each pair of squares can produce at most four promotion choices. The
   generous bound also accommodates study positions with unusual piece counts. */
enum { MAX_MOVES = 64 * 64 * 4 };

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
        if (strcmp(extra, "moves") == 0)
            puts("info string Move lists are not supported yet");
        else
            puts("info string Invalid position command");
        return;
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

/* This is a temporary board for testing king safety, not a complete move
   application: turn, clocks, and castling rights are not needed by this test. */
static int move_keeps_king_safe(const struct Position *position, struct Move move)
{
    struct Position next = *position;
    next.board[move.to] = next.board[move.from];
    next.board[move.from] = EMPTY;
    if (move.en_passant_capture_square != -1)
        next.board[move.en_passant_capture_square] = EMPTY;
    if (move.promotion != EMPTY)
        next.board[move.to] = move.promotion;
    return is_in_check(&next, position->side_to_move) == 0;
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

/* The caller supplies room for MAX_MOVES entries. Return -1 if either color
   lacks exactly one king. Castling comes in a later lesson. */
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
            int en_passant_capture = -1;
            if (piece == WHITE_PAWN || piece == BLACK_PAWN) {
                en_passant_capture = find_en_passant_capture(position, from, to);
                if (!pawn_can_move(position, from, to) && en_passant_capture == -1)
                    continue;
            } else {
                if (!piece_attacks_square(position, from, to))
                    continue;
            }

            struct Move move;
            move.from = from;
            move.to = to;
            move.promotion = EMPTY;
            move.en_passant_capture_square = en_passant_capture;

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

    printf("info string moves partial");
    for (int i = 0; i < count; i++) {
        int from = moves[i].from;
        int to = moves[i].to;
        printf(" %c%d%c%d", 'a' + from % 8, from / 8 + 1,
                           'a' + to % 8, to / 8 + 1);
        if (moves[i].promotion != EMPTY)
            putchar(tolower(piece_symbols[moves[i].promotion]));
    }
    putchar('\n');
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
        } else if (strcmp(command, "quit") == 0) {
            break;
        }

        /* A GUI may wait for our reply before sending its next command. */
        fflush(stdout);
    }

    return 0;
}
