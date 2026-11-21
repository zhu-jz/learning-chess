"""Trace how the two engines report selective depth for the Qg7 mate."""

from pathlib import Path
import sys

from compare_nodes import run


def depth_reports(output):
    reports = []
    for line in output.splitlines():
        if line.startswith("info depth "):
            fields = line.split()
            depth = int(fields[fields.index("depth") + 1])
            nodes = int(fields[fields.index("nodes") + 1])
            seldepth = int(fields[fields.index("seldepth") + 1])
            reports.append((depth, nodes, seldepth))
    return reports


def compare(engine):
    position = "position fen 7k/8/5KQ1/8/8/8/8/8 w - - 0 1\n"
    ours = run([engine], position + "go depth 2\n")
    trace = Path(__file__).with_name("trace_root_window.py")
    theirs = run([sys.executable, "-B", trace, "--seldepth"], position + "bench 1 1 2 current depth\n")

    last_play = None
    rises = []
    for line in ours.stderr.splitlines():
        if line.startswith("play "):
            last_play = line
        elif line.startswith("seldepth-rise "):
            rises.append((last_play, line))
    if rises != [("play 44 ply 0 g6g7", "seldepth-rise 1 nodes 44")]:
        raise AssertionError(f"Unexpected C depth updates: {rises}. Use the trace build.")

    snapshots = [line for line in theirs.stderr.splitlines()
                 if line.startswith(("worker-seldepth ", "saved-seldepth "))]
    if snapshots != [
        "worker-seldepth 0 nodes 1", "worker-seldepth 0 nodes 17", "worker-seldepth 0 nodes 43",
        "saved-seldepth 0 g6g7 depth 1",
        "worker-seldepth 1 nodes 66", "saved-seldepth 1 g6g7 depth 2",
    ]:
        raise AssertionError(f"Unexpected reference depths: {snapshots}")

    c_reports = depth_reports(ours.stdout)
    ref_reports = depth_reports(theirs.stdout)
    if c_reports != ref_reports or ref_reports != [(1, 43, 1), (2, 66, 2)]:
        raise AssertionError(f"Unexpected UCI reports: C={c_reports}, reference={ref_reports}")
    print("Depth   Nodes   C seldepth   Reference seldepth")
    for (depth, nodes, c_depth), (_, _, ref_depth) in zip(c_reports, ref_reports):
        print(f"{depth:5} {nodes:7} {c_depth:12} {ref_depth:19}")
    for play, rise in rises:
        print(f"C: {play} -> {rise}")
    for line in snapshots:
        print(f"Reference: {line}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Usage: python tests/compare_seldepth.py PATH_TO_TRACE_ENGINE")
    compare(Path(sys.argv[1]).resolve())
