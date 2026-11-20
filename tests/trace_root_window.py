"""Run the reference UCI engine and print each root search window to stderr."""

from pathlib import Path
import runpy
import sys


if __name__ == "__main__":
    reference = Path(__file__).resolve().parents[1] / "minifish-reference-2026-11-05"
    sys.path.insert(0, str(reference))
    import search

    original_search = search.search_node

    def trace_search(position, alpha, beta, depth, cut_node, node_type):
        at_root = position.st.ply == 0 and node_type == search.PV
        if at_root:
            before = position.nodes
        score = original_search(position, alpha, beta, depth, cut_node, node_type)
        if at_root:
            print("root-window", depth, alpha, beta, score, before, position.nodes,
                  file=sys.stderr)
        return score

    search.search_node = trace_search
    runpy.run_path(str(reference / "__main__.py"), run_name="__main__")
