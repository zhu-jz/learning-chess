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


def fen_with_pieces(pieces, side="w", en_passant="-", castling="-"):
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
    return "/".join(rows) + f" {side} {castling} {en_passant} 0 1"


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
        self.assertEqual(words[:4], ["info", "string", "moves", "legal"])
        moves = words[4:]
        self.assertEqual(len(moves), len(set(moves)), "Duplicate moves")
        return moves

    def expect_pawn_moves_both_colors(self, pieces, source, targets, en_passant="-"):
        # Reflect ranks and exchange colors to exercise the same rule for Black.
        def reflect(square):
            return square[0] + str(9 - int(square[1])) + square[2:]

        for side in ["w", "b"]:
            board = pieces
            start = source
            target_square = en_passant
            destinations = targets.split()
            if side == "b":
                board = {reflect(square): piece.swapcase() for square, piece in pieces.items()}
                start = reflect(source)
                destinations = [reflect(square) for square in destinations]
                if en_passant != "-":
                    target_square = reflect(en_passant)
            with self.subTest(side=side, pieces=board, source=start):
                self.send("position fen " + fen_with_pieces(board, side, target_square) + "\n")
                actual = [move for move in self.read_moves() if move[:2] == start]
                self.assertCountEqual(actual, [start + square for square in destinations])

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

    def test_starting_moves_and_position_preservation(self):
        white_moves = ["b1a3", "b1c3", "g1f3", "g1h3"]
        black_moves = []
        for file in "abcdefgh":
            white_moves += [file + "2" + file + "3", file + "2" + file + "4"]
            black_moves += [file + "7" + file + "5", file + "7" + file + "6"]
        black_moves += ["b8a6", "b8c6", "g8f6", "g8h6"]
        for _ in range(2):
            self.assertEqual(self.read_moves(), white_moves)
            self.expect_position(START_POSITION)
        self.send("position fen " + START_FEN.replace(" w ", " b ") + "\n")
        self.assertEqual(self.read_moves(), black_moves)
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

    def test_moves_errors_and_pawn_only_mobility(self):
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
        self.assertIn("e1g1", moves)
        self.assertIn("e1c1", moves)
        # The king has no moves, but the pawn can still advance.
        self.send("position fen 8/8/8/8/8/6k1/P4q2/7K w - - 0 1\n")
        self.assertEqual(self.read_moves(), ["a2a3", "a2a4"])
        self.send("quit\n")
        self.expect_exit()

    def test_pawn_advances_and_ordinary_captures(self):
        cases = [
            ({"e2": "P"}, "e2", "e3 e4"),
            ({"e2": "P", "e3": "N"}, "e2", ""),
            ({"e2": "P", "e3": "n"}, "e2", ""),
            ({"e2": "P", "e4": "N"}, "e2", "e3"),
            ({"e2": "P", "e4": "n"}, "e2", "e3"),
            ({"e2": "P", "d3": "n", "f3": "n"}, "e2", "d3 e3 f3 e4"),
            ({"e2": "P", "d3": "P", "f3": "P"}, "e2", "e3 e4"),
            ({"e2": "P", "e3": "n", "d3": "n", "f3": "n"}, "e2", "d3 f3"),
            ({"e4": "P"}, "e4", "e5"),
            ({"e5": "P", "d4": "n", "e4": "n", "f4": "n"}, "e5", "e6"),
        ]
        for pieces, source, targets in cases:
            board = {"h1": "K", "h8": "k"}
            board.update(pieces)
            self.expect_pawn_moves_both_colors(board, source, targets)
        self.send("quit\n")
        self.expect_exit()

    def test_pawn_edges_and_end_ranks(self):
        cases = [
            ({"a2": "P", "b3": "n", "h2": "n"}, "a2", "a3 a4 b3"),
            ({"h2": "P", "g3": "b", "a4": "n"}, "h2", "h3 h4 g3"),
            ({"e6": "P"}, "e6", "e7"),
            ({"a1": "P"}, "a1", ""),
            ({"a8": "P"}, "a8", ""),
        ]
        for pieces, source, targets in cases:
            board = {"h1": "K", "h8": "k"}
            board.update(pieces)
            self.expect_pawn_moves_both_colors(board, source, targets)
        # A pawn must not capture the opposing king, even in a study position.
        self.expect_pawn_moves_both_colors({"h1": "K", "d3": "k", "e2": "P"},
                                          "e2", "e3 e4")
        self.send("quit\n")
        self.expect_exit()

    def test_pawn_pins_and_check_evasions(self):
        cases = [
            # Advances keep the rook's line blocked; a diagonal capture opens it.
            ({"e1": "K", "a8": "k", "e8": "r", "e2": "P", "d3": "b"},
             "e2", "e3 e4"),
            # This diagonal pin permits capturing the bishop, but no advance.
            ({"c1": "K", "a8": "k", "e3": "b", "d2": "P"}, "d2", "e3"),
            # Only the final square must block check, not the square crossed.
            ({"e4": "K", "a8": "k", "a4": "r", "d2": "P"}, "d2", "d4"),
            ({"e3": "K", "a8": "k", "a3": "r", "d2": "P"}, "d2", "d3"),
            ({"e1": "K", "a8": "k", "d3": "n", "e2": "P"}, "e2", "d3"),
            ({"e1": "K", "a8": "k", "e8": "r", "a2": "P"}, "a2", ""),
        ]
        for board, source, targets in cases:
            self.expect_pawn_moves_both_colors(board, source, targets)
        self.send("quit\n")
        self.expect_exit()

    def test_promotion_choices_for_advances_and_captures(self):
        cases = [
            ({"h1": "K", "h8": "k", "e7": "P"}, "e7", ["e8"]),
            ({"h1": "K", "h8": "k", "e7": "P", "d8": "n", "f8": "n"},
             "e7", ["d8", "e8", "f8"]),
            ({"h1": "K", "h8": "k", "a7": "P", "b8": "n"}, "a7", ["a8", "b8"]),
            ({"a1": "K", "a8": "k", "h7": "P", "g8": "n"}, "h7", ["g8", "h8"]),
        ]
        for board, source, destinations in cases:
            targets = " ".join(square + piece for square in destinations for piece in "nbrq")
            self.expect_pawn_moves_both_colors(board, source, targets)
        self.send("quit\n")
        self.expect_exit()

    def test_promotion_blockers_and_king_capture_exclusion(self):
        cases = [
            ({"h1": "K", "h8": "k", "e7": "P", "e8": "n"}, "e7", ""),
            ({"h1": "K", "h8": "k", "e7": "P", "e8": "N", "d8": "B"}, "e7", ""),
            ({"h1": "K", "h8": "k", "e7": "P", "e8": "N", "d8": "n", "f8": "B"},
             "e7", "d8n d8b d8r d8q"),
            ({"h1": "K", "d8": "k", "e7": "P"}, "e7", "e8n e8b e8r e8q"),
        ]
        for board, source, targets in cases:
            self.expect_pawn_moves_both_colors(board, source, targets)
        self.send("quit\n")
        self.expect_exit()

    def test_promotions_respect_king_safety(self):
        cases = [
            # Capturing the rook is the only pawn move that answers its check.
            ({"e1": "K", "a8": "k", "e8": "r", "d7": "P"}, "d7", "e8n e8b e8r e8q"),
            # Each promotion blocks the checking rook's line along rank 8.
            ({"a8": "K", "h6": "k", "h8": "r", "e7": "P"}, "e7", "e8n e8b e8r e8q"),
            # Capturing on d8 would expose the king to the rook on e8.
            ({"e1": "K", "a8": "k", "e8": "r", "e7": "P", "d8": "n"}, "e7", ""),
        ]
        for board, source, targets in cases:
            self.expect_pawn_moves_both_colors(board, source, targets)
        self.send("quit\n")
        self.expect_exit()

    def test_promotions_preserve_position_and_print_suffixes(self):
        self.send("position fen 7k/4P3/8/8/8/8/8/7K w - - 9 12\n")
        expected = ["h1g1", "h1g2", "h1h2", "e7e8n", "e7e8b", "e7e8r", "e7e8q"]
        for _ in range(2):
            self.assertEqual(self.read_moves(), expected)
            self.expect_position([
                "8 . . . . . . . k", "7 . . . . P . . .",
                "6 . . . . . . . .", "5 . . . . . . . .",
                "4 . . . . . . . .", "3 . . . . . . . .",
                "2 . . . . . . . .", "1 . . . . . . . K",
                "  a b c d e f g h", "Side to move: White", "Castling: -",
                "En passant: -", "Halfmove clock: 9", "Fullmove number: 12",
            ])
        self.send("quit\n")
        self.expect_exit()

    def test_en_passant_captures_and_position_preservation(self):
        cases = [(SAMPLE_FEN, "e5", ["e5d6", "e5e6"]),
                 ("r3k2r/8/8/8/3Pp3/8/8/R3K2R b Kq d3 0 42", "e4", ["e4d3", "e4e3"])]
        for fen, source, expected in cases:
            self.send("position fen " + fen + "\n")
            self.assertEqual([move for move in self.read_moves() if move[:2] == source],
                             expected)
        self.send("position fen " + SAMPLE_FEN + "\n")
        for _ in range(2):
            self.assertIn("e5d6", self.read_moves())
            self.expect_position(SAMPLE_POSITION)
        # Reloading the same board without the target removes the capture.
        self.send("position fen " + SAMPLE_FEN.replace(" d6 ", " - ") + "\n")
        self.assertNotIn("e5d6", self.read_moves())
        self.send("quit\n")
        self.expect_exit()

    def test_en_passant_from_both_sides_and_board_edges(self):
        board = {"h1": "K", "h8": "k", "c5": "P", "e5": "P", "d5": "p"}
        self.expect_pawn_moves_both_colors(board, "c5", "c6 d6", "d6")
        self.expect_pawn_moves_both_colors(board, "e5", "d6 e6", "d6")
        self.expect_pawn_moves_both_colors(
            {"h1": "K", "h8": "k", "a5": "P", "b5": "p"}, "a5", "a6 b6", "b6")
        self.expect_pawn_moves_both_colors(
            {"a1": "K", "a8": "k", "h5": "P", "g5": "p"}, "h5", "g6 h6", "g6")
        # A neighboring array index across the board edge is not a neighbor file.
        self.expect_pawn_moves_both_colors(
            {"a1": "K", "a8": "k", "h5": "P", "a5": "p"}, "h5", "h6", "a6")
        self.send("quit\n")
        self.expect_exit()

    def test_en_passant_requires_a_matching_board(self):
        cases = [
            ({"e5": "P"}, "e5", "e6", "d6"),
            ({"e5": "P", "d5": "P"}, "e5", "e6", "d6"),
            ({"e5": "P", "d5": "n"}, "e5", "e6", "d6"),
            ({"e5": "P", "d5": "p", "d6": "P"}, "e5", "e6", "d6"),
            # Occupied enemy targets remain ordinary diagonal captures.
            ({"e5": "P", "d5": "p", "d6": "n"}, "e5", "d6 e6", "d6"),
            ({"e5": "P", "d5": "p"}, "e5", "e6", "-"),
            ({"e5": "P", "d5": "p"}, "e5", "e6", "f6"),
            ({"e4": "P", "d4": "p"}, "e4", "e5", "d6"),
        ]
        for pieces, source, targets, en_passant in cases:
            board = {"h1": "K", "h8": "k"}
            board.update(pieces)
            self.expect_pawn_moves_both_colors(board, source, targets, en_passant)
        # Capturing the knight on d6 must not also remove the checking pawn d5.
        self.expect_pawn_moves_both_colors(
            {"e4": "K", "a8": "k", "e5": "P", "d5": "p", "d6": "n"},
            "e5", "", "d6")
        self.send("quit\n")
        self.expect_exit()

    def test_en_passant_cannot_expose_the_king(self):
        cases = [
            # Removing both adjacent pawns opens the rook's horizontal line.
            ({"h5": "K", "a8": "k", "a5": "r", "g5": "P", "f5": "p"},
             "g5", "g6", "f6"),
            # Vacating the capturing pawn's file exposes the king.
            ({"e1": "K", "a8": "k", "e8": "r", "e5": "P", "d5": "p"},
             "e5", "e6", "d6"),
            # Removing the captured pawn opens a diagonal to the king.
            ({"a2": "K", "h8": "k", "f7": "b", "e5": "P", "d5": "p"},
             "e5", "e6", "d6"),
        ]
        for board, source, targets, en_passant in cases:
            self.expect_pawn_moves_both_colors(board, source, targets, en_passant)
        self.send("quit\n")
        self.expect_exit()

    def test_en_passant_can_answer_check(self):
        cases = [
            # Remove the pawn that is checking the king on e4.
            ({"e4": "K", "a8": "k", "e5": "P", "d5": "p"}, "e5", "d6", "d6"),
            # Interpose on d6 to block the bishop's check along e7-d6-c5.
            ({"c5": "K", "a8": "k", "e7": "b", "e5": "P", "d5": "p"},
             "e5", "d6", "d6"),
            # Removing the checking pawn reveals a bishop behind it: still illegal.
            ({"e4": "K", "a8": "k", "c6": "b", "e5": "P", "d5": "p"},
             "e5", "", "d6"),
        ]
        for board, source, targets, en_passant in cases:
            self.expect_pawn_moves_both_colors(board, source, targets, en_passant)
        self.send("quit\n")
        self.expect_exit()

    def expect_castles_both_colors(self, pieces, rights, expected):
        def reflect(square):
            return square[0] + str(9 - int(square[1]))

        for side in ["w", "b"]:
            board = pieces
            castling = rights
            castles = ["e1c1", "e1g1"]
            wanted = expected.split()
            if side == "b":
                board = {reflect(square): piece.swapcase() for square, piece in pieces.items()}
                castling = rights.swapcase()
                castles = ["e8c8", "e8g8"]
                wanted = [reflect(move[:2]) + reflect(move[2:]) for move in wanted]
            with self.subTest(side=side, pieces=board, rights=castling):
                self.send("position fen " + fen_with_pieces(board, side, castling=castling) + "\n")
                actual = [move for move in self.read_moves() if move in castles]
                self.assertCountEqual(actual, wanted)

    def test_castling_requires_the_matching_right(self):
        board = {"a1": "R", "e1": "K", "h1": "R", "e8": "k"}
        for rights, expected in [("-", ""), ("K", "e1g1"), ("Q", "e1c1"),
                                 ("KQ", "e1c1 e1g1"), ("kq", "")]:
            self.expect_castles_both_colors(board, rights, expected)
        self.send("quit\n")
        self.expect_exit()

    def test_castling_requires_home_pieces(self):
        base = {"a1": "R", "e1": "K", "h1": "R", "e8": "k"}
        for square, replacement, expected in [("a1", None, "e1g1"),
                                               ("h1", None, "e1c1"),
                                               ("a1", "B", "e1g1"),
                                               ("h1", "B", "e1c1"),
                                               ("a1", "r", ""),
                                               ("h1", "r", "")]:
            board = base.copy()
            if replacement is None:
                del board[square]
            else:
                board[square] = replacement
            self.expect_castles_both_colors(board, "KQ", expected)
        # Rights do not allow a displaced king to make a two-square move.
        for side, board, rights, forbidden in [
            ("w", {"a1": "R", "d1": "K", "h1": "R", "e8": "k"}, "KQ", ["d1b1", "d1f1"]),
            ("b", {"a8": "r", "d8": "k", "h8": "r", "e1": "K"}, "kq", ["d8b8", "d8f8"]),
        ]:
            self.send("position fen " + fen_with_pieces(board, side, castling=rights) + "\n")
            moves = self.read_moves()
            for move in forbidden:
                self.assertNotIn(move, moves)
        self.send("quit\n")
        self.expect_exit()

    def test_castling_requires_every_path_square_empty(self):
        base = {"a1": "R", "e1": "K", "h1": "R", "e8": "k"}
        for square in ["b1", "c1", "d1", "f1", "g1"]:
            for piece in ["N", "n"]:
                board = base.copy()
                board[square] = piece
                expected = "e1g1" if square in ["b1", "c1", "d1"] else "e1c1"
                self.expect_castles_both_colors(board, "KQ", expected)
        self.send("quit\n")
        self.expect_exit()

    def test_castling_attack_restrictions_apply_to_king_path(self):
        cases = [("e8", ""), ("f8", "e1c1"), ("g8", "e1c1"),
                 ("d8", "e1g1"), ("c8", "e1g1"),
                 ("a8", "e1c1 e1g1"), ("b8", "e1c1 e1g1"), ("h8", "e1c1 e1g1")]
        for square, expected in cases:
            # Keep the opposing king off the testing rook's file.
            king_square = "e7" if square[0] == "d" else "d7"
            board = {"a1": "R", "e1": "K", "h1": "R", king_square: "k", square: "r"}
            self.expect_castles_both_colors(board, "KQ", expected)
        self.send("quit\n")
        self.expect_exit()

    def test_castling_respects_pawns_kings_and_pinned_attackers(self):
        self.expect_castles_both_colors(
            {"a1": "R", "e1": "K", "h1": "R", "a8": "k", "e2": "p"}, "KQ", "")
        self.expect_castles_both_colors(
            {"a1": "R", "e1": "K", "h1": "R", "g2": "k"}, "KQ", "e1c1")
        # The pinned knight still attacks d1; the rook on f1 also blocks kingside.
        self.expect_castles_both_colors(
            {"a1": "R", "e1": "K", "h1": "R", "f1": "R", "f2": "n", "f8": "k"},
            "KQ", "")
        self.send("quit\n")
        self.expect_exit()

    def test_castling_generation_preserves_position(self):
        self.send("position fen r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 17 42\n")
        for _ in range(2):
            moves = self.read_moves()
            self.assertEqual(len(moves), 26)
            self.assertIn("e1c1", moves)
            self.assertIn("e1g1", moves)
            self.expect_position([
                "8 r . . . k . . r", "7 . . . . . . . .",
                "6 . . . . . . . .", "5 . . . . . . . .",
                "4 . . . . . . . .", "3 . . . . . . . .",
                "2 . . . . . . . .", "1 R . . . K . . R",
                "  a b c d e f g h", "Side to move: White", "Castling: KQkq",
                "En passant: -", "Halfmove clock: 17", "Fullmove number: 42",
            ])
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
