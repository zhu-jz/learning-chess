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
                                            struct SearchStats *stats)
{
    struct SearchResult result;
    stats->nodes = 0;
    stats->selective_depth = 0;
    int success = negamax(&position, 0, ply, alpha, beta, NULL, NULL, stats, &result);
    assert(success);
    assert(result.score == expected_score);
    assert(stats->selective_depth >= ply + result.pv_length);
    assert(stats->selective_depth <= MAX_SEARCH_PLY);
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

static void test_repetition(void)
{
    struct Position a = position_from_fen("4r2k/8/8/8/8/8/8/KN6", "w", "-");
    struct Position b = a;
    b.halfmove_clock = 17;
    b.fullmove_number = 42;
    assert(same_repetition_position(&a, &b));
    b.side_to_move = BLACK;
    assert(!same_repetition_position(&a, &b));
    b = a;
    b.castling[WHITE_KINGSIDE] = 1;
    assert(!same_repetition_position(&a, &b));
    b = a;
    b.board[1] = WHITE_BISHOP;
    assert(!same_repetition_position(&a, &b));
    b = a;
    b.en_passant_square = 43; /* d6, with no attacking pawn */
    assert(same_repetition_position(&a, &b));
    a = position_from_fen("k3r3/8/8/3pP3/8/8/8/4K3", "w", "d6");
    b = a;
    b.en_passant_square = -1;
    assert(!same_repetition_position(&a, &b));
    /* The reference retains this EP key even though e5d6 exposes the king. */
    assert(!play_uci_move(&b, "e5d6"));
    b = a;
    assert(!play_uci_move(&b, "e5d6"));
    a = position_from_fen("7k/8/8/8/3Pp3/8/8/K7", "b", "d3");
    b = a;
    b.en_passant_square = -1;
    assert(!same_repetition_position(&a, &b));

    struct PositionHistory history;
    history.count = 0;
    a = position_from_fen("4r2k/8/8/8/8/8/8/KN6", "w", "-");
    remember_position(&history, &a);
    prepare_repetition_history(&history);
    const char *cycle[4] = { "b1a3", "h8g8", "a3b1", "g8h8" };
    int success;
    /* Model the search path: a unique root is not yet eligible, but a
       position first reached below it ends a cycle on its next visit. */
    for (int i = 0; i < 5; i++) {
        success = play_uci_move(&a, cycle[i % 4]);
        assert(success);
        history.positions[history.count] = a;
        history.can_repeat[history.count] = 1;
        history.count++;
        assert(is_repetition(&a, &history) == (i == 4));
    }
    struct SearchStats stats = { 0, 0 };
    struct SearchResult result;
    success = negamax(&a, 0, 5, -INFINITE_SCORE, INFINITE_SCORE,
                      NULL, &history, &stats, &result);
    assert(success && result.score == 0 && result.pv_length == 0 && stats.nodes == 1);
    assert(stats.selective_depth == 5);
    assert(history.count == 6);
    a.halfmove_clock = 0;
    assert(!is_repetition(&a, &history));

    /* Game history requires an earlier occurrence before a row is eligible. */
    history.count = 0;
    a = position_from_fen("4r2k/8/8/8/8/8/8/KN6", "w", "-");
    remember_position(&history, &a);
    for (int i = 0; i < 8; i++) {
        success = play_uci_move(&a, cycle[i % 4]);
        assert(success);
        remember_position(&history, &a);
    }
    prepare_repetition_history(&history);
    for (int i = 0; i < history.count; i++)
        assert(history.can_repeat[i] == (i >= 4));
    stats.nodes = 0;
    stats.selective_depth = 0;
    success = negamax(&a, 1, 0, -INFINITE_SCORE, INFINITE_SCORE,
                      NULL, &history, &stats, &result);
    assert(success && result.score == 0 && history.count == 9);
    expect_move(result, "b1a3");
    /* A failed child must also pop its history entry. */
    stats.nodes = ULLONG_MAX - 1;
    success = negamax(&a, 1, 0, -INFINITE_SCORE, INFINITE_SCORE,
                      NULL, &history, &stats, &result);
    assert(!success && history.count == 9);

    /* Keep only the last 100 game positions, then reset on a capture. */
    for (int i = 0; i < 112; i++) {
        success = play_uci_move(&a, cycle[i % 4]);
        assert(success);
        remember_position(&history, &a);
    }
    assert(history.count == GAME_HISTORY_SIZE);
    assert(history.positions[0].halfmove_clock == 21);
    assert(history.positions[99].halfmove_clock == 120);
    a = position_from_fen("7k/8/8/8/8/2n5/8/KN6", "w", "-");
    success = play_uci_move(&a, "b1c3");
    assert(success);
    remember_position(&history, &a);
    assert(history.count == 1 && history.positions[0].halfmove_clock == 0);

    /* Exercise the last two search slots and restore the count after return. */
    set_start_position(&a);
    history.count = GAME_HISTORY_SIZE + MAX_SEARCH_PLY - 2;
    for (int i = 0; i < history.count; i++) {
        history.positions[i] = a;
        history.can_repeat[i] = 0;
    }
    stats.nodes = 0;
    stats.selective_depth = 0;
    success = negamax(&a, 2, MAX_SEARCH_PLY - 2, -INFINITE_SCORE, INFINITE_SCORE,
                      NULL, &history, &stats, &result);
    assert(success && result.pv_length == 2);
    assert(history.count == GAME_HISTORY_SIZE + MAX_SEARCH_PLY - 2);
}

static void test_selective_depth(void)
{
    struct SearchStats stats = { 0, 0 };
    struct SearchResult result;
    struct Position position = position_from_fen("7k/8/4p3/3r4/8/8/8/K2Q4", "w", "-");

    /* Stand pat wins, but the rejected capture line still contributes its
       deepest node: Qxd5 exd5. The PV alone cannot tell us this depth. */
    int success = negamax(&position, 0, 0, -INFINITE_SCORE, INFINITE_SCORE,
                          NULL, NULL, &stats, &result);
    assert(success && result.pv_length == 0 && stats.nodes == 3);
    assert(stats.selective_depth == 2);

    /* An iteration resets depth but keeps the accumulated node count.
       Here the new search ends immediately at the stand-pat cutoff. */
    stats.selective_depth = 0;
    success = negamax(&position, 0, 0, 0, 200, NULL, NULL, &stats, &result);
    assert(success && stats.nodes == 4 && stats.selective_depth == 0);

    /* Starting near the cap exercises the final legal array boundary. */
    position = position_from_fen("7k/2p5/5K2/8/8/8/8/7R", "b", "-");
    result = search_at_horizon(position, MAX_SEARCH_PLY - 1,
                               -INFINITE_SCORE, INFINITE_SCORE, -400, &stats);
    assert(stats.nodes == 2 && stats.selective_depth == MAX_SEARCH_PLY);
    result = search_at_horizon(position, MAX_SEARCH_PLY,
                               -INFINITE_SCORE, INFINITE_SCORE, -400, &stats);
    assert(stats.nodes == 1 && stats.selective_depth == MAX_SEARCH_PLY);

    /* Terminal leaves and rule draws also count their arrival ply. */
    position = position_from_fen("k7/1Q6/2K5/8/8/8/8/8", "b", "-");
    result = search_at_horizon(position, 7, -INFINITE_SCORE, INFINITE_SCORE,
                               -MATE_SCORE + 7, &stats);
    assert(stats.nodes == 1 && stats.selective_depth == 7);
    position = position_from_fen("7k/8/8/8/8/8/8/K2Q4", "w", "-");
    position.halfmove_clock = 100;
    result = search_at_horizon(position, 5, -INFINITE_SCORE, INFINITE_SCORE, 0, &stats);
    assert(stats.nodes == 1 && stats.selective_depth == 5);

    /* A node refused by the counter overflow guard was never visited. */
    stats.nodes = ULLONG_MAX;
    stats.selective_depth = 0;
    success = negamax(&position, 0, 5, -INFINITE_SCORE, INFINITE_SCORE,
                      NULL, NULL, &stats, &result);
    assert(!success && stats.nodes == ULLONG_MAX && stats.selective_depth == 0);
}

static void test_principal_variation_search(void)
{
    struct Position position = position_from_fen("7k/8/4p3/3r4/8/8/8/K2Q4", "w", "-");
    struct PositionHistory history;
    history.count = 0;
    remember_position(&history, &position);
    prepare_repetition_history(&history);
    struct SearchStats stats = { 0, 0 };
    struct SearchResult result;

    /* Qxd5 is searched first and loses to exd5. Later, Qb1 beats that
       score: its scout uses node 13, and its full re-search uses node 14. */
    int success = negamax(&position, 1, 0, -INFINITE_SCORE, INFINITE_SCORE,
                          NULL, &history, &stats, &result);
    assert(success && result.score == 300 && stats.nodes == 38);
    expect_move(result, "d1b1");
    check_pv(position, result, 0);
    assert(history.count == 1);
    assert(same_repetition_position(&position, &history.positions[0]));
    assert(history.can_repeat[0] == 0);

    /* When the scout already reaches beta, its bound proves a cutoff.
       There is no need to pay for node 14 or the remaining moves. */
    stats.nodes = 0;
    stats.selective_depth = 0;
    success = negamax(&position, 1, 0, -INFINITE_SCORE, 200,
                      NULL, &history, &stats, &result);
    assert(success && result.score >= 200 && stats.nodes == 13);
    expect_move(result, "d1b1");
    assert(history.count == 1);

    /* An already narrow window needs no extra scout. A fail-low result
       is an upper bound; it need not give the exact score or best line. */
    stats.nodes = 0;
    stats.selective_depth = 0;
    success = negamax(&position, 1, 0, 300, 301,
                      NULL, &history, &stats, &result);
    assert(success && result.score <= 300 && stats.nodes == 31);
    assert(history.count == 1);

    /* Refuse either the scout's node 13 or the full re-search's node 14.
       Both error paths must remove the child from repetition history. */
    for (int allowed = 12; allowed <= 13; allowed++) {
        stats.nodes = ULLONG_MAX - allowed;
        stats.selective_depth = 0;
        success = negamax(&position, 1, 0, -INFINITE_SCORE, INFINITE_SCORE,
                          NULL, &history, &stats, &result);
        assert(!success && stats.nodes == ULLONG_MAX && history.count == 1);
        assert(same_repetition_position(&position, &history.positions[0]));
    }
}

int main(void)
{
    struct SearchStats stats = { 0, 0 };
    struct Position position;
    struct SearchResult result;

    /* A quiet horizon counts once, with no extra negamax-to-quiescence node. */
    set_start_position(&position);
    result = search_at_horizon(position, 0, -INFINITE_SCORE, INFINITE_SCORE, 0, &stats);
    assert(stats.nodes == 1 && result.pv_length == 0);

    /* Stand pat can reject a losing capture, and can also cut off directly. */
    position = position_from_fen("7k/8/4p3/3r4/8/8/8/K2Q4", "w", "-");
    result = search_at_horizon(position, 0, -INFINITE_SCORE, INFINITE_SCORE, 300, &stats);
    assert(stats.nodes == 3 && result.pv_length == 0);
    result = search_at_horizon(position, 0, 0, 200, 300, &stats);
    assert(stats.nodes == 1 && result.pv_length == 0);

    /* After Qxd5, Black must see exd5 instead of accepting the lost rook. */
    position = position_from_fen("7k/8/4p3/3Q4/8/8/8/K7", "b", "-");
    result = search_at_horizon(position, 0, -INFINITE_SCORE, INFINITE_SCORE, 100, &stats);
    expect_move(result, "e6d5");
    assert(stats.nodes == 2);

    position = position_from_fen("7k/8/8/3pP3/8/8/8/K7", "w", "d6");
    result = search_at_horizon(position, 0, -INFINITE_SCORE, INFINITE_SCORE, 100, &stats);
    expect_move(result, "e5d6");
    assert(stats.nodes == 2);

    /* A non-capturing promotion belongs in quiescence, for either color. */
    position = position_from_fen("7k/4P3/8/8/8/8/8/7K", "w", "-");
    result = search_at_horizon(position, 0, -INFINITE_SCORE, INFINITE_SCORE, 900, &stats);
    expect_move(result, "e7e8q");
    position = position_from_fen("7k/8/8/8/8/8/4p3/7K", "b", "-");
    result = search_at_horizon(position, 0, -INFINITE_SCORE, INFINITE_SCORE, 900, &stats);
    expect_move(result, "e2e1q");

    /* Queening stalemates; retaining all promotion choices finds a rook. */
    position = position_from_fen("8/k1P5/2K5/8/8/8/8/8", "w", "-");
    result = search_at_horizon(position, 0, -INFINITE_SCORE, INFINITE_SCORE, 500, &stats);
    expect_move(result, "c7c8r");

    /* Only Kg8 evades check. Even a favorable stand-pat cutoff is forbidden. */
    position = position_from_fen("7k/2p5/5K2/8/8/8/8/7R", "b", "-");
    result = search_at_horizon(position, 0, -600, -500, -400, &stats);
    expect_move(result, "h8g8");
    assert(stats.nodes == 2);

    /* The last available ply is searched; at the limit we approximate.
       This fallback does not claim that standing pat in check is legal. */
    result = search_at_horizon(position, MAX_SEARCH_PLY - 1,
                               -INFINITE_SCORE, INFINITE_SCORE, -400, &stats);
    expect_move(result, "h8g8");
    assert(stats.nodes == 2);
    result = search_at_horizon(position, MAX_SEARCH_PLY,
                               -INFINITE_SCORE, INFINITE_SCORE, -400, &stats);
    assert(stats.nodes == 1 && result.pv_length == 0);

    /* Terminal detection must precede both stand pat and the recursion cap. */
    position = position_from_fen("k7/8/1QK5/8/8/8/8/8", "b", "-");
    result = search_at_horizon(position, 0, -2000, -1000, 0, &stats);
    assert(stats.nodes == 1 && result.pv_length == 0);
    result = search_at_horizon(position, MAX_SEARCH_PLY, -2000, -1000, 0, &stats);
    assert(stats.nodes == 1 && result.pv_length == 0);
    position = position_from_fen("k7/1Q6/2K5/8/8/8/8/8", "b", "-");
    result = search_at_horizon(position, MAX_SEARCH_PLY, -INFINITE_SCORE, INFINITE_SCORE,
                               -MATE_SCORE + MAX_SEARCH_PLY, &stats);
    assert(stats.nodes == 1 && result.pv_length == 0);

    /* Qxg7 mates during quiescence, beyond the normal maximum depth. */
    position = position_from_fen("7k/6r1/5KQ1/8/8/8/8/8", "w", "-");
    result = search_at_horizon(position, 4, -INFINITE_SCORE, INFINITE_SCORE,
                               MATE_SCORE - 5, &stats);
    expect_move(result, "g6g7");

    /* Ordinary search builds a full line. Near the recursion limit only
       the remaining plies fit, and reusing the result must discard old moves. */
    set_start_position(&position);
    stats.nodes = 0;
    stats.selective_depth = 0;
    int success = negamax(&position, 4, 0, -INFINITE_SCORE, INFINITE_SCORE, NULL, NULL, &stats, &result);
    assert(success && result.pv_length == 4 && stats.nodes == 1882);
    check_pv(position, result, 0);
    stats.nodes = 0;
    stats.selective_depth = 0;
    success = negamax(&position, 4, MAX_SEARCH_PLY - 2,
                      -INFINITE_SCORE, INFINITE_SCORE, NULL, NULL, &stats, &result);
    assert(success && result.pv_length == 2);
    check_pv(position, result, MAX_SEARCH_PLY - 2);
    stats.nodes = 0;
    stats.selective_depth = 0;
    success = negamax(&position, 4, MAX_SEARCH_PLY,
                      -INFINITE_SCORE, INFINITE_SCORE, NULL, NULL, &stats, &result);
    assert(success && result.pv_length == 0 && stats.nodes == 1);

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
    stats.nodes = 0;
    stats.selective_depth = 0;
    success = negamax(&position, 2, 0, -INFINITE_SCORE, INFINITE_SCORE,
                      &preferred, NULL, &stats, &result);
    assert(success && result.score == 0 && result.pv_length == 2);
    expect_move(result, "e2e4");
    char reply[6];
    move_to_string(result.pv[1], reply);
    assert(strcmp(reply, "a7a5") == 0);
    check_pv(position, result, 0);
    stats.nodes = 0;
    stats.selective_depth = 0;
    success = negamax(&position, 2, 0, -INFINITE_SCORE, INFINITE_SCORE,
                      NULL, NULL, &stats, &result);
    assert(success && result.score == 0 && stats.nodes == 62);
    expect_move(result, "b1a3");

    /* Below the root, 100 halfmoves is a draw even when material is unequal.
       Test the quiescence horizon and the hard ply limit as well. */
    position = position_from_fen("7k/8/8/8/8/8/8/K2Q4", "w", "-");
    for (int clock = 98; clock <= 101; clock++) {
        position.halfmove_clock = clock;
        int expected = clock >= 100 ? 0 : 900;
        result = search_at_horizon(position, 1, -INFINITE_SCORE, INFINITE_SCORE,
                                   expected, &stats);
        assert(stats.nodes == 1 && result.pv_length == 0);
    }
    position.halfmove_clock = INT_MAX;
    result = search_at_horizon(position, MAX_SEARCH_PLY,
                               -INFINITE_SCORE, INFINITE_SCORE, 0, &stats);
    assert(stats.nodes == 1 && result.pv_length == 0);

    /* Being in check does not prevent a draw if a legal evasion exists. */
    position = position_from_fen("7k/2p5/5K2/8/8/8/8/7R", "b", "-");
    position.halfmove_clock = 100;
    result = search_at_horizon(position, 1, -600, -500, 0, &stats);
    assert(stats.nodes == 1 && result.pv_length == 0);

    /* Checkmate still wins over the counter, including at the ply limit. */
    position = position_from_fen("k7/1Q6/2K5/8/8/8/8/8", "b", "-");
    position.halfmove_clock = 100;
    result = search_at_horizon(position, MAX_SEARCH_PLY, -INFINITE_SCORE, INFINITE_SCORE,
                               -MATE_SCORE + MAX_SEARCH_PLY, &stats);
    assert(stats.nodes == 1 && result.pv_length == 0);

    /* Captures, en passant, and promotions reset the counter before the
       next quiescence node tests for a draw. */
    position = position_from_fen("7k/8/4p3/3Q4/8/8/8/K7", "b", "-");
    position.halfmove_clock = 99;
    result = search_at_horizon(position, 1, -INFINITE_SCORE, INFINITE_SCORE, 100, &stats);
    expect_move(result, "e6d5");
    position = position_from_fen("7k/8/8/3pP3/8/8/8/K7", "w", "d6");
    position.halfmove_clock = 99; /* Synthetic clock to isolate the reset. */
    result = search_at_horizon(position, 1, -INFINITE_SCORE, INFINITE_SCORE, 100, &stats);
    expect_move(result, "e5d6");
    position = position_from_fen("7k/4P3/8/8/8/8/8/7K", "w", "-");
    position.halfmove_clock = 99;
    result = search_at_horizon(position, 1, -INFINITE_SCORE, INFINITE_SCORE, 900, &stats);
    expect_move(result, "e7e8q");

    /* Castling is a king move without a capture, so it does not reset the
       clock. A root preference lets us inspect that drawn line directly. */
    position = position_from_fen("7k/8/8/8/8/8/8/R3K3", "w", "-");
    position.castling[WHITE_QUEENSIDE] = 1;
    position.halfmove_clock = 99;
    struct Move castle = { 4, 2, EMPTY, -1, 0, 3 }; /* e1c1, rook a1d1 */
    stats.nodes = 0;
    stats.selective_depth = 0;
    success = negamax(&position, 2, 0, -INFINITE_SCORE, INFINITE_SCORE,
                      &castle, NULL, &stats, &result);
    assert(success && result.score == 0 && result.pv_length == 1);
    expect_move(result, "e1c1");
    check_pv(position, result, 0);

    test_repetition();
    test_selective_depth();
    test_principal_variation_search();
    puts("Search checks passed");
    return 0;
}
