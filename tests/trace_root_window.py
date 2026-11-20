"""Trace the reference's opening move and each root search window to stderr."""

from pathlib import Path
import runpy
import sys


if __name__ == "__main__":
    reference = Path(__file__).resolve().parents[1] / "minifish-reference-2026-11-05"
    sys.path.insert(0, str(reference))
    import search
    from uci import uci_move

    original_search = search.search_node

    def trace_search(position, alpha, beta, depth, cut_node, node_type):
        at_root = position.st.ply == 0 and node_type == search.PV
        if at_root:
            before = position.nodes
        # On the first child's arrival, the root frame still holds the move
        # that search actually played. The node counter includes that move.
        first_child = position.st.ply == 1 and position.nodes == 1
        if first_child:
            move = position.stack[position.st_idx - 1].currentMove
        score = original_search(position, alpha, beta, depth, cut_node, node_type)
        if first_child:
            print("first-child", uci_move(move, position.chess960), "root-score", -score,
                  "nodes", position.nodes, file=sys.stderr)
        if at_root:
            print("root-window", depth, alpha, beta, score, before, position.nodes,
                  file=sys.stderr)
        return score

    search.search_node = trace_search
    runpy.run_path(str(reference / "__main__.py"), run_name="__main__")
