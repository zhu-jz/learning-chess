# Learning Chess

A chess engine built one small, readable C lesson at a time. We use ordinary
functions, arrays, structures, and explicit control flow, with explanations for
the decisions that are not obvious from the code.

Our eventual target is to reproduce the engine in
`minifish-reference-2026-11-05`: ordinary chess, one search worker, and exact
fixed-depth search results under matching settings and initial state. This
includes each iteration's node count, score, bounds, selective depth, principal
variation, and final best move and ponder move. Elapsed time and search speed
are excluded. Intermediate lessons do not yet reproduce the full engine.

## Lesson 1: talking to a chess GUI

UCI (Universal Chess Interface) is a text protocol. A GUI writes one command
per line to the engine's standard input; the engine answers on standard output.
The first lesson introduced the initial conversation:

| Command | Response |
| --- | --- |
| `uci` | Engine identification, followed by `uciok` |
| `isready` | `readyok` |
| `quit` | Exit successfully |

Empty lines and unknown commands are ignored. End of input also exits cleanly.
There is no move selection or search yet; `isready` simply confirms that the
command loop is ready.

`main.c` contains the whole program. `read_command` reads a complete line before
the loop examines its first word. Spaces, tabs, and Windows carriage returns
are accepted as separators. The buffer holds at most 4095 bytes before the
newline. Longer lines are consumed and ignored in full: a trailing `quit`, for
example, must never become a separate command. A final line without a newline
is also handled.

After a response, `fflush(stdout)` sends buffered output immediately. This
matters when a GUI connects through pipes and waits for a reply before sending
anything else.

## Lesson 2: representing the board

`struct Position` now holds an array of 64 squares. Each element stores a named
piece value from `enum Piece`, or `EMPTY`. An enum gives names to integer values;
`EMPTY` is zero, and the following names receive successive values.

Files (columns a through h) and ranks (rows 1 through 8) are counted from zero
inside the program. We locate a square with `rank * 8 + file`:

| Square | File | Rank | Array index |
| --- | --- | --- | --- |
| a1 | 0 | 0 | 0 |
| h1 | 7 | 0 | 7 |
| a2 | 0 | 1 | 8 |
| e1 | 4 | 0 | 4 |
| h8 | 7 | 7 | 63 |

`set_start_position` clears the board, then places both back ranks and the
pawns. It receives the address of the position (`&position`), so assignments
through `position->board` change that position rather than a separate copy.
The position currently stores only piece placement; later lessons will add
the side to move and other state needed to describe a chess game.

The new `d` command displays the board. This is a manual inspection command,
not a standard UCI command. Enter it while the engine is running:

```text
8 r n b q k b n r
7 p p p p p p p p
6 . . . . . . . .
5 . . . . . . . .
4 . . . . . . . .
3 . . . . . . . .
2 P P P P P P P P
1 R N B Q K B N R
  a b c d e f g h
```

Uppercase letters represent White's pieces, lowercase letters represent
Black's, and a dot marks an empty square. `N` means knight; `K` means king.
`print_board` visits the ranks in reverse order to show Black at the top and
White at the bottom. Its `const` parameter prevents it from changing the board
through that pointer.

The engine initializes the starting position once at startup. FEN parsing,
the UCI `position` command, and moving pieces are left for later lessons.

## Build and try it

From the project root in PowerShell, using MSYS2 UCRT64 GCC:

```powershell
New-Item -ItemType Directory -Force build | Out-Null
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
& C:\msys64\ucrt64\bin\gcc.exe -std=c11 -Wall -Wextra -Wpedantic -O0 -g main.c -o build\learning-chess.exe
.\build\learning-chess.exe
```

Enter `uci`, `isready`, and `quit`, each on its own line. The replies should be:

```text
id name Learning Chess
id author Learning Chess contributors
uciok
readyok
```

The C source uses only the standard library. On a system with GCC available as
`gcc`, the equivalent build command is:

```sh
mkdir -p build
gcc -std=c11 -Wall -Wextra -Wpedantic -O0 -g main.c -o build/learning-chess
```

## Test

The tests use Python's standard library; no packages need to be installed.
After compiling, run:

```powershell
.\.venv\Scripts\python.exe tests\test_uci.py build\learning-chess.exe
```

On other systems, use `python3 tests/test_uci.py build/learning-chess`.
The tests talk to a running process through pipes, checking that replies arrive
before input closes. They also check whitespace, unknown commands, oversized
lines, `quit`, and end of input. The board test checks every displayed rank,
piece, and coordinate label, then repeats the display to check that inspection
leaves the position unchanged.
