"""Replay both depth-one retries in the Qg7 mate position."""

from pathlib import Path
import sys

from compare_nodes import run


def attempt_moves(output, number):
    moves = []
    completed = 0
    start_nodes = 0
    for line in output.splitlines():
        fields = line.split()
        if line.startswith("root-window "):
            completed += 1
            end_nodes = int(fields[-1])
            if completed == number:
                if len(moves) != end_nodes - start_nodes:
                    raise AssertionError("Played moves must account for every node in the retry")
                return moves
            start_nodes = end_nodes
        elif completed == number - 1 and line.startswith("play "):
            moves.append((int(fields[3]), fields[4]))
    raise AssertionError(f"Missing root window {number}. Build the C engine with -DTRACE_SEARCH_MOVES.")


def check_third_attempt(ours, theirs, control):
    actual = attempt_moves(ours.stderr, 3)
    reference = attempt_moves(theirs.stderr, 3)
    expected = []
    for event in reference:
        expected.append(event)
        if event in [(0, "g6h5"), (0, "g6h6")]:
            expected.append((1, "h8g8"))
    if len(reference) != 26 or actual != reference:
        raise AssertionError("Expected all 26 moves to match on the third attempt")

    # Ignore the two table hits in a fresh reference search. The experiment
    # should restore the two evasions while preserving the earlier searches.
    misses = [line for line in control.stderr.splitlines() if line.startswith("forced-miss ")]
    if misses != ["forced-miss g6h5 bound 2 value -1946", "forced-miss g6h6 bound 2 value -1884"]:
        raise AssertionError(f"Unexpected cached bounds: {misses}")
    for number in (1, 2):
        if attempt_moves(control.stderr, number) != attempt_moves(theirs.stderr, number):
            raise AssertionError(f"Control changed attempt {number}")
    if len(expected) != 28 or attempt_moves(control.stderr, 3) != expected:
        raise AssertionError("Expected a forced miss to add Kg8 after each queen check")
    print("Third attempt: C 26 nodes, reference 26, reference with two misses 28.")


def compare(engine):
    position = "position fen 7k/8/5KQ1/8/8/8/8/8 w - - 0 1\n"
    reference = Path(__file__).with_name("trace_root_window.py")
    ours = run([engine], position + "go depth 1\n")
    trace = [sys.executable, "-B", reference, "--moves"]
    bench = position + "bench 1 1 1 current depth\n"
    theirs = run(trace, bench)
    control = run(trace + ["--miss-after-checks"], bench)
    if attempt_moves(ours.stderr, 1) != [(0, "g6b1")] or attempt_moves(theirs.stderr, 1) != [(0, "g6b1")]:
        raise AssertionError("Expected both first attempts to play only Qb1")
    actual = attempt_moves(ours.stderr, 2)
    shared = attempt_moves(theirs.stderr, 2)

    for label, moves in [("C", actual), ("Reference", shared)]:
        print(f"{label} second attempt ({len(moves)} nodes)")
        for ply, move in moves:
            print("  " * (ply + 1) + move)

    if len(shared) != 16 or shared[0] != (0, "g6b1") or shared[-1] != (0, "g6g7"):
        raise AssertionError("Expected the reference to reach Qg7 on its sixteenth node")
    replies = [move for ply, move in shared if ply > 0]
    if replies != ["h8g8", "h8g8"]:
        raise AssertionError(f"Unexpected replies in the reference retry: {replies}")
    if actual != shared:
        raise AssertionError("Expected the same moves at the same plies throughout the retry")
    check_third_attempt(ours, theirs, control)
    for output in (ours.stdout, theirs.stdout):
        if not any("score mate 1 nodes 43" in line and line.endswith(" pv g6g7")
                   for line in output.splitlines() if line.startswith("info depth 1 ")):
            raise AssertionError("Expected mate in one, 43 nodes and the Qg7 principal variation")
    for output in (ours.stdout, theirs.stdout, control.stdout):
        if "bestmove g6g7" not in output.splitlines():
            raise AssertionError("Expected every completed search to choose Qg7")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Usage: python tests/compare_retry_moves.py PATH_TO_TRACE_ENGINE")
    compare(Path(sys.argv[1]).resolve())
