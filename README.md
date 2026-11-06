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
In lesson 2 the position stored only piece placement. Lesson 3 adds the
side to move and other state needed to describe a chess game.

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

The engine initializes the starting position at startup. The display above
shows the board portion; lesson 3 adds the position's other fields below it.

## Lesson 3: reading a position from FEN

FEN (Forsyth-Edwards Notation) describes a position with six fields. For example:

```text
r3k2r/8/8/3pP3/8/8/8/R3K2R w Kq d6 17 42
```

| Field | Example | Meaning |
| --- | --- | --- |
| Pieces | `r3k2r/.../R3K2R` | Ranks 8 to 1, separated by `/`; digits count empty squares |
| Side to move | `w` | `w` for White, `b` for Black |
| Castling rights | `Kq` | White kingside and Black queenside rights |
| En passant target | `d6` | Target square after a pawn's two-square advance |
| Halfmove clock | `17` | Plies since the last pawn move or capture |
| Fullmove number | `42` | Starts at 1 and increases after Black's move |

Castling uses the letters `KQkq`, in that order, omitting unavailable rights.
A single `-` means no rights or no en passant target. A castling right does not
mean castling is legal immediately. FEN records an en passant target even when
no opposing pawn can capture there. These conventions come from the
[FEN specification, section 16.1](https://www.saremba.de/chessgml/standards/pgn/pgn-complete.htm#c16.1).

Try these commands in the running engine:

```text
position fen r3k2r/8/8/3pP3/8/8/8/R3K2R w Kq d6 17 42
d
position startpos
d
quit
```

The first `d` shows the supplied board followed by:

```text
Side to move: White
Castling: Kq
En passant: d6
Halfmove clock: 17
Fullmove number: 42
```

`position startpos` restores every field to its initial value. Successful
`position` commands do not print a response; use `d` to inspect the result.

`read_position_command` continues splitting the command line with `strtok`.
Calling `strtok` with `NULL` continues from its previous location; the six
field pointers refer to words inside the existing line buffer. `read_fen`
then reads those fields explicitly, in order. Castling rights are four integer
flags, and an en passant square of `-1` means no target.

The parser checks eight ranks of eight squares, piece letters, side, castling
syntax, and en passant coordinates. The target must be on rank 6 with White
to move or rank 3 with Black to move. Counters must contain decimal digits,
fit in an `int`, and have a fullmove number of at least 1. `read_number` checks
for overflow before multiplying by ten, so even very long input is safe.
These are format checks, not a full chess legality check: king counts, check,
and whether pieces support the claimed rights will be addressed with the rules.

All parsing happens in a temporary `struct Position`. Only a successful command
reaches `*position = next`, which copies the entire structure, including its
arrays. A malformed command prints `info string Invalid position command`
and preserves the previous position. Trailing text is rejected too. Move lists
such as `position startpos moves e2e4` report that moves are not supported yet
and also preserve the previous position.

## Lesson 4: detecting attacked squares

`is_square_attacked` answers whether a given color attacks a square. It scans
the 64 squares, finds pieces of that color, and asks `piece_attacks_square`
whether each piece reaches the target. Both functions inspect the position
without changing it, and the answer is independent of whose turn it is.

The new `attacked` inspection command takes a square and `white` or `black`.
Like `d`, this is an extra command for studying the engine, not standard UCI.
For example:

```text
position startpos
attacked e3 white
attacked e4 white
attacked e6 black
```

The replies are:

```text
info string attacked e3 white yes
info string attacked e4 white no
info string attacked e6 black yes
```

The pawns on d2 and f2 attack e3. Their ability to move forward is a separate
rule. The parser requires a square from a1 to h8, a color, and no extra words;
invalid input reports `info string Invalid attacked command`.

For each piece, the code subtracts the starting file and rank from the target
file and rank. The signs show direction; `abs` from `<stdlib.h>` gives the
distances. This keeps the two board dimensions explicit: h1 and a2 are adjacent
array entries, but their files are seven columns apart.

| Piece | Attack pattern |
| --- | --- |
| White pawn | One file sideways and one rank upwards |
| Black pawn | One file sideways and one rank downwards |
| Knight | One file and two ranks, or two files and one rank |
| King | One square in any direction |
| Bishop | Equal file and rank distances |
| Rook | Same file or same rank |
| Queen | Bishop or rook pattern |

A piece never attacks its own square. For bishops, rooks, and queens, matching
the pattern is only the first step. The code then walks towards the target,
one square at a time. Any intervening piece blocks the attack. The walk stops
before the target, so the first occupied square is still attacked or defended.
Because alignment is checked first and both endpoints are valid, the walk
stays on the board. Knights skip this walk and can jump over pieces.

Attack detection is not legal move generation. Pawns attack empty diagonal
squares, kings attack adjacent squares, and pinned pieces still count as
attackers. The latter rule is explicit in
[FIDE's Laws of Chess, article 3.1.3](https://handbook.fide.com/chapter/e012023).
We also count a friendly occupied target as defended. These conventions will
let us test a king's safety after making a candidate move. This lesson does
not yet generate moves or report check automatically.

## Lesson 5: detecting check

Check detection builds directly on attack detection: find the king's square,
then ask whether the opposing color attacks it. `find_king` scans the board
for the requested king. `is_in_check` selects the opponent and calls
`is_square_attacked`, so it needs no new piece movement rules.

The new `check white` and `check black` inspection commands ask about the named
color, regardless of whose turn it is. Like `attacked`, these are extra study
commands, not standard UCI. For example:

```text
position fen k3r3/8/8/8/8/8/8/4K3 w - - 0 1
check white
check black
```

The black rook on e8 attacks the white king on e1, giving these replies:

```text
info string check white yes
info string check black no
```

`is_in_check` returns `1` for check and `0` for no check. Our FEN reader also
accepts incomplete study positions, so there is a third result: `-1` means
the requested color has no king or more than one king. `find_king` scans beyond
the first match to catch duplicates, and `is_in_check` handles this error before
using the square as a board index. The command then reports, for example:

```text
info string Cannot check white: expected exactly one king
```

In C, any nonzero value is true, including `-1`. The command therefore tests
the error result explicitly before printing yes or no. This distinction keeps
an invalid king count from being mistaken for check or a safe king.

Only the requested color's king count is checked. This is an inspection of
that king, not a full position validator: adjacent kings can both report check,
and a pinned enemy piece still counts as an attacker. Neither the position nor
its side to move changes. Checkmate and stalemate will require legal move
generation in later lessons.

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
leaves the position unchanged. FEN tests cover every position field, resetting
the position, en passant without a capturer, counter limits, and rejection of
malformed input or unsupported move lists without changing the current state.
Attack tests check every target square for isolated pieces of both colors,
central and edge positions, blockers in all eight sliding directions, knight
jumps, pinned pieces, and command validation. Some fixtures intentionally omit
kings to isolate a single piece's geometry; they are not complete legal games.
Check tests cover both colors, each attacking piece, blockers, pinned attackers,
adjacent kings, missing or duplicate kings, and inspection without changing
the position.
