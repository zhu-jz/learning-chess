#include <stdio.h>
#include <string.h>

enum Piece {
    EMPTY,
    WHITE_PAWN, WHITE_KNIGHT, WHITE_BISHOP, WHITE_ROOK, WHITE_QUEEN, WHITE_KING,
    BLACK_PAWN, BLACK_KNIGHT, BLACK_BISHOP, BLACK_ROOK, BLACK_QUEEN, BLACK_KING
};

struct Position {
    /* Files run from a to h; ranks run from 1 to 8.
       Square = rank * 8 + file, with both rank and file counted from zero. */
    int board[64];
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
}

static void print_board(const struct Position *position)
{
    /* Symbols follow the order of enum Piece. Uppercase pieces are White. */
    const char piece_symbols[] = ".PNBRQKpnbrqk";

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
