"""Compare the C engine's see command with the November 5 reference."""

from pathlib import Path
import subprocess
import sys

from see_cases import SEE_CASES


def teaching_score(engine, fen, move):
    commands = f"position fen {fen}\nsee {move}\nquit\n"
    result = subprocess.run([engine], input=commands, text=True, capture_output=True,
                            timeout=10, check=True)
    prefix = "info string see cp "
    if not result.stdout.startswith(prefix):
        raise ValueError(f"{move}: expected a SEE score, got {result.stdout!r}")
    return int(result.stdout[len(prefix):])


def compare(engine):
    reference = Path(__file__).resolve().parents[1] / "minifish-reference-2026-11-05"
    if not (reference / "position.py").is_file():
        sys.exit(f"Missing reference snapshot: {reference}")
    sys.path.insert(0, str(reference))
    from nnue import nnue_init
    from position import Position, see_test
    from uci import uci_to_move

    nnue_init()
    print(f"{'exchange':23} {'C score':>8} {'ref bound':>10}   ref at C score / score+1")
    for name, board, ep, move, expected, boundary in SEE_CASES:
        fen = f"{board} w - {ep} 0 1"
        score = teaching_score(engine, fen, move)
        if score != expected:
            raise AssertionError(f"{name}: expected C score {expected}, got {score}")

        position = Position(search_worker=False)
        position.set(fen)
        capture = uci_to_move(position, move)
        if not capture:
            raise ValueError(f"{name}: reference could not read {move}")

        # Probe both scales. Equality should pass at the reference boundary,
        # and the very next integer should fail.
        for threshold in (score, score + 1, boundary, boundary + 1):
            passes = see_test(position, capture, threshold)
            if passes != (threshold <= boundary):
                raise AssertionError(f"{name}: reference returned {passes} at {threshold}, "
                                     f"expected boundary {boundary}")
        at_score = see_test(position, capture, score)
        above_score = see_test(position, capture, score + 1)
        print(f"{name:23} {score:8} {boundary:10}   {at_score} / {above_score}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Usage: python tests/compare_see.py PATH_TO_ENGINE")
    compare(Path(sys.argv[1]).resolve())
