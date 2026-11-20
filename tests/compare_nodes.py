"""Compare depth-one node reports and trace the reference's forced Ka2 attempts."""

from pathlib import Path
import subprocess
import sys


def run(command, commands):
    # A fresh process for each clock keeps hash entries and previous scores
    # from the other search out of this experiment.
    return subprocess.run(command, input=commands + "quit\n", text=True,
                          capture_output=True, check=True, timeout=60)


def depth_one_nodes(output):
    reports = [line.split() for line in output.splitlines() if line.startswith("info depth 1 ")]
    if not reports or "bestmove a1a2" not in output.splitlines():
        raise AssertionError("Expected a completed depth-one Ka2 search:\n" + output)
    last = reports[-1]
    return int(last[last.index("nodes") + 1])


def compare(engine):
    root = Path(__file__).resolve().parents[1]
    reference = root / "minifish-reference-2026-11-05" / "__main__.py"
    if not reference.is_file():
        sys.exit(f"Missing reference snapshot: {reference}")

    print("Halfmove clock   C info nodes   Reference info nodes   Reference bench total")
    # Each row records depth, alpha, beta, score, nodes before, nodes after.
    windows = {
        99: [(1, -19, 19, 0, 0, 1)],
        0: [(1, -19, 19, -395, 0, 1), (1, -414, 0, -395, 1, 2)],
    }
    trace = Path(__file__).with_name("trace_root_window.py")
    for clock, expected in windows.items():
        position = f"position fen 8/7p/8/8/8/8/2k5/K7 w - - {clock} 1\n"
        ours = run([engine], position + "moves\ngo depth 1\n")
        if "info string moves legal a1a2" not in ours.stdout.splitlines():
            raise AssertionError("Ka2 must be the only legal move:\n" + ours.stdout)

        # bench waits for the worker before accepting quit. It prints both
        # the UCI iteration report and a later total, taken after bestmove.
        theirs = run([sys.executable, "-B", trace], position + "bench 1 1 1 current depth\n")
        attempts = [tuple(map(int, line.split()[1:])) for line in theirs.stderr.splitlines()
                    if line.startswith("root-window ")]
        if attempts != expected:
            raise AssertionError(f"Clock {clock}: expected {expected}, observed {attempts}")
        expected_reference = len(expected)  # Each attempt plays only Ka2.
        c_nodes = depth_one_nodes(ours.stdout)
        ref_nodes = depth_one_nodes(theirs.stdout)
        totals = [int(line.split(":")[1]) for line in theirs.stderr.splitlines()
                  if line.startswith("Nodes searched")]
        if c_nodes != ref_nodes or (ref_nodes, totals) != (expected_reference, [expected_reference + 1]):
            raise AssertionError(f"Clock {clock}: C={c_nodes}, reference={ref_nodes}, bench={totals}")
        print(f"{clock:13} {c_nodes:14} {ref_nodes:22} {totals[0]:23}")
        for depth, alpha, beta, score, before, after in attempts:
            print(f"  depth {depth}   window [{alpha}, {beta}]   returned {score}"
                  f"   nodes {before} -> {after}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Usage: python tests/compare_nodes.py PATH_TO_C_ENGINE")
    compare(Path(sys.argv[1]).resolve())
