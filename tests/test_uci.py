"""Exercise the engine as a GUI would, without third-party packages."""

from pathlib import Path
from queue import Empty, Queue
import subprocess
import sys
from threading import Thread
import unittest


ENGINE = Path(sys.argv.pop(1)).resolve()
TIMEOUT = 5


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
        expected = [
            "8 r n b q k b n r",
            "7 p p p p p p p p",
            "6 . . . . . . . .",
            "5 . . . . . . . .",
            "4 . . . . . . . .",
            "3 . . . . . . . .",
            "2 P P P P P P P P",
            "1 R N B Q K B N R",
            "  a b c d e f g h",
        ]
        for _ in range(2):
            self.send("d\n")
            for line in expected:
                self.expect(line)
            self.send("isready\n")
            self.expect("readyok")
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
