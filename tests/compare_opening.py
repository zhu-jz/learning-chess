"""Compare the root moves, scores and retries from the starting position."""

from pathlib import Path
import sys

from compare_nodes import run


def read_attempts(trace):
    attempts = []
    plays = []
    scores = {}
    nodes = 0
    for line in trace.splitlines():
        fields = line.split()
        if line.startswith("play "):
            if fields[3] != "0":
                raise AssertionError(f"Unexpected reply below the opening root: {line}")
            plays.append(fields[4])
        elif line.startswith("root-score "):
            # A scout and its full-window re-search share one played move.
            move = fields[1]
            if not plays or move != plays[-1]:
                raise AssertionError(f"Score without its played move: {line}")
            scores[move] = int(fields[2])
        elif line.startswith("root-window "):
            end = int(fields[-1])
            if list(scores) != plays or end - nodes != len(plays):
                raise AssertionError(f"Root moves do not account for the nodes: {line}")
            alpha, beta, result = map(int, fields[2:5])
            attempts.append((alpha, beta, result, scores))
            plays = []
            scores = {}
            nodes = end
    # Only completed windows belong to the iteration. The reference bench
    # also plays the best move after publishing the UCI node count.
    return attempts


def check_report(output, score, nodes, move):
    reports = [line.split() for line in output.splitlines() if line.startswith("info depth 1 ")]
    if len(reports) != 1:
        raise AssertionError(f"Expected one depth-one UCI report: {output}")
    fields = reports[0]
    start = fields.index("score")
    assert fields[start:start + 3] == ["score", "cp", str(score)]
    assert int(fields[fields.index("nodes") + 1]) == nodes
    assert fields[fields.index("pv") + 1:] == [move]
    assert f"bestmove {move}" in output.splitlines()


def compare(engine):
    position = "position startpos\n"
    ours = run([engine], position + "go depth 1\n")
    trace = Path(__file__).with_name("trace_root_window.py")
    theirs = run([sys.executable, "-B", trace, "--moves", "--root-scores"],
                 position + "bench 1 1 1 current depth\n")
    c_attempts = read_attempts(ours.stderr)
    ref_attempts = read_attempts(theirs.stderr)
    for label, attempts in [("C (cp)", c_attempts), ("Reference (internal units)", ref_attempts)]:
        print(label)
        for alpha, beta, result, scores in attempts:
            print(f"  [{alpha}, {beta}] returned {result} after {len(scores)} moves")
            print("    " + "  ".join(f"{move} {score:+d}" for move, score in scores.items()))

    if len(c_attempts) != 1:
        raise AssertionError("Expected one C attempt. Use the build with -DTRACE_SEARCH_MOVES.")
    alpha, beta, result, c_scores = c_attempts[0]
    assert (alpha, beta, result) == (-9, 9, 0)
    assert len(c_scores) == 20 and set(c_scores.values()) == {0}

    expected = [(-19, 19, 28, 4), (-19, 47, 90, 2), (-19, 115, 150, 9), (-19, 183, 182, 20)]
    assert [(a, b, r, len(scores)) for a, b, r, scores in ref_attempts] == expected
    first = ref_attempts[0][3]
    assert list(first) == list(c_scores)[:4]
    assert list(first.items()) == [("a2a3", -67), ("b2b3", -42), ("c2c3", -49), ("d2d3", 28)]
    for attempt, move in zip(ref_attempts, ["d2d3", "e2e3", "d2d4", "e2e4"]):
        alpha, beta, result, scores = attempt
        assert scores[move] == result == max(scores.values())
        if result >= beta:
            assert list(scores)[-1] == move
    assert ref_attempts[-1][3].keys() == c_scores.keys()
    check_report(ours.stdout, 0, 20, "a2a3")
    check_report(theirs.stdout, 87, 35, "e2e4")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Usage: python tests/compare_opening.py PATH_TO_TRACE_ENGINE")
    compare(Path(sys.argv[1]).resolve())
