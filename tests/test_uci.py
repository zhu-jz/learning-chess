"""Exercise the engine as a GUI would, without third-party packages."""

from pathlib import Path
from queue import Empty, Queue
import subprocess
import sys
from threading import Thread
import unittest


ENGINE = Path(sys.argv.pop(1)).resolve()
TIMEOUT = 5

START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
START_POSITION = """\
8 r n b q k b n r
7 p p p p p p p p
6 . . . . . . . .
5 . . . . . . . .
4 . . . . . . . .
3 . . . . . . . .
2 P P P P P P P P
1 R N B Q K B N R
  a b c d e f g h
Side to move: White
Castling: KQkq
En passant: -
Halfmove clock: 0
Fullmove number: 1""".splitlines()

SAMPLE_FEN = "r3k2r/8/8/3pP3/8/8/8/R3K2R w Kq d6 17 42"
SAMPLE_POSITION = """\
8 r . . . k . . r
7 . . . . . . . .
6 . . . . . . . .
5 . . . p P . . .
4 . . . . . . . .
3 . . . . . . . .
2 . . . . . . . .
1 R . . . K . . R
  a b c d e f g h
Side to move: White
Castling: Kq
En passant: d6
Halfmove clock: 17
Fullmove number: 42""".splitlines()


def fen_with_pieces(pieces, side="w"):
    """Build small attack fixtures from a mapping such as {"d4": "N"}."""
    rows = []
    for rank in "87654321":
        row = ""
        empty = 0
        for file in "abcdefgh":
            piece = pieces.get(file + rank)
            if piece is None:
                empty += 1
            else:
                if empty:
                    row += str(empty)
                    empty = 0
                row += piece
        if empty:
            row += str(empty)
        rows.append(row)
    return "/".join(rows) + f" {side} - - 0 1"


class UciTests(unittest.TestCase):
    def setUp(self):
        self.engine = subprocess.Popen(
            [str(ENGINE)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="ascii",
        )
        self.output = Queue()
        # Reading on a separate thread lets a missing flush fail with a timeout
        # instead of hanging the test forever, including on Windows pipes.
        self.reader = Thread(target=self.read_output, daemon=True)
        self.reader.start()

    def read_output(self):
        for line in self.engine.stdout:
            self.output.put(line.rstrip("\r\n"))
        self.output.put(None)

    def tearDown(self):
        if self.engine.poll() is None:
            self.engine.kill()
        self.engine.wait(timeout=TIMEOUT)
        self.reader.join(timeout=TIMEOUT)
        self.engine.stdin.close()
        self.engine.stdout.close()
        self.engine.stderr.close()

    def send(self, commands):
        self.engine.stdin.write(commands)
        self.engine.stdin.flush()

    def expect(self, expected):
        try:
            actual = self.output.get(timeout=TIMEOUT)
        except Empty:
            self.fail(f"Timed out waiting for {expected!r}")
        self.assertEqual(actual, expected)

    def expect_exit(self):
        self.assertEqual(self.engine.wait(timeout=TIMEOUT), 0)
        self.expect(None)
        self.assertEqual(self.engine.stderr.read(), "")

    def expect_position(self, expected):
        self.send("d\nisready\n")
        for line in expected:
            self.expect(line)
        self.expect("readyok")

    def expect_attack(self, square, color, expected):
        self.send(f"attacked {square} {color}\n")
        answer = "yes" if expected else "no"
        self.expect(f"info string attacked {square} {color} {answer}")

    def expect_check(self, color, expected):
        self.send(f"check {color}\n")
        answer = "yes" if expected else "no"
        self.expect(f"info string check {color} {answer}")

    def read_moves(self):
        self.send("moves\n")
        try:
            line = self.output.get(timeout=TIMEOUT)
        except Empty:
            self.fail("Timed out waiting for moves")
        self.assertIsNotNone(line)
        words = line.split()
        self.assertEqual(words[:4], ["info", "string", "moves", "non-pawn"])
        moves = words[4:]
        self.assertEqual(len(moves), len(set(moves)), "Duplicate moves")
        return moves

    def test_handshake_while_input_stays_open(self):
        self.send("uci\n")
        self.expect("id name Learning Chess")
        self.expect("id author Learning Chess contributors")
        self.expect("uciok")
        for _ in range(2):
            self.send("isready\n")
            self.expect("readyok")
        self.assertIsNone(self.engine.poll())
        self.send("quit\n")
        self.expect_exit()

    def test_whitespace_and_unknown_commands(self):
        self.send("\n \t\r\nunknown\nisready_extra\n \tisready \t\r\n")
        self.expect("readyok")
        self.send("quit\nisready\n")
        self.expect_exit()

    def test_starting_board(self):
        for _ in range(2):
            self.expect_position(START_POSITION)
        self.send("quit\n")
        self.expect_exit()

    def test_fen_and_startpos_reset(self):
        self.send("position fen " + START_FEN + "\n")
        self.expect_position(START_POSITION)
        self.send(" \tposition\tfen " + SAMPLE_FEN + " \r\n")
        self.expect_position(SAMPLE_POSITION)
        self.send("position startpos\n")
        self.expect_position(START_POSITION)
        self.send("quit\n")
        self.expect_exit()

    def test_en_passant_without_available_capture(self):
        self.send("position fen rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/"
                  "RNBQKBNR b KQkq e3 0 1\n")
        expected = START_POSITION.copy()
        expected[4] = "4 . . . . P . . ."
        expected[6] = "2 P P P P . P P P"
        expected[9] = "Side to move: Black"
        expected[11] = "En passant: e3"
        self.expect_position(expected)
        self.send("quit\n")
        self.expect_exit()

    def test_missing_rights_and_large_counters(self):
        self.send("position fen " + SAMPLE_FEN + "\n")
        self.expect_position(SAMPLE_POSITION)
        self.send("position fen r3k2r/8/8/3pP3/8/8/8/R3K2R "
                  "b - - 2147483647 2147483647\n")
        expected = SAMPLE_POSITION[:9] + [
            "Side to move: Black", "Castling: -", "En passant: -",
            "Halfmove clock: 2147483647", "Fullmove number: 2147483647",
        ]
        self.expect_position(expected)
        self.send("quit\n")
        self.expect_exit()

    def test_invalid_positions_leave_previous_position_unchanged(self):
        self.send("position fen " + SAMPLE_FEN + "\n")
        self.expect_position(SAMPLE_POSITION)
        commands = ["position", "position unknown", "position fen",
                    "position startpos extra", "position fen " + SAMPLE_FEN + " extra"]
        fields = SAMPLE_FEN.split()
        for count in range(1, 6):
            commands.append("position fen " + " ".join(fields[:count]))
        invalid_fields = {
            0: ["8/8/8/8/8/8/8", "8/8/8/8/8/8/8/8/8",
                "9/8/8/8/8/8/8/8", "7/8/8/8/8/8/8/8",
                "8P/8/8/8/8/8/8/8", "44/8/8/8/8/8/8/8",
                "x7/8/8/8/8/8/8/8", "8//8/8/8/8/8/8",
                "8/8/8/8/8/8/8/8/", "/8/8/8/8/8/8/8/8"],
            1: ["W", "white", "-"],
            2: ["KK", "qK", "K-", "A"],
            3: ["d3", "d4", "i6", "D6", "d66", "d"],
            4: ["-1", "+1", "1x", "2147483648", "9" * 50],
            5: ["0", "-1", "1x", "2147483648"],
        }
        for index, values in invalid_fields.items():
            for value in values:
                invalid = fields.copy()
                invalid[index] = value
                commands.append("position fen " + " ".join(invalid))
        for command in commands:
            with self.subTest(command=command):
                self.send(command + "\n")
                self.expect("info string Invalid position command")
                self.expect_position(SAMPLE_POSITION)
        self.send("quit\n")
        self.expect_exit()

    def test_move_lists_are_rejected_without_changing_position(self):
        self.send("position fen " + SAMPLE_FEN + "\n")
        self.expect_position(SAMPLE_POSITION)
        for command in ["position startpos moves e2e4",
                        "position fen " + SAMPLE_FEN + " moves e5d6"]:
            self.send(command + "\n")
            self.expect("info string Move lists are not supported yet")
            self.expect_position(SAMPLE_POSITION)
        self.send("quit\n")
        self.expect_exit()

    def test_piece_attack_patterns(self):
        diagonal = "a1 b2 c3 e5 f6 g7 h8 a7 b6 c5 e3 f2 g1"
        straight = "a4 b4 c4 e4 f4 g4 h4 d1 d2 d3 d5 d6 d7 d8"
        cases = [
            ("P", "d4", "c5 e5"),
            ("p", "d4", "c3 e3"),
            ("N", "d4", "b3 b5 c2 c6 e2 e6 f3 f5"),
            ("B", "d4", diagonal),
            ("R", "d4", straight),
            ("Q", "d4", diagonal + " " + straight),
            ("K", "d4", "c3 d3 e3 c4 e4 c5 d5 e5"),
            ("N", "h1", "f2 g3"),
            ("B", "h1", "g2 f3 e4 d5 c6 b7 a8"),
            ("R", "h1", "a1 b1 c1 d1 e1 f1 g1 h2 h3 h4 h5 h6 h7 h8"),
            ("Q", "h1", "a1 b1 c1 d1 e1 f1 g1 h2 h3 h4 h5 h6 h7 h8 "
                         "g2 f3 e4 d5 c6 b7 a8"),
            ("K", "h1", "g1 g2 h2"),
            ("P", "a2", "b3"), ("P", "h2", "g3"),
            ("p", "a7", "b6"), ("p", "h7", "g6"),
            ("P", "a8", ""), ("p", "h1", ""),
        ]
        # Non-pawn attacks have the same geometry for both colors.
        cases += [(piece.lower(), square, targets)
                  for piece, square, targets in cases if piece in "NBRQK"]
        for piece, source, targets in cases:
            with self.subTest(piece=piece, source=source):
                self.send("position fen " + fen_with_pieces({source: piece}) + "\n")
                color = "white" if piece.isupper() else "black"
                other = "black" if color == "white" else "white"
                expected = set(targets.split())
                for rank in "12345678":
                    for file in "abcdefgh":
                        square = file + rank
                        self.expect_attack(square, color, square in expected)
                self.expect_attack(source, other, False)
        self.send("quit\n")
        self.expect_exit()

    def test_sliding_attacks_stop_at_first_piece(self):
        # Each row gives a square before the blocker, the blocker, and beyond.
        rays = [("d5", "d6", "d7"), ("d3", "d2", "d1"),
                ("e4", "f4", "g4"), ("c4", "b4", "a4"),
                ("e5", "f6", "g7"), ("c5", "b6", "a7"),
                ("e3", "f2", "g1"), ("c3", "b2", "a1")]
        for queen, color in [("Q", "white"), ("q", "black")]:
            for blocker in ["N", "n"]:
                for before, occupied, beyond in rays:
                    with self.subTest(queen=queen, blocker=blocker, square=occupied):
                        fen = fen_with_pieces({"d4": queen, occupied: blocker})
                        self.send("position fen " + fen + "\n")
                        self.expect_attack(before, color, True)
                        self.expect_attack(occupied, color, True)
                        self.expect_attack(beyond, color, False)
        self.send("quit\n")
        self.expect_exit()

    def test_knight_jumps_and_pinned_pieces_still_attack(self):
        self.send("position fen 8/8/8/8/8/8/PP6/NP6 w - - 0 1\n")
        self.expect_attack("b3", "white", True)
        self.expect_attack("c2", "white", True)
        self.send("position fen k3r3/8/8/8/8/8/4N3/4K3 w - - 0 1\n")
        self.expect_attack("f4", "white", True)
        self.expect_attack("e1", "black", False)
        self.expect_attack("e2", "black", True)
        # Kings attack adjacent squares even when moving there would be illegal.
        self.send("position fen 8/8/8/8/8/4k3/8/4K3 b - - 0 1\n")
        self.expect_attack("e2", "white", True)
        self.expect_attack("e2", "black", True)
        self.send("quit\n")
        self.expect_exit()

    def test_attack_commands_preserve_position(self):
        self.send("position fen " + SAMPLE_FEN + "\n")
        self.send(" \tattacked\td6\twhite \r\n")
        self.expect("info string attacked d6 white yes")
        self.expect_attack("d6", "black", False)
        for command in ["attacked", "attacked e4", "attacked e4 red",
                        "attacked a0 white", "attacked a9 white",
                        "attacked i4 white", "attacked E4 white",
                        "attacked e44 white", "attacked e white",
                        "attacked e4 white extra"]:
            with self.subTest(command=command):
                self.send(command + "\n")
                self.expect("info string Invalid attacked command")
        self.expect_position(SAMPLE_POSITION)
        self.send("quit\n")
        self.expect_exit()

    def test_check_by_each_piece_and_color(self):
        self.expect_check("white", False)
        self.expect_check("black", False)
        cases = [
            ("white", {"e4": "K", "a8": "k"},
             [("d5", "p"), ("f6", "n"), ("b7", "b"), ("e7", "r"), ("h4", "q")]),
            ("black", {"e5": "k", "a1": "K"},
             [("d4", "P"), ("f3", "N"), ("b2", "B"), ("e2", "R"), ("h5", "Q")]),
        ]
        for color, kings, attackers in cases:
            for square, piece in attackers:
                for side in ["w", "b"]:
                    with self.subTest(color=color, attacker=piece, turn=side):
                        pieces = kings.copy()
                        pieces[square] = piece
                        self.send("position fen " + fen_with_pieces(pieces, side) + "\n")
                        self.expect_check(color, True)
        # Adjacent kings attack one another, even in this illegal position.
        self.send("position fen " + fen_with_pieces({"e4": "K", "e5": "k"}) + "\n")
        self.expect_check("white", True)
        self.expect_check("black", True)
        self.send("quit\n")
        self.expect_exit()

    def test_check_with_blockers_and_pinned_attacker(self):
        for blocker in ["N", "n"]:
            pieces = {"e1": "K", "a8": "k", "e8": "r", "e2": blocker}
            self.send("position fen " + fen_with_pieces(pieces) + "\n")
            self.expect_check("white", False)
        # A friendly rook defending the king's square is not an enemy attack.
        self.send("position fen " + fen_with_pieces({"e1": "K", "e8": "R", "a8": "k"}) + "\n")
        self.expect_check("white", False)
        # The black knight is pinned to its king, but still attacks f4.
        pieces = {"f4": "K", "e1": "R", "e2": "n", "e8": "k"}
        self.send("position fen " + fen_with_pieces(pieces) + "\n")
        self.expect_check("white", True)
        self.expect_check("black", False)
        self.send("quit\n")
        self.expect_exit()

    def test_check_requires_exactly_one_requested_king(self):
        cases = [({}, "white"), ({}, "black"),
                 ({"a1": "K", "h1": "K", "a8": "k"}, "white"),
                 ({"a8": "k", "h8": "k", "a1": "K"}, "black")]
        for pieces, color in cases:
            with self.subTest(pieces=pieces, color=color):
                self.send("position fen " + fen_with_pieces(pieces) + "\n")
                self.send(f"check {color}\n")
                self.expect(f"info string Cannot check {color}: expected exactly one king")
        # Inspection of one king is allowed in an otherwise incomplete fixture.
        for piece, color in [("K", "white"), ("k", "black")]:
            for square in ["a1", "h8"]:
                self.send("position fen " + fen_with_pieces({square: piece}) + "\n")
                self.expect_check(color, False)
        self.send("quit\n")
        self.expect_exit()

    def test_check_commands_preserve_position(self):
        self.send("position fen " + SAMPLE_FEN + "\n")
        self.send(" \tcheck\twhite \r\n")
        self.expect("info string check white no")
        self.expect_check("black", False)
        for command in ["check", "check red", "check White", "check white extra"]:
            self.send(command + "\n")
            self.expect("info string Invalid check command")
        self.expect_position(SAMPLE_POSITION)
        self.send("quit\n")
        self.expect_exit()

    def test_starting_non_pawn_moves_and_position_preservation(self):
        for _ in range(2):
            self.assertEqual(self.read_moves(), ["b1a3", "b1c3", "g1f3", "g1h3"])
            self.expect_position(START_POSITION)
        self.send("position fen " + START_FEN.replace(" w ", " b ") + "\n")
        self.assertEqual(self.read_moves(), ["b8a6", "b8c6", "g8f6", "g8h6"])
        self.send("position fen " + SAMPLE_FEN + "\n")
        self.read_moves()
        self.expect_position(SAMPLE_POSITION)
        self.send("quit\n")
        self.expect_exit()

    def test_non_pawn_move_patterns(self):
        diagonal = "b2 c3 e5 f6 g7 h8 a7 b6 c5 e3 f2 g1"
        straight = "a4 b4 c4 e4 f4 g4 h4 d1 d2 d3 d5 d6 d7 d8"
        cases = [("N", "b3 b5 c2 c6 e2 e6 f3 f5"),
                 ("B", diagonal), ("R", straight), ("Q", diagonal + " " + straight)]
        for side in ["w", "b"]:
            for piece, targets in cases:
                with self.subTest(side=side, piece=piece):
                    pieces = {"a1": "K", "h7": "k", "d4": piece}
                    if side == "b":
                        pieces = {square: symbol.swapcase() for square, symbol in pieces.items()}
                    self.send("position fen " + fen_with_pieces(pieces, side) + "\n")
                    moves = self.read_moves()
                    self.assertCountEqual([move for move in moves if move[:2] == "d4"],
                                          ["d4" + square for square in targets.split()])
                    self.assertTrue(all(move[:2] in ["a1", "d4"] for move in moves))
        self.send("position fen " + fen_with_pieces({"a1": "K", "h8": "k"}) + "\n")
        self.assertCountEqual(self.read_moves(), ["a1b1", "a1a2", "a1b2"])
        self.send("quit\n")
        self.expect_exit()

    def test_move_captures_and_blockers(self):
        pieces = {"a1": "K", "h7": "k", "d4": "Q", "f4": "P", "d6": "p"}
        self.send("position fen " + fen_with_pieces(pieces) + "\n")
        moves = self.read_moves()
        for move in ["d4e4", "d4d5", "d4d6"]:
            self.assertIn(move, moves)
        for move in ["d4f4", "d4g4", "d4d7", "d4a1", "d4d4"]:
            self.assertNotIn(move, moves)
        # Even an otherwise invalid study position must never capture a king.
        self.send("position fen " + fen_with_pieces({"a1": "K", "h8": "k", "h1": "R"}) + "\n")
        self.assertNotIn("h1h8", self.read_moves())
        self.send("quit\n")
        self.expect_exit()

    def test_pinned_moves_and_check_evasions(self):
        king_moves = ["e1d1", "e1f1", "e1d2", "e1f2"]
        for piece in ["N", "R"]:
            self.send("position fen " + fen_with_pieces(
                {"e1": "K", "a8": "k", "e8": "r", "e2": piece}) + "\n")
            expected = king_moves.copy()
            if piece == "R":
                expected += ["e2e3", "e2e4", "e2e5", "e2e6", "e2e7", "e2e8"]
            self.assertCountEqual(self.read_moves(), expected)
        # The knight can interpose on either e2 or e4 to answer the rook check.
        self.send("position fen " + fen_with_pieces(
            {"e1": "K", "a8": "k", "e8": "r", "c3": "N"}) + "\n")
        self.assertCountEqual(self.read_moves(), king_moves + ["c3e2", "c3e4"])
        # Blocking just the bishop does not answer this double check.
        self.send("position fen " + fen_with_pieces(
            {"e1": "K", "a8": "k", "e8": "r", "b4": "b", "a2": "N"}) + "\n")
        self.assertCountEqual(self.read_moves(), ["e1d1", "e1f1", "e1f2"])
        # Capturing the checking knight removes its attack from the copied board.
        self.send("position fen " + fen_with_pieces(
            {"e2": "K", "a8": "k", "c3": "n", "b1": "N"}) + "\n")
        self.assertIn("b1c3", self.read_moves())
        self.send("quit\n")
        self.expect_exit()

    def test_king_safety_after_moving_or_capturing(self):
        for defended in [False, True]:
            pieces = {"e1": "K", "a8": "k", "e2": "r"}
            if defended:
                pieces["e8"] = "r"
            self.send("position fen " + fen_with_pieces(pieces) + "\n")
            self.assertEqual("e1e2" in self.read_moves(), not defended)
        # e1 currently blocks the rook's line to f1; moving the king uncovers it.
        self.send("position fen " + fen_with_pieces({"e1": "K", "h8": "k", "a1": "r"}) + "\n")
        self.assertNotIn("e1f1", self.read_moves())
        self.send("position fen " + fen_with_pieces({"e1": "K", "e3": "k"}) + "\n")
        self.assertCountEqual(self.read_moves(), ["e1d1", "e1f1"])
        # The pinned black knight still makes f5 unsafe for White's king.
        self.send("position fen " + fen_with_pieces(
            {"f4": "K", "e1": "R", "e7": "n", "e8": "k"}) + "\n")
        self.assertNotIn("f4f5", self.read_moves())
        self.send("quit\n")
        self.expect_exit()

    def test_moves_errors_and_incomplete_scope(self):
        for pieces in [{}, {"a1": "K"}, {"a8": "k"},
                       {"a1": "K", "b1": "K", "a8": "k"},
                       {"a8": "k", "b8": "k", "a1": "K"}]:
            self.send("position fen " + fen_with_pieces(pieces) + "\nmoves\n")
            self.expect("info string Cannot generate moves: expected exactly one king of each color")
        self.send("position startpos\nmoves extra\n")
        self.expect("info string Invalid moves command")
        self.expect_position(START_POSITION)
        self.send("position fen r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1\n")
        moves = self.read_moves()
        self.assertNotIn("e1g1", moves)
        self.assertNotIn("e1c1", moves)
        # No non-pawn moves does not mean no legal moves: a2a3 remains possible.
        self.send("position fen 8/8/8/8/8/6k1/P4q2/7K w - - 0 1\n")
        self.assertEqual(self.read_moves(), [])
        self.send("quit\n")
        self.expect_exit()

    def test_line_length_boundary(self):
        self.send(" " * 4088 + "isready\n")  # Exactly 4095 bytes.
        self.expect("readyok")
        self.send("isready" + " " * 4089 + "\n")  # One byte too many.
        self.send(" " * 4095 + "quit\nisready\n")
        self.expect("readyok")
        self.send("quit\n")
        self.expect_exit()

    def test_empty_input(self):
        self.engine.stdin.close()
        self.expect_exit()

    def test_final_command_without_newline(self):
        self.send("isready")
        self.engine.stdin.close()
        self.expect("readyok")
        self.expect_exit()

    def test_oversized_final_line(self):
        self.send(" " * 4095 + "quit")
        self.engine.stdin.close()
        self.expect_exit()


if __name__ == "__main__":
    unittest.main()
