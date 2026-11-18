"""Compare the C engine's see command with the November 5 reference."""

from pathlib import Path
import subprocess
import sys

from see_cases import SEE_CASES


def engine_replies(engine, fen, commands):
    commands = f"position fen {fen}\n" + "\n".join(commands) + "\nquit\n"
    result = subprocess.run([engine], input=commands, text=True, capture_output=True,
                            timeout=10, check=True)
    return result.stdout.splitlines()


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
        thresholds = [expected, expected + 1, boundary - 1, boundary, boundary + 1, 0]
        commands = [f"see {move}"] + [f"see_test {move} {t}" for t in thresholds]
        replies = engine_replies(engine, fen, commands)
        if len(replies) != len(commands) or replies[0] != f"info string see cp {expected}":
            raise AssertionError(f"{name}: unexpected C output: {replies}")

        position = Position(search_worker=False)
        position.set(fen)
        capture = uci_to_move(position, move)
        if not capture:
            raise ValueError(f"{name}: reference could not read {move}")

        # Probe both scales. Equality should pass at the reference boundary,
        # and the very next integer should fail.
        for threshold, reply in zip(thresholds, replies[1:]):
            passes = see_test(position, capture, threshold)
            answer = "yes" if passes else "no"
            if reply != f"info string see_test {answer}":
                raise AssertionError(f"{name} at {threshold}: C said {reply!r}, reference says {answer}")
            if passes != (threshold <= boundary):
                raise AssertionError(f"{name}: reference returned {passes} at {threshold}, "
                                     f"expected boundary {boundary}")
        at_score = see_test(position, capture, expected)
        above_score = see_test(position, capture, expected + 1)
        print(f"{name:23} {expected:8} {boundary:10}   {at_score} / {above_score}")
    print(f"C and reference threshold answers agree on all {len(SEE_CASES)} fixtures.")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Usage: python tests/compare_see.py PATH_TO_ENGINE")
    compare(Path(sys.argv[1]).resolve())
