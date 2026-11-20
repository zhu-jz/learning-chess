"""Replay the second depth-one attempt in the Qg7 mate position."""

from pathlib import Path
import sys

from compare_nodes import run


def second_attempt(output):
    moves = []
    completed = 0
    start_nodes = 0
    for line in output.splitlines():
        fields = line.split()
        if line.startswith("root-window "):
            completed += 1
            end_nodes = int(fields[-1])
            if completed == 2:
                if len(moves) != end_nodes - start_nodes:
                    raise AssertionError("Played moves must account for every node in the retry")
                return moves
            start_nodes = end_nodes
        elif completed == 1 and line.startswith("play "):
            moves.append((int(fields[3]), fields[4]))
    raise AssertionError("Missing second root window. Build the C engine with -DTRACE_SEARCH_MOVES.")


def compare(engine):
    position = "position fen 7k/8/5KQ1/8/8/8/8/8 w - - 0 1\n"
    reference = Path(__file__).with_name("trace_root_window.py")
    ours = run([engine], position + "go depth 1\n")
    theirs = run([sys.executable, "-B", reference, "--moves"],
                 position + "bench 1 1 1 current depth\n")
    actual = second_attempt(ours.stderr)
    shared = second_attempt(theirs.stderr)

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
    for output in (ours.stdout, theirs.stdout):
        if "bestmove g6g7" not in output.splitlines():
            raise AssertionError("Expected both completed searches to choose Qg7")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Usage: python tests/compare_retry_moves.py PATH_TO_TRACE_ENGINE")
    compare(Path(sys.argv[1]).resolve())
