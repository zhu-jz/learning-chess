"""Trace reference root windows. Lesson 53 uses --miss-after-checks with --moves."""

from pathlib import Path
import runpy
import sys


if __name__ == "__main__":
    reference = Path(__file__).resolve().parents[1] / "minifish-reference-2026-11-05"
    sys.path.insert(0, str(reference))
    import search
    from uci import uci_move

    original_search = search.search_node
    original_print_pv = search.uci_print_pv
    original_do_move = search.do_move
    original_probe = search.tt_probe
    miss_after_checks = "--miss-after-checks" in sys.argv
    trace_scores = "--root-scores" in sys.argv
    if trace_scores:
        sys.argv.remove("--root-scores")
    checked_positions = {}

    def trace_move(position, move, gives_check):
        ply = position.st.ply
        original_do_move(position, move, gives_check)
        name = uci_move(move, position.chess960)
        print("play", position.nodes, "ply", ply, name, file=sys.stderr)
        # In the Qg7 fixture the halfmove clock is low, so quiescence probes
        # the position key directly. Remember the two Black-to-move positions.
        if miss_after_checks and ply == 0 and gives_check and name in ("g6h5", "g6h6"):
            checked_positions[position.st.key] = name

    def probe_with_two_misses(key):
        hit, entry = original_probe(key)
        if hit and key in checked_positions:
            print("forced-miss", checked_positions[key], "bound", search.tte_bound(entry),
                  "value", search.tte_value(entry), file=sys.stderr)
            return False, entry
        return hit, entry

    def trace_search(position, alpha, beta, depth, cut_node, node_type):
        at_root = position.st.ply == 0 and node_type == search.PV
        if at_root:
            before = position.nodes
        # On the first child's arrival, the root frame still holds the move
        # that search actually played. The node counter includes that move.
        first_child = position.st.ply == 1 and position.nodes == 1
        root_child = trace_scores and position.st.ply == 1
        if first_child or root_child:
            move = position.stack[position.st_idx - 1].currentMove
        score = original_search(position, alpha, beta, depth, cut_node, node_type)
        if root_child:
            print("root-score", uci_move(move, position.chess960), -score, file=sys.stderr)
        if first_child:
            print("first-child", uci_move(move, position.chess960), "root-score", -score,
                  "nodes", position.nodes, file=sys.stderr)
        if at_root:
            print("root-window", depth, alpha, beta, score, before, position.nodes,
                  file=sys.stderr)
            if trace_depth:
                print("worker-seldepth", position.selDepth, "nodes", position.nodes, file=sys.stderr)
        return score

    def trace_print_pv(position, depth, alpha, beta, final=False):
        # The single-PV report reads the depth saved with the leading move.
        leading = position.rootMoves.move[0]
        print("saved-seldepth", leading.selDepth, uci_move(leading.pv[0], position.chess960),
              "depth", depth, file=sys.stderr)
        original_print_pv(position, depth, alpha, beta, final)

    search.search_node = trace_search
    trace_depth = "--seldepth" in sys.argv
    if trace_depth:
        sys.argv.remove("--seldepth")
        search.uci_print_pv = trace_print_pv
    # Extra move-by-move output is optional for larger searches.
    if "--moves" in sys.argv:
        sys.argv.remove("--moves")
        search.do_move = trace_move
    if miss_after_checks:
        sys.argv.remove("--miss-after-checks")
        search.do_move = trace_move
        search.tt_probe = probe_with_two_misses
    runpy.run_path(str(reference / "__main__.py"), run_name="__main__")
