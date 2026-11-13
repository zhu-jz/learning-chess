/* Include the engine in this test program so we can start directly at the
   quiescence horizon or recursion limit. Only its entry point is renamed. */
#define main uci_main
#include "../main.c"
#undef main
#include <assert.h>

static struct Position position_from_fen(char *pieces, char *side, char *en_passant)
{
    char *fields[6] = { pieces, side, "-", en_passant, "0", "1" };
    struct Position position;
    int success = read_fen(&position, fields);
    assert(success);
    return position;
}

static void expect_same_move(struct Move actual, struct Move expected)
{
    assert(actual.from == expected.from && actual.to == expected.to);
    assert(actual.promotion == expected.promotion);
    assert(actual.en_passant_capture_square == expected.en_passant_capture_square);
    assert(actual.rook_from == expected.rook_from && actual.rook_to == expected.rook_to);
}

static void check_move_preference(struct Position position, const char *preferred_text)
{
    struct Move *moves = malloc(MAX_MOVES * sizeof(struct Move));
    struct Move *original = malloc(MAX_MOVES * sizeof(struct Move));
    assert(moves != NULL && original != NULL);
    int count = generate_moves(&position, moves);
    assert(count > 0);
    order_moves(&position, moves, count);
    int selected = -1;
    for (int i = 0; i < count; i++) {
        original[i] = moves[i];
        char text[6];
        move_to_string(moves[i], text);
        if (strcmp(text, preferred_text) == 0)
            selected = i;
    }
    assert(selected >= 0);
    struct Move preferred = original[selected];
    struct Move missing = preferred;
    missing.from = -1;
    search_move_first(moves, count, NULL);
    search_move_first(moves, count, &missing);
    for (int i = 0; i < count; i++)
        expect_same_move(moves[i], original[i]);

    /* A matching hint identifies the move; the legal list supplies its
       en passant and castling details. Underpromotions must match exactly. */
    preferred.en_passant_capture_square = -1;
    preferred.rook_from = -1;
    preferred.rook_to = -1;
    for (int repeat = 0; repeat < 2; repeat++) {
        search_move_first(moves, count, &preferred);
        expect_same_move(moves[0], original[selected]);
        int next = 1;
        for (int i = 0; i < count; i++) {
            if (i != selected) {
                expect_same_move(moves[next], original[i]);
                next++;
            }
        }
    }
    free(original);
    free(moves);
}

static void check_pv(struct Position position, struct SearchResult result, int ply)
{
    assert(result.pv_length >= 0);
    assert(result.pv_length <= MAX_SEARCH_PLY - ply);
    for (int i = 0; i < result.pv_length; i++) {
        char text[6];
        move_to_string(result.pv[i], text);
        int legal = play_uci_move(&position, text);
        assert(legal);
    }
}

static struct SearchResult search_at_horizon(struct Position position, int ply,
                                            int alpha, int beta, int expected_score,
                                            unsigned long long *nodes)
{
    struct SearchResult result;
    *nodes = 0;
    int success = negamax(&position, 0, ply, alpha, beta, NULL, nodes, &result);
    assert(success);
    assert(result.score == expected_score);
    check_pv(position, result, ply);
    return result;
}

static void expect_move(struct SearchResult result, const char *expected)
{
    char text[6];
    assert(result.pv_length > 0);
    move_to_string(result.pv[0], text);
    assert(strcmp(text, expected) == 0);
}

int main(void)
{
    unsigned long long nodes;
    struct Position position;
    struct SearchResult result;

    /* A quiet horizon counts once, with no extra negamax-to-quiescence node. */
    set_start_position(&position);
    result = search_at_horizon(position, 0, -INFINITE_SCORE, INFINITE_SCORE, 0, &nodes);
    assert(nodes == 1 && result.pv_length == 0);

    /* Stand pat can reject a losing capture, and can also cut off directly. */
    position = position_from_fen("7k/8/4p3/3r4/8/8/8/K2Q4", "w", "-");
    result = search_at_horizon(position, 0, -INFINITE_SCORE, INFINITE_SCORE, 300, &nodes);
    assert(nodes == 3 && result.pv_length == 0);
    result = search_at_horizon(position, 0, 0, 200, 300, &nodes);
    assert(nodes == 1 && result.pv_length == 0);

    /* After Qxd5, Black must see exd5 instead of accepting the lost rook. */
    position = position_from_fen("7k/8/4p3/3Q4/8/8/8/K7", "b", "-");
    result = search_at_horizon(position, 0, -INFINITE_SCORE, INFINITE_SCORE, 100, &nodes);
    expect_move(result, "e6d5");
    assert(nodes == 2);

    position = position_from_fen("7k/8/8/3pP3/8/8/8/K7", "w", "d6");
    result = search_at_horizon(position, 0, -INFINITE_SCORE, INFINITE_SCORE, 100, &nodes);
    expect_move(result, "e5d6");
    assert(nodes == 2);

    /* A non-capturing promotion belongs in quiescence, for either color. */
    position = position_from_fen("7k/4P3/8/8/8/8/8/7K", "w", "-");
    result = search_at_horizon(position, 0, -INFINITE_SCORE, INFINITE_SCORE, 900, &nodes);
    expect_move(result, "e7e8q");
    position = position_from_fen("7k/8/8/8/8/8/4p3/7K", "b", "-");
    result = search_at_horizon(position, 0, -INFINITE_SCORE, INFINITE_SCORE, 900, &nodes);
    expect_move(result, "e2e1q");

    /* Queening stalemates; retaining all promotion choices finds a rook. */
    position = position_from_fen("8/k1P5/2K5/8/8/8/8/8", "w", "-");
    result = search_at_horizon(position, 0, -INFINITE_SCORE, INFINITE_SCORE, 500, &nodes);
    expect_move(result, "c7c8r");

    /* Only Kg8 evades check. Even a favorable stand-pat cutoff is forbidden. */
    position = position_from_fen("7k/2p5/5K2/8/8/8/8/7R", "b", "-");
    result = search_at_horizon(position, 0, -600, -500, -400, &nodes);
    expect_move(result, "h8g8");
    assert(nodes == 2);

    /* The last available ply is searched; at the limit we approximate.
       This fallback does not claim that standing pat in check is legal. */
    result = search_at_horizon(position, MAX_SEARCH_PLY - 1,
                               -INFINITE_SCORE, INFINITE_SCORE, -400, &nodes);
    expect_move(result, "h8g8");
    assert(nodes == 2);
    result = search_at_horizon(position, MAX_SEARCH_PLY,
                               -INFINITE_SCORE, INFINITE_SCORE, -400, &nodes);
    assert(nodes == 1 && result.pv_length == 0);

    /* Terminal detection must precede both stand pat and the recursion cap. */
    position = position_from_fen("k7/8/1QK5/8/8/8/8/8", "b", "-");
    result = search_at_horizon(position, 0, -2000, -1000, 0, &nodes);
    assert(nodes == 1 && result.pv_length == 0);
    result = search_at_horizon(position, MAX_SEARCH_PLY, -2000, -1000, 0, &nodes);
    assert(nodes == 1 && result.pv_length == 0);
    position = position_from_fen("k7/1Q6/2K5/8/8/8/8/8", "b", "-");
    result = search_at_horizon(position, MAX_SEARCH_PLY, -INFINITE_SCORE, INFINITE_SCORE,
                               -MATE_SCORE + MAX_SEARCH_PLY, &nodes);
    assert(nodes == 1 && result.pv_length == 0);

    /* Qxg7 mates during quiescence, beyond the normal maximum depth. */
    position = position_from_fen("7k/6r1/5KQ1/8/8/8/8/8", "w", "-");
    result = search_at_horizon(position, 4, -INFINITE_SCORE, INFINITE_SCORE,
                               MATE_SCORE - 5, &nodes);
    expect_move(result, "g6g7");

    /* Ordinary search builds a full line. Near the recursion limit only
       the remaining plies fit, and reusing the result must discard old moves. */
    set_start_position(&position);
    nodes = 0;
    int success = negamax(&position, 4, 0, -INFINITE_SCORE, INFINITE_SCORE, NULL, &nodes, &result);
    assert(success && result.pv_length == 4 && nodes == 1894);
    check_pv(position, result, 0);
    nodes = 0;
    success = negamax(&position, 4, MAX_SEARCH_PLY - 2,
                      -INFINITE_SCORE, INFINITE_SCORE, NULL, &nodes, &result);
    assert(success && result.pv_length == 2);
    check_pv(position, result, MAX_SEARCH_PLY - 2);
    nodes = 0;
    success = negamax(&position, 4, MAX_SEARCH_PLY,
                      -INFINITE_SCORE, INFINITE_SCORE, NULL, &nodes, &result);
    assert(success && result.pv_length == 0 && nodes == 1);

    position = position_from_fen("7k/P7/8/8/8/8/8/7K", "w", "-");
    check_move_preference(position, "a7a8n");
    position = position_from_fen("7k/8/8/3pP3/8/8/8/K7", "w", "d6");
    check_move_preference(position, "e5d6");
    position = position_from_fen("7k/8/8/8/8/8/8/R3K3", "w", "-");
    position.castling[WHITE_QUEENSIDE] = 1;
    check_move_preference(position, "e1c1");

    /* A preferred quiet move wins a root tie. Black's reply still follows
       ordinary generation order, and a later search with NULL starts fresh. */
    set_start_position(&position);
    struct Move preferred = { 12, 28, EMPTY, -1, -1, -1 }; /* e2e4 */
    nodes = 0;
    success = negamax(&position, 2, 0, -INFINITE_SCORE, INFINITE_SCORE,
                      &preferred, &nodes, &result);
    assert(success && result.score == 0 && result.pv_length == 2);
    expect_move(result, "e2e4");
    char reply[6];
    move_to_string(result.pv[1], reply);
    assert(strcmp(reply, "a7a5") == 0);
    check_pv(position, result, 0);
    nodes = 0;
    success = negamax(&position, 2, 0, -INFINITE_SCORE, INFINITE_SCORE,
                      NULL, &nodes, &result);
    assert(success && result.score == 0 && nodes == 62);
    expect_move(result, "b1a3");

    /* Below the root, 100 halfmoves is a draw even when material is unequal.
       Test the quiescence horizon and the hard ply limit as well. */
    position = position_from_fen("7k/8/8/8/8/8/8/K2Q4", "w", "-");
    for (int clock = 98; clock <= 101; clock++) {
        position.halfmove_clock = clock;
        int expected = clock >= 100 ? 0 : 900;
        result = search_at_horizon(position, 1, -INFINITE_SCORE, INFINITE_SCORE,
                                   expected, &nodes);
        assert(nodes == 1 && result.pv_length == 0);
    }
    position.halfmove_clock = INT_MAX;
    result = search_at_horizon(position, MAX_SEARCH_PLY,
                               -INFINITE_SCORE, INFINITE_SCORE, 0, &nodes);
    assert(nodes == 1 && result.pv_length == 0);

    /* Being in check does not prevent a draw if a legal evasion exists. */
    position = position_from_fen("7k/2p5/5K2/8/8/8/8/7R", "b", "-");
    position.halfmove_clock = 100;
    result = search_at_horizon(position, 1, -600, -500, 0, &nodes);
    assert(nodes == 1 && result.pv_length == 0);

    /* Checkmate still wins over the counter, including at the ply limit. */
    position = position_from_fen("k7/1Q6/2K5/8/8/8/8/8", "b", "-");
    position.halfmove_clock = 100;
    result = search_at_horizon(position, MAX_SEARCH_PLY, -INFINITE_SCORE, INFINITE_SCORE,
                               -MATE_SCORE + MAX_SEARCH_PLY, &nodes);
    assert(nodes == 1 && result.pv_length == 0);

    /* Captures, en passant, and promotions reset the counter before the
       next quiescence node tests for a draw. */
    position = position_from_fen("7k/8/4p3/3Q4/8/8/8/K7", "b", "-");
    position.halfmove_clock = 99;
    result = search_at_horizon(position, 1, -INFINITE_SCORE, INFINITE_SCORE, 100, &nodes);
    expect_move(result, "e6d5");
    position = position_from_fen("7k/8/8/3pP3/8/8/8/K7", "w", "d6");
    position.halfmove_clock = 99; /* Synthetic clock to isolate the reset. */
    result = search_at_horizon(position, 1, -INFINITE_SCORE, INFINITE_SCORE, 100, &nodes);
    expect_move(result, "e5d6");
    position = position_from_fen("7k/4P3/8/8/8/8/8/7K", "w", "-");
    position.halfmove_clock = 99;
    result = search_at_horizon(position, 1, -INFINITE_SCORE, INFINITE_SCORE, 900, &nodes);
    expect_move(result, "e7e8q");

    /* Castling is a king move without a capture, so it does not reset the
       clock. A root preference lets us inspect that drawn line directly. */
    position = position_from_fen("7k/8/8/8/8/8/8/R3K3", "w", "-");
    position.castling[WHITE_QUEENSIDE] = 1;
    position.halfmove_clock = 99;
    struct Move castle = { 4, 2, EMPTY, -1, 0, 3 }; /* e1c1, rook a1d1 */
    nodes = 0;
    success = negamax(&position, 2, 0, -INFINITE_SCORE, INFINITE_SCORE,
                      &castle, &nodes, &result);
    assert(success && result.score == 0 && result.pv_length == 1);
    expect_move(result, "e1c1");
    check_pv(position, result, 0);

    puts("Search checks passed");
    return 0;
}
