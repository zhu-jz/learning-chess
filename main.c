#include <limits.h>
#include <stdio.h>
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
        } else if (strcmp(command, "quit") == 0) {
            break;
        }

        /* A GUI may wait for our reply before sending its next command. */
        fflush(stdout);
    }

    return 0;
}
