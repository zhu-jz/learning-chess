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
