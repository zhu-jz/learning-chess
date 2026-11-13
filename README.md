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
In lesson 1 there was no move selection or search; `isready` simply confirmed
that the command loop was ready. Lesson 14 adds a bounded search command.

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
and preserves the previous position. Lesson 3 rejected trailing text and move
lists too; lesson 11 adds support for `position ... moves ...`.

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

## Lesson 6: ordinary non-pawn moves

`struct Move` holds two square indices, `from` and `to`. Lesson 6 introduced
`generate_non_pawn_moves` to fill an array of these structures for the side to
move. It covered knights, bishops, rooks, queens, and ordinary one-square king
moves. Lesson 7 extends the same function and renames it `generate_moves`.

The `moves` inspection command takes no arguments. At the starting position:

```text
position startpos
moves
```

At the lesson 6 commit, the response was:

```text
info string moves non-pawn b1a3 b1c3 g1f3 g1h3
```

Each move is printed as its starting square followed by its destination.
Only the four knight moves appeared because pawn moves were outside that lesson.
The `non-pawn` label made that limitation visible. This is an extra study
command; it does not apply moves. Lesson 14 adds the separate UCI `go` command.

The lesson 6 generator considers each starting square, then each destination square.
It skips pieces of the wrong color and pawns, rejects friendly destinations
and king captures, then reuses `piece_attacks_square` to check movement and
blockers. For these pieces, the ordinary movement pattern is the attack
pattern. Pawns will need separate rules for forward moves and captures.

Each remaining candidate goes through `move_keeps_king_safe`. That function
copies the position, clears the starting square, and places the moving piece
on the destination, replacing any captured piece. It keeps the move only when
`is_in_check` returns exactly zero for the moving side. Testing the resulting
board handles pins, capturing a checking piece, blocking a check, and attacks
uncovered when the king vacates its square.

This copy is only for the king-safety test. It does not update the side to move,
clocks, en passant target, or castling rights, because attack detection does not
use them. Complete move application will be a separate lesson. The original
position, including all of its metadata, stays unchanged.

The caller provides an array of `MAX_MOVES` entries and receives a count.
The bound in lesson 6 was deliberately generous: 64 starting squares times 64
destinations. Each pair produced at most one move; lesson 8 expands this bound
for promotions. The function returns `-1` if either color
does not have exactly one king, and the command reports that error explicitly.
It does not otherwise validate whether the position could arise in a game.

The list currently follows increasing source and destination indices. Matching
the reference engine's move order will be a later step toward search parity.
At this stage an empty list meant only that no supported non-pawn moves were
found; it could not establish checkmate or stalemate while pawn moves were missing.

## Lesson 7: pawn advances and ordinary captures

`generate_moves` now includes pawns. Its `pawn_can_move` helper handles their
movement separately from attacks. White advances towards higher ranks and
Black towards lower ranks; the code expresses this with a `direction` of `1`
or `-1`.

A pawn can advance one square on its file if the destination is empty. From
rank 2 for White or rank 7 for Black, it can advance two squares when both the
intermediate square and the destination are empty. A double advance checks
the starting rank, so a pawn farther up the board cannot use it.

For an ordinary capture, the destination must contain an enemy piece and be
one square diagonally forwards. `piece_attacks_square` already describes that
diagonal pattern, so the helper reuses it. The generator's shared checks still
exclude friendly targets and king captures. Pawns cannot capture forwards or
move diagonally into an empty square.

In lesson 7 the `moves` response began with `info string moves ordinary`. At the
starting position it lists 20 moves: four knight moves and sixteen pawn
advances. For example, to inspect just a pawn and the two kings:

```text
position fen 7k/8/8/8/8/8/4P3/7K w - - 0 1
moves
```

At the lesson 7 commit, the response was:

```text
info string moves ordinary h1g1 h1g2 h1h2 e2e3 e2e4
```

Every candidate still passes through `move_keeps_king_safe`. A pinned pawn
may advance if it continues to shield its king, but may not expose that king
by capturing sideways. A pawn can also answer check by blocking an attack or
capturing the checking piece. For a double advance, king safety is tested on
the final board, not on a hypothetical board with the pawn stopped halfway.

Lesson 7 omitted promotions, en passant, and castling. Pawn moves to the last
rank were omitted entirely until `Move` could record a promotion piece.
Lesson 8 adds those moves. Pawns already on an end rank in a study position
still produce no moves.

Move generation continues to leave the position unchanged. Lesson 11 adds
move application, including updating clocks and other position fields.

## Lesson 8: promotions

`Move` now has a `promotion` field. For an ordinary move it is `EMPTY`; for a
promotion it holds the new colored piece, such as `WHITE_QUEEN` or
`BLACK_KNIGHT`. Storing the actual piece makes updating the copied board a
single assignment.

`pawn_can_move` now permits a pawn to reach its last rank. Once the generator
finds such a candidate, it emits four choices: knight, bishop, rook, and queen.
The two explicit promotion arrays list the allowed pieces for each color.
There is no choice to remain a pawn or become a king. The same loop handles
both forward promotions and capture promotions.

Every choice passes through `move_keeps_king_safe`. After moving the pawn and
removing any captured piece, that function replaces the pawn with the chosen
piece on the temporary board. The original position remains unchanged.

Move printing adds a lowercase `n`, `b`, `r`, or `q` suffix for either color.
For example, `e7e8q` promotes White's pawn to a queen, while `e2e1n` promotes
Black's pawn to a knight. `tolower` from `<ctype.h>` converts our colored piece
symbol to the lowercase suffix. Ordinary moves still have four characters.

Try:

```text
position fen 7k/4P3/8/8/8/8/8/7K w - - 9 12
moves
```

At the lesson 8 commit, the response was:

```text
info string moves partial h1g1 h1g2 h1h2 e7e8n e7e8b e7e8r e7e8q
```

Lesson 8 introduced the `partial` label because en passant and castling were
still missing. Lessons 9 and 10 add those moves; lesson 11 adds complete move
application.

`MAX_MOVES` is now `64 * 64 * 4`: at most four choices per pair of squares.
This simple bound continues to accommodate unusual study positions. It uses
more space than a practical chess engine needs, but its safety is easy to see.
Choices currently appear in knight, bishop, rook, queen order; matching the
reference's move ordering remains a later step toward search parity.

## Lesson 9: en passant

An en passant capture moves a pawn diagonally into an empty square and removes
the opposing pawn beside its starting square. Unlike an ordinary capture, the
captured piece is not on the destination. `Move` therefore gains an
`en_passant_capture_square` field. It is `-1` for other moves, or the square of
the pawn to remove for en passant.

`find_en_passant_capture` checks that the destination matches the position's
FEN target and is empty. White must move from rank 5 to rank 6; Black must move
from rank 4 to rank 3. The pawn must attack the target diagonally, and an opposing
pawn must occupy the square just behind it. The function returns that captured
square, or `-1` when the conditions do not hold.

The generator accepts either an ordinary pawn move or a valid en passant
candidate. It records the extra capture square before calling
`move_keeps_king_safe`. The copied board now changes in three places: the
capturing pawn leaves its starting square, arrives at the target, and the
captured pawn disappears. Only then does the code test for check.

That order matters. Removing two pawns from a rank can uncover a rook's attack
on the king. Removing the captured pawn can uncover a bishop's diagonal.
Conversely, en passant can answer check by removing the checking pawn or by
placing the capturing pawn between a sliding piece and the king. The existing
attack detector handles all of these cases on the resulting board.

Try:

```text
position fen 7k/8/8/3pP3/8/8/8/7K w - d6 0 1
moves
```

At the lesson 9 commit, the response was:

```text
info string moves partial h1g1 h1g2 h1h2 e5d6 e5e6
```

The en passant move is printed simply as `e5d6`, with no extra suffix. The
inspection command still leaves the position unchanged, including its FEN
target. The reader trusts that target as supplied history; it does not prove
that the preceding move was a double pawn advance. The new board checks reject
an absent or incorrect captured pawn. An occupied destination may still be an
ordinary capture, but it never removes a second piece as en passant.

En passant is available only immediately after the opposing double advance.
Lesson 11 maintains that lifetime by setting and clearing the target after
moves. Loading a FEN without the target also removes the opportunity.
Lesson 9 kept the `partial` label until castling was added.

## Lesson 10: standard castling

Castling moves the king two squares towards a rook, then places that rook on
the square the king crossed. We support standard chess: White's king starts
on e1 and Black's on e8, with rooks on the a- or h-file. Chess960 is outside
this project's current scope.

`Move` gains `rook_from` and `rook_to`, both `-1` for other moves. For example,
White's kingside castle records king movement e1-g1 and rook movement h1-f1.
The printed move is simply `e1g1`. The other castles print as `e1c1`, `e8g8`,
and `e8c8`.

`prepare_castling_move` checks the corresponding FEN right, the king's home
square, and a matching rook on its home square. Every square between them
must be empty. It then checks the king's starting square and tests the crossing
square on a copy with the king moved and the rook still at home. If these
checks pass, it records the rook movement. The generator's usual
`move_keeps_king_safe` call checks the final board, now with both pieces moved.

The three king squares must be safe. The rook's square may be attacked.
On the queenside, b1/b8 must be empty but need not be safe, since the king
crosses d1/d8 and finishes on c1/c8. These distinctions follow
[FIDE's castling rules, article 3.8.2](https://handbook.fide.com/chapter/e012023).

Try:

```text
position fen r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1
moves
```

The list contains 26 moves, including `e1c1` and `e1g1`. Changing the active
color to `b` gives Black's 26 moves, including `e8c8` and `e8g8`.

All standard move types are now represented, so responses begin with
`info string moves legal`. This describes moves from a valid standard-chess
position. The FEN reader still permits study boards that could not arise in
a game, and move generation is not a complete position validator.

Generation remains read-only: castling rights, clocks, and the actual board
do not change when `moves` is called. As with en passant history, the engine
trusts the supplied FEN rights. Updating those rights when a king or rook moves,
or a rook is captured, is covered by lesson 11. This lesson adds neither
game-result reporting nor search.

## Lesson 11: applying moves and reading move lists

The engine now accepts move sequences after either starting-position form:

```text
position startpos moves e2e4 e7e5 g1f3 b8c6 f1b5
d
position fen 7k/3p4/8/4P3/8/8/8/7K b - - 9 12 moves d7d5 e5d6
d
```

The first sequence reaches the position after 3. Bb5, with Black to move,
a halfmove clock of 3, and fullmove number 3. The second sequence includes
White capturing en passant; the white pawn finishes on d6 and the black pawn
disappears from d5. Black is then to move at fullmove number 13.

`play_uci_move` generates the legal moves and compares their coordinate strings
with the next input word. The selected `Move` already contains any promotion,
en passant, and castling details. This keeps the command parser from having to
duplicate the chess rules. `move_to_string` is shared by this matching step and
the `moves` inspection command. Promotion suffixes must be lowercase and are
required when promoting; castling uses the king's start and destination.

`move_pieces` contains the board changes shared by king-safety testing and full
move application. `apply_move` saves the moving and captured pieces before
changing the board, then updates the rest of the position:

- A king move removes both of that color's castling rights.
- A rook leaving its home square, or being captured there, removes the
  corresponding right. Returning a piece never restores a right.
- Every move clears the old en passant target. A double pawn advance sets
  the crossed square as the new target, even without a potential capturer.
- Pawn moves and captures reset the halfmove clock; other moves increment it.
- Black's move increments the fullmove number. Every move changes the turn.

The king-safety test continues to use only `move_pieces` on a copy: it asks about
the moving side's king before changing turns. Full application uses the same
board operations and adds the metadata updates, so special moves have only one
piece-placement implementation.

The whole `position` command is atomic. The parser loads its starting position
and plays all supplied moves on a temporary structure. Only when every move
succeeds does it replace the current position. For example, an illegal third
move leaves the engine's previous position unchanged, rather than installing
the position after the first two moves. Errors report
`info string Cannot play move <token>`. An empty `moves` list is accepted.

The existing FEN reader accepts counters up to `INT_MAX`. `apply_move` checks
before incrementing either counter and refuses a move that would overflow,
without changing anything. A pawn move or capture can still reset a maximum
halfmove clock. Counter overflow produces the same command error as an invalid
or illegal move. Ordinary game counters do not approach this boundary.

Successful commands remain silent; use `d`, `check`, or `moves` to inspect the
result. Lesson 11 updates current position state. Later lessons build search
and draw handling on top of it.

## Lesson 12: counting legal move sequences with perft

`perft` counts legal move sequences of exactly a given length. It combines
move generation and move application, making it a useful check before adding
search. It does not evaluate positions or choose a move.

Try:

```text
position startpos
perft 3
```

The response is:

```text
info string perft depth 3 nodes 8902
```

Depth is measured in plies: one move by either player. At depth 0 there is
exactly one sequence, the empty sequence, so the answer is 1. At depth 1 the
answer is the number of legal moves. At greater depths the function applies
each move to a copy and recursively counts the remaining moves. Adding the
child counts gives the result. A position with no legal moves contributes zero
at any positive depth, even if it was reached before the requested depth.

This counts the final positions of move sequences, not distinct boards. Two
different sequences reaching the same board are counted separately. Perft
does not stop for repetition, move-count draws, or insufficient material.
These counts are also different from the nodes visited by a searching engine.

The recursive function calls `generate_moves` and `apply_move` directly. Each
child has its own copied `Position`, so there is no undo operation and the
original position stays unchanged. The command resets the two move counters
on its root copy: they do not affect legal moves, and this avoids overflow
when a supplied FEN already has maximum counters. Board state, side to move,
castling rights, and the en passant target are preserved on that copy.

The large `MAX_MOVES` array cannot safely be a local array at every recursive
level. This lesson introduces `malloc` to obtain its storage from the heap.
The returned pointer is checked against `NULL` before use, and every exit
after allocation calls `free`. Only the lists on the active recursion path
remain allocated. Parent and child lists are separate, so generating replies
cannot overwrite the moves the parent is still visiting.

The result uses `unsigned long long`, with a check before each addition.
An allocation or counting failure reports `info string Cannot complete perft`
without printing a partial result. The command accepts depths 0 through 6,
requires exactly one king of each color, and rejects extra arguments. The
depth limit bounds recursion; large traversals can still take time. Perft runs
synchronously, so begin with small depths when inspecting a new position.

The tests use the same four fixtures as the reference snapshot's
[test_engine.py](minifish-reference-2026-11-05/test_engine.py):

| Position | Depth | Nodes |
| --- | ---: | ---: |
| Starting position | 4 | 197,281 |
| Castling (Kiwipete) | 3 | 97,862 |
| En passant endgame | 4 | 43,238 |
| Promotion position | 3 | 9,467 |

All four counts match. This verifies those move trees; exact search parity
still requires the reference evaluation, ordering, and search algorithms in
later lessons.

## Lesson 13: material evaluation

`evaluate` gives the position a simple material score. This is a teaching
evaluator for the first search lessons, not the reference engine's NNUE
evaluation. Its deliberately simple piece values are:

| Piece | Value in centipawns |
| --- | ---: |
| Pawn | 100 |
| Knight | 300 |
| Bishop | 300 |
| Rook | 500 |
| Queen | 900 |
| King | 0 |

One centipawn is one hundredth of our pawn value. These numbers are fixed
weights for this lesson, not a claim that a piece has the same practical worth
in every position. The king has no material price: it cannot be traded, and
search handles checkmate separately in lesson 14. Empty squares also contribute zero.

The function scans the board, adds White's material, and subtracts Black's.
It then negates the result if Black is to move. A positive score therefore
means a material advantage for the side to move; a negative score means a
material disadvantage. This convention will let negamax search reverse the
sign when considering the opponent's reply.

The new `eval` inspection command takes no arguments. For example:

```text
position fen 7k/8/8/8/8/8/4P3/7K w - - 0 1
eval
position fen 7k/8/8/8/8/8/4P3/7K b - - 0 1
eval
```

The replies are:

```text
info string eval material cp 100
info string eval material cp -100
```

Both describe the same material advantage for White. The sign changes because
the side to move changes. The initial position scores zero, as do positions
with balanced material under these weights.

The evaluator reads the board and side to move without modifying the position.
It ignores piece placement, mobility, king safety, clocks, castling rights,
and the en passant target. It also does not validate king counts or determine
checkmate, stalemate, or draws. Even a checkmated position receives only its
material score from `eval`; terminal game results belong to search.

The `material` label makes the command's scope explicit. No NNUE network is
loaded and these scores are not expected to match Minifish yet.

## Lesson 14: fixed-depth negamax search

The engine can now choose a move using `go depth N`, for depths 1 through 4.
For example:

```text
position startpos
go depth 2
```

The response in lesson 14 was (lesson 15 reduces the node count):

```text
info depth 2 score cp 0 nodes 421
bestmove b1a3
```

In this lesson, `negamax` examines every legal move, applies it to a copied
position, and calls itself with one less ply remaining. The child reports a
score from the opponent's perspective, so the parent negates it. The move with the greatest
negated score is the parent's best move. This single rule handles both colors.

At depth zero the function uses the material evaluator, but first checks
whether there are any legal moves. No moves while in check means checkmate;
no moves without check means stalemate. Checking these cases before evaluating
material is essential for recognizing a mate on the final searched move.

Checkmate scores use `-MATE_SCORE + ply`, where `ply` is the distance from the
search root. `MATE_SCORE` is 100,000, larger than any possible material score
under the teaching weights. After signs alternate back to the root, a shorter
winning mate scores higher than a longer one. When all choices lose to mate,
the engine prefers delaying it. Stalemate scores zero.

The command reports material results as `score cp`, from the root side's
perspective. Forced mates are reported as `score mate` in whole moves: positive
for a winning mate, negative for a losing mate. A root that is already
checkmated reports `mate 0`. Both a checkmated root and a stalemated root return
`bestmove 0000`, because no legal move exists.

In lesson 14, `SearchResult` holds the score, chosen move, and a flag saying
whether a move was selected. Lesson 19 replaces that move and flag with a
move sequence and its length. Terminal positions and the original depth-zero
leaves have no selected move.
The function returns success separately so allocation or counter failures are
not mistaken for chess scores. Lists are allocated and freed as in perft.

Each invocation counts as one search node, including the root and depth-zero
leaves. Starting-position depth two therefore visits `1 + 20 + 400 = 421`
nodes. These are search-node counts, not perft's final-leaf counts. Strictly
greater scores replace the best move; ties keep the first generated move.
Thus `b1a3` wins the starting-position tie by enumeration order, not because
the material evaluator understands opening development.

This first search is deliberately limited. It has no alpha-beta pruning,
quiescence search, move ordering, iterative deepening, or NNUE. It handles
checkmate and stalemate, but does not yet adjudicate repetition, move-count
draws, or insufficient material. In lesson 14, counters are normalized on the
root copy, as in perft, while the current position stays unchanged. Lesson 21
keeps the halfmove clock for draw detection. These teaching scores and node
counts are not yet expected to match Minifish's search.

The command runs synchronously and accepts only `go depth 1` through
`go depth 4`. Other limits and extra arguments are rejected. During a search,
input commands wait until it finishes; `stop`, clock-based play, and responsive
input handling belong to later lessons. The depth cap keeps the
teaching search bounded. Reported depth is the requested limit, even when a
terminal root finishes immediately.

## Lesson 15: skipping branches with alpha-beta pruning

Alpha-beta pruning keeps the same fixed-depth answer while avoiding work that
cannot improve the root's choice. The search still uses negamax, copied
positions, material evaluation, and the same move order. The change adds two
integer parameters and a stopping condition to the existing loop.

`alpha` is the score threshold we need to improve. `beta` is the threshold
where the caller already has enough information to reject this branch.
For example, suppose the root has found a move worth 100. While examining
another move, we find an opponent reply worth -50 to the opponent, or 50 to
the root. The opponent can choose that reply, so this root move is worth at
most 50. There is no need to examine the opponent's remaining replies: the
root already has its 100-point alternative.

Changing sides negates scores and reverses their order. A parent's window
`alpha, beta` therefore becomes `-beta, -alpha` in the recursive call.
After each reply, we negate its score, update the best result, and raise
`alpha` if the score improves it. Once `alpha >= beta`, we stop the loop.
Equality is enough: a tied alternative cannot replace our first choice.
The move list is freed after the loop, including when it ends early.

The function returns the best score it encountered, even if that score lies
outside the original window. This convention is called *fail-soft*. An
internal call's score need not be exact: a result at or below its original
alpha is an upper bound, and a result at or above beta is a lower bound.
A result strictly inside the window is exact. The caller needs these bounds
only to decide whether to continue searching a branch.

At the root, the window starts at `-INFINITE_SCORE, INFINITE_SCORE`, beyond
every material and mate score. The root result is exact. A later root move
whose bound merely ties the best score cannot replace it, because the move
update still uses a strict `>` comparison. The chosen move therefore matches
exhaustive negamax with the same move order, including its first-move tie rule.
Lesson 16 changes that order and can therefore select a different tied move.

Starting-position `go depth 2` now prints:

```text
info depth 2 score cp 0 nodes 60
bestmove b1a3
```

The first root move visits its child and all 20 replies. Each of the other
19 root moves needs only its child and one reply to establish a tie:
`1 + 21 + 19 * 2 = 60` nodes. Our node definition is unchanged; skipped
recursive calls simply do not contribute to the count.

These counts describe lesson 15, before adding move ordering:

| Starting-position depth | Exhaustive negamax | Alpha-beta |
| --- | ---: | ---: |
| 1 | 21 | 21 |
| 2 | 421 | 60 |
| 3 | 9,323 | 546 |
| 4 | 206,604 | 2,215 |

The lesson 15 tests retain scores and chosen moves recorded from lesson 14's
exhaustive engine, and check that pruning never visits more nodes on those deeper fixtures.
Some trees offer no opportunity to prune, so their counts stay the same.
They cover opening play, castling rights, recaptures, promotions, en passant,
and winning and losing mates. Starting-position node counts are also exact
regressions for this move order and pruning rule.

The search still accepts depths 1 through 4 and runs synchronously. The earlier
limitations on evaluation and draw handling remain. Lesson 16 adds move ordering
to improve pruning further. These node counts describe
our teaching search; they do not yet reproduce Minifish.

## Lesson 16: searching captures and promotions first

Alpha-beta can stop a branch sooner when it finds a strong reply early.
`move_priority` gives each move a simple priority: the material gained
immediately by capturing a piece or promoting a pawn. `order_moves` then
places larger priorities first. Both functions use the existing teaching
piece values.

| Move | Priority |
| --- | ---: |
| Quiet move, including castling | 0 |
| Capture a pawn, including en passant | 100 |
| Capture a rook | 500 |
| Capture a queen | 900 |
| Promote to knight or bishop without capturing | 300 - 100 = 200 |
| Promote to rook without capturing | 500 - 100 = 400 |
| Promote to queen without capturing | 900 - 100 = 800 |
| Capture a rook and promote to queen | 500 + 800 = 1,300 |

En passant needs special attention: the destination is empty, so we read the
captured pawn from `en_passant_capture_square`. For a promotion, subtracting
the pawn's value counts only the extra material gained. A capture promotion
gets both contributions. Piece values are the same for both colors.

This priority is a guess about search order. It does not consider whether a
captured piece is defended, the value of the capturing piece, or king safety
beyond the legal-move check already done by the generator. Search still
determines the score. For example, the defended-rook exercise from lesson 14
tries the rook capture first but rejects it after seeing the recapture at
depth two. No moves are removed by ordering.

`order_moves` uses insertion sort. Before iteration `i`, the moves before `i`
are already ordered. Save move `i`, shift each preceding move with a lower
priority one place to the right, then put the saved move into the gap.
Equal priorities are never shifted past each other. This makes the sort
*stable*: it preserves generation order within each priority. It uses a
temporary move and ordinary loops, with no extra array or comparison callback.
For `n` moves it can take roughly `n * n` comparisons; this is a readability
choice, and sorting work is not included in search-node counts.

In lesson 16, search orders its local list after the terminal and depth-zero
checks. Lesson 17 lets depth-zero positions continue with quiescence search.
Leaves that search no children need no sorting. Move generation,
the `moves` command, and perft retain their original enumeration order.

The strict `>` update in negamax now keeps the first **searched** move when
search scores tie. Different priorities can therefore change the chosen
move without changing the score. In the White promotion fixture
`7k/4P3/8/8/8/8/8/7K w - - 0 1`, depth three previously chose `h1g1`:
delaying promotion still reached the same material score. Now `e7e8q` is
searched first and keeps that tie. Equal priorities still use the old
generation order, so repeated searches remain deterministic.

Measured node counts in lesson 16, with the same evaluation and depth limits, are:

| Position and depth | Lesson 15 | With ordering |
| --- | ---: | ---: |
| Starting position, depth 4 | 2,215 | 1,594 |
| After `e4 e5 Nf3 Nc6 Bb5 a6`, depth 3 | 2,609 | 1,013 |
| Kiwipete from lesson 12, depth 3 | 9,067 | 2,542 |

Ordering does not guarantee fewer nodes on every position, and fewer nodes
alone do not guarantee faster execution because sorting also costs work.
The tests check these specific reductions, preserve the earlier exhaustive
scores, and cover capture ties, en passant priority, promotion choices, and
unchanged move lists after search. Changed tied choices were also checked
with the previous engine by playing the new move and searching its replies.

Depth limits, synchronous UCI input, material evaluation, and the earlier draw
limitations are unchanged. This simple priority is not Minifish's complete
move-ordering policy; exact reference parity remains a later target.

## Lesson 17: continuing tactical exchanges with quiescence

A fixed-depth material leaf can stop halfway through a capture exchange.
In our defended-rook example, White can capture a rook with the queen, but
Black's pawn immediately recaptures the queen:

```text
position fen 7k/8/4p3/3r4/8/8/8/K2Q4 w - - 0 1
go depth 1
```

Lesson 16 chose `d1d5` and reported 800 centipawns because it stopped after
the apparent rook gain. The current response is:

```text
info depth 1 score cp 300 nodes 37
bestmove d1b1
```

Quiescence sees `e6d5` beyond the normal horizon and rejects the queen trade.
The ordinary `eval` command still reports material only; the extra tactical
work belongs to search.

We keep one recursive function. While `depth` is positive, `negamax` searches
all legal moves and reduces the remaining depth. At zero, it enters
quiescence mode and leaves the remaining depth at zero in recursive calls.
`ply` continues to increase, recording the real distance from the root.
This shares position copying, alpha-beta bounds, move ordering, and cleanup
without introducing a second copy of the search loop.

Every node first generates the complete legal list and checks for checkmate
or stalemate. An empty capture list would not be enough: there might still
be legal quiet moves. Terminal detection also runs before any material cutoff.

When the side to move is not in check, quiescence starts with the material
score as its baseline. This is called *stand pat*. If that score reaches
beta, the node can return immediately; otherwise it raises alpha if needed.
The search then considers legal captures and all four promotion choices.
En passant counts as a capture even though its destination is empty, and a
promotion counts even without a capture. Quiet non-promoting moves, including
quiet checks, are skipped in this mode.

Stand pat is a search assumption: we assume the player can avoid making the
position worse while declining an unfavorable exchange. It does not actually
pass a turn. This assumption can fail in zugzwang, where every legal move
worsens the position. Quiescence is an approximation, not full minimax over
all moves beyond the depth limit.

When in check, the player must respond, so there is no stand-pat baseline or
stand-pat cutoff. Search starts with `-INFINITE_SCORE` and considers **every**
legal evasion, including king moves and quiet blocks. Captures or promotions
that give check can therefore lead to a longer sequence of replies.

`MAX_SEARCH_PLY` limits the total distance from the root to 64 plies.
Lesson 22 also stops repetition cycles, but long sequences that do not repeat
still need this hard recursion bound.
At the limit, checkmate and stalemate still get their proper scores. Other
positions return material, including positions in check. That emergency
fallback can stop an unresolved exchange; it is a deliberate limitation of
this teaching search, not a declaration of a draw or a legal pass in check.
The bound limits recursion depth, not elapsed time or total node count.

Mate scores still use the actual `ply`. UCI mate detection now allows the
whole 64-ply range, because a mate can be found beyond the requested normal
depth. In lesson 17, `info depth` reports the requested depth. Nodes count
each visited position once, including the horizon and every quiescence continuation;
changing search mode does not count the same position again.

This lesson changes the searched tree, so scores, best moves, and node counts
can differ from lessons 14 through 16. For example, starting-position counts
for separate searches at depths 1 through 4 are 21, 62, 529, and 1,894.
Extending some branches can change later cutoffs, so total nodes need not
increase at every depth.
These are regression counts for our current engine, not Minifish parity.

The UCI tests cover the changed recapture behavior, promotions beyond the
horizon for both colors, deterministic counts, and the existing special moves
and terminal positions. A small C test program also starts search directly
at depth zero and at the recursion boundary. It checks stand-pat cutoffs,
quiet check evasions, en passant, underpromotion to avoid stalemate, mate
distance beyond normal depth, and terminal detection at the cap.

## Lesson 18: reaching the requested depth one step at a time

`go depth N` now searches depths 1, 2, and so on through N. This is called
*iterative deepening*. After each completed iteration, the engine reports its
score and the total nodes visited so far. It sends one `bestmove` after the
last iteration.

For example, in lesson 18, `position startpos` followed by `go depth 3` produced
the following output. Lesson 19 appends a move sequence to each search report.

```text
info depth 1 score cp 0 nodes 21
info depth 2 score cp 0 nodes 83
info depth 3 score cp 0 nodes 612
bestmove b1a3
```

The new loop is in `read_go_command`. Each call to `negamax` starts from the
same root position with a full alpha-beta window and a larger normal depth.
Quiescence still extends tactical lines beyond that depth. Move ordering and
search evaluation are unchanged, so each iteration gives the same answer
as a separate search at that depth in lesson 17.

The node counter starts at zero once per `go` command and is shared by all
iterations. It includes repeated work, including a root visit for each
iteration. Thus the starting-position totals are:

| Completed depth | Nodes in this iteration | Cumulative nodes reported |
| --- | ---: | ---: |
| 1 | 21 | 21 |
| 2 | 62 | 83 |
| 3 | 529 | 612 |
| 4 | 1,894 | 2,506 |

A new `go` command resets the counter. Searching does not play the chosen
move or change the current position. The `perft` command keeps its existing
meaning and output.

Each iteration writes into a temporary `SearchResult`. Only a successful
search replaces the saved result. This matters when a later search fails,
for example because a move-list allocation fails: the engine reports the
error and returns the best move from the last completed iteration. If the
first iteration fails, it reports the error without inventing a result.
An incomplete iteration never produces an `info depth` report.

The saved result is replaced even if the score becomes worse. Deeper search
can uncover a recapture or a forced loss; its result is more informed than
the shallower one. In our defended-rook fixture, depths one and two choose
`d1b1` with score 300, while depth three chooses `d1e1` with score 400. The
final response must use the depth-three move. Another fixture changes from
a material score at depth one to a losing mate score at depth two.

`print_search_info` holds the existing score-formatting code and flushes
standard output after each report. A GUI can therefore display a completed
iteration while the next one runs. Input handling is still synchronous:
`stop` and clock-based play remain later work. The supported depth range is
still 1 through 4.

For now, all requested iterations run, even when a mate is found or the root
has no legal moves. A terminal root visits one node per iteration and ends
with `bestmove 0000`. This keeps the loop simple and its output predictable.
Lesson 18 does not yet use a previous iteration's best move to improve ordering;
lesson 20 adds that step at the root. Iterative deepening provides progress
reports and a completed result to retain when interruption is added later.

Tests check the complete report sequence, cumulative counts, resets between
commands, changing scores and moves, and a single final `bestmove`. The
individual iteration results were also compared with the lesson 17 engine.
Exact Minifish parity remains a later target.

## Lesson 19: reporting the principal variation

A score tells us how the engine rates a position. The *principal variation*,
usually shortened to *PV*, shows the sequence of moves the search chose to
support that score. Each completed iteration now appends a `pv` field:

```text
position startpos
go depth 3
info depth 1 score cp 0 nodes 21 pv b1a3
info depth 2 score cp 0 nodes 83 pv b1a3 a7a5
info depth 3 score cp 0 nodes 612 pv b1a3 a7a5 a1b1
bestmove b1a3
```

Each sequence starts at the root position for that `go` command. The first
move belongs to the side to move; the remaining moves alternate sides.
Coordinates use the same format as `position ... moves`, including a final
promotion letter when needed. Castling uses the king's starting and ending
squares, and en passant uses the capturing pawn's squares.

`SearchResult` now contains `score`, `pv_length`, and `pv`, an ordinary array
of moves. A nonempty array already supplies the chosen move in `pv[0]`, so
we replace the old single-move field and flag rather than maintain duplicate
state. A zero length means there is no continuation selected at that node.

At entry, `negamax` sets the result's length to zero. Whenever a move improves
the best score, the function places that move in slot zero and copies the
child's sequence into the following slots. The new length is the child's
length plus one. For example, if we choose `b1a3` and the child returns
`a7a5 a1b1`, our result becomes `b1a3 a7a5 a1b1`. A tied score keeps the
previous sequence, just as it previously kept the first chosen move.

The array holds `MAX_SEARCH_PLY` moves. At a node at distance `ply`, at most
`MAX_SEARCH_PLY - ply` moves remain before the recursion limit. The child is
one ply closer to that limit, so prepending our move always fits. Terminal
nodes and nodes at the limit return an empty sequence. Stand pat also leaves
an empty sequence unless a searched move improves its score. Only entries
below `pv_length` are read; stale array contents beyond that length are ignored.

PV collection does not change which moves are searched, how they are ordered,
or when alpha-beta cuts off. Scores, node counts, and final best moves are
unchanged from lesson 18. Copying these short arrays adds some work, but makes
the connection between a parent result and a child result explicit.

The displayed line can be longer than the requested normal depth because
it includes quiescence continuations. For example, depth one in
`8/8/8/8/8/5k2/4p3/r6K w - - 0 1` reports `pv h1h2 e2e1q`: the king escape
is the normal search move, and the promotion is found by quiescence. A line
can also be shorter when it reaches a terminal position. The losing-mate
fixture at depth three reports `pv h8h7 g5g7`, ending at checkmate after two
plies. Quiescence stand pat and the recursion fallback remain approximations;
printing a line does not remove those limits on the score.

`print_search_info` prints the saved sequence before flushing the completed
iteration's report. A terminal root omits the `pv` field and ends with
`bestmove 0000`. Otherwise, the final `bestmove` is the first move of the last
completed iteration's PV. Each new iteration builds its own sequence, and a
failed iteration cannot replace the previously completed result.

Tests replay reported sequences through the legal move parser, check their
ending material scores or mate distances, and ensure the final move agrees
with the PV. They cover promotions, underpromotion, en passant, lines extended
by quiescence, terminal roots, and replacing a long line with a shorter one.
The C checks also verify lengths near the recursion limit. Comparisons with
lesson 18 confirm unchanged scores, node counts, and best moves.

PV reporting is another step toward the reference protocol. Lesson 20 uses
its first move to order the next iteration at the root. The remaining PV
moves and ponder reporting are still later work.
Exact Minifish parity remains a later target.

## Lesson 20: trying the previous best root move first

Iterative deepening now reuses one piece of information from its completed
search: the best move at the root. Trying it first in the next iteration can
establish a useful alpha bound sooner and let other branches cut off earlier.
It is still searched normally at the new depth; its old score is not reused.

Before each iteration, `read_go_command` sets a `preferred` move pointer to
`NULL`. After the first iteration, if the completed result has a PV, the
pointer refers to its first move. The next search writes into a separate
result, so the previous result remains available until the iteration completes.
Terminal roots have no PV and therefore no preference.

`negamax` first applies our ordinary capture-and-promotion ordering. Then
`search_move_first` looks for the preferred move in the generated legal list.
It matches the starting square, destination, and promotion piece. Promotion
matters because `a7a8n` and `a7a8q` are different moves. The generated move
supplies all details needed to apply it, including the captured pawn's square
for en passant and the rook's movement for castling. A missing match leaves
the list unchanged.

To put the matching move first, save it, shift all preceding moves one place
to the right, and fill slot zero with the saved move. This preserves the
relative order of every other move. Swapping the match with slot zero would
move the displaced move behind intervening moves and alter their tie order.
The helper searches a list at most once and shifts at most that many moves.

Only the root receives a preference. Recursive calls pass `NULL`, so all
replies keep the existing ordering. A new `go` command starts without a
preference, even if the engine just searched the same position. There is no
global move cache and no reuse of later moves from the old PV in this lesson.

The defended-rook fixture from lesson 17 illustrates the effect:

```text
position fen 7k/8/4p3/3r4/8/8/8/K2Q4 w - - 0 1
go depth 3
info depth 1 score cp 300 nodes 37 pv d1b1
info depth 2 score cp 300 nodes 208 pv d1b1 d5d2
info depth 3 score cp 400 nodes 1005 pv d1e1 d5d3 e1e6
bestmove d1e1
```

After depth one, the quiet move `d1b1` is searched before the tempting rook
capture `d1d5`. Cumulative counts at depths two and three fall from 212 and
1,023 to 208 and 1,005. Depth three still replaces the preferred move with
`d1e1` when it finds a better score. Starting-position counts stay unchanged
because its previous best move was already first.

This changes ordering, not the set of moves or the search depth, so scores
remain the same. Node counts and PVs can change, and the preferred move wins
if it ties the best score. The C tests demonstrate this with a supplied
`e2e4` preference in the starting position: it wins the zero-score root tie
instead of `b1a3`. Normal starting-position searches still choose `b1a3`.
As with capture ordering, improvement in node count is position-dependent.

Tests cover stable ordering, absent preferences, promotion identity,
castling and en passant details, and resetting the preference between
commands. The existing PV replay checks still pass, and comparisons against
lesson 19 preserve every iteration's score. Exact Minifish move ordering and
node-count parity remain later work.

## Lesson 21: recognizing the fifty-move draw in search

The FEN halfmove clock counts plies since the last pawn move or capture.
One move by each side is two plies, so fifty moves by each side corresponds
to 100 on this counter. Search now uses this field instead of resetting it
to zero on its root copy. The existing `apply_move` function already resets
it for pawn moves and captures, and increments it for other moves.

We follow the reference's search policy: below the root, a counter of 100
or more is scored as a draw unless the position is checkmate. The reference
implements the threshold and checkmate exception in `is_draw`, in both
`minifish-reference-2026-11-05/movegen.py` and
`minifish-reference-2026-11-05/minifish/position.c`. Its main search skips
this immediate-draw test at the root, where it still needs to choose a move.

In our `negamax`, the existing no-legal-moves test runs first. It returns
the mate score for checkmate or zero for stalemate. The new check then returns
zero if `ply > 0` and `halfmove_clock >= 100`. This also applies at the
quiescence horizon and before the hard recursion fallback. Being in check
does not prevent this draw when the player still has a legal evasion.

Consider White's queen advantage in this position:

```text
position fen 7k/8/8/8/8/8/8/K2Q4 w - - 99 1
go depth 2
info depth 1 score cp 0 nodes 24 pv a1b1
info depth 2 score cp 0 nodes 48 pv a1b1
bestmove a1b1
```

Every legal root move here is a non-capturing king or queen move. It raises
the clock to 100, so the child returns a draw before evaluating the material
advantage. At a starting clock of 98, depth one still scores the queen
advantage as 900; depth two reaches the draw threshold. A draw node returns
an empty continuation, so the root PV can stop early even though the final
position has legal replies and unequal material. `eval` remains a material
inspection command and does not adjudicate draws.

Pawn moves, including non-capturing advances and promotions, reset the clock
before the child is searched. Ordinary captures and en passant also reset
it; castling does not. Thus a root pawn move or capture can keep a line going.
Checkmate takes priority: in the earlier `g6g7` mate fixture with clock 99, the mating move
raises the clock to 100, but its child still returns checkmate rather than
a draw.

The root exception is deliberate. A UCI `go` request asks the engine to
choose a move, so a root with legal moves still gets a legal `bestmove` even
at clock 100 or above. A pawn move or capture there can reset the counter,
and a mating move can still win. This is the reference's search behavior;
claiming a draw or ending the game is not a new UCI action in this lesson.
`bestmove 0000` continues to mean that no legal root move exists.

Large FEN counters need care because `apply_move` rejects integer overflow.
On the root copy only, halfmove values above 100 are capped at 100: they have
the same meaning for this threshold test, while leaving room for a root
move to increment the counter. The fullmove number still starts at one on
the copy because it has no effect on search. The user's current position
keeps both original counters, including `INT_MAX` values. `perft` continues
to ignore draw rules and normalize both counters on its own copy.

Tests cover clocks 98, 99, 100, 101, and `INT_MAX`, both colors, pawn and
capture resets, check evasions, mating moves at the boundary, terminal roots,
and position preservation. The C tests exercise the quiescence and recursion
boundaries directly, including an en passant reset with an artificial high
clock to isolate that behavior.

We use a fixed draw score of zero for clarity. Minifish's main search uses
a small node-dependent adjustment to its draw score; its quiescence uses
zero. Matching that distinction belongs to the later search-parity work.
Lesson 22 adds repetition detection. Other draw conditions remain later work.

## Lesson 22: detecting repetitions by comparing positions

The engine now remembers positions supplied through `position ... moves`
and positions visited on the active search branch. A repetition can end a
branch with a zero score and an empty continuation, just like the fifty-move
draw. Checkmate and stalemate are still handled first, and the root continues
searching for a legal move.

`same_repetition_position` compares the board one square at a time, the side
to move, the four castling rights, and the relevant en passant target. The
clocks are excluded: they can differ when the same position returns. Keeping
castling rights matters because moving a king or rook away and back can
restore the board without restoring the right to castle.

For en passant we follow the reference's key construction. A target is ignored
if no pawn of the side to move attacks it. If a pawn attacks it, the target
matters even when that pawn is pinned and the capture would expose its king.
This is a deliberate match to the snapshot's behavior, not a separate
implementation of all rules for adjudicating repetition in a played game.
The original FEN target remains available to board display and move generation;
only the repetition comparison uses this interpretation.

`PositionHistory` is an array of complete position snapshots, an active count,
and a `can_repeat` flag for each entry. There is no hash table or incremental
key calculation. Recording a game position simply copies the structure.
After a pawn move or capture, older positions are discarded because the
position cannot return to them. Otherwise, only the latest 100 game positions
are retained, shifting the array when it fills. This covers the reference's
99-ply history window; beyond that, the fifty-move rule already handles a
non-root node unless a move resets the counter.

The reference has an important distinction between history and search:

- A game position, including the root, is eligible as an earlier match only
  if it already occurred before. A subsequent visit then completes a third
  occurrence. `prepare_repetition_history` sets the flags explicitly instead
  of clearing position keys as the reference does.
- A position first reached below the root is immediately eligible as an
  earlier match. Returning to it ends a search cycle without waiting for a
  third occurrence. A unique root does not get this immediate eligibility.

The reference expresses this policy through `prepare_root_history` in
`position.py`, root-key handling in `search.py`, and `is_draw` in `movegen.py`.
Our comparisons start four plies back, then step back by two. Four plies is
the shortest return in ordinary legal play, and stepping by two keeps the
same side to move. The current halfmove clock limits the scan so a search
branch never compares against positions before its last pawn move or capture.

For example:

```text
position fen 4r2k/8/8/8/8/8/8/KN6 w - - 0 1 moves b1a3 h8g8 a3b1 g8h8 b1a3 h8g8 a3b1 g8h8
go depth 1
info depth 1 score cp 0 nodes 6 pv b1a3
bestmove b1a3
```

White's knight is worth 200 less than Black's rook under our teaching values.
With no preceding moves, or only one copy of that four-ply cycle, depth one
reports -200 and chooses `a1a2`. After two cycles, `b1a3` reaches a position
that already appeared twice, giving White a draw. Loading only the final FEN,
even with the same clocks, loses that earlier history and again scores -200.
A FEN contains a position and counters; it cannot reconstruct past boards.

Input remains atomic. `read_position_command` builds both the new position
and its history in temporary structures. If any FEN field or listed move is
invalid, neither replaces the current state. A successful new `position`
command starts fresh history from its own initial position.

Each `go` copies the game history and prepares its eligibility flags. Before
searching a child, `negamax` appends the child's position and increments the
count. After the call, it decrements the count before handling failure,
cutoff, or the next sibling. Entries beyond the active count are ignored, so
one branch cannot create a repetition in another. There are 64 extra slots
for search, matching the recursion limit. Iterations reuse the restored game
prefix, and the user's history is never changed by search.

A repetition PV can end with legal moves still available and unequal
material. `eval` still reports material; perft still counts legal move
sequences without draw adjudication. The history policy is for search, and
does not add a UCI draw-claim command. Other reference mechanisms, such as
detecting an upcoming game cycle before making its move, remain later work.

Tests cover game-history eligibility, cycles inside search, position identity,
pinned en passant targets, the last history slots, long move lists, resets,
failed-command rollback, repeated searches, and cleanup after a child fails.
The earlier search and perft regressions continue to pass. Draws retain the
teaching score of zero; exact reference search parity is still a later target.

## Lesson 23: reporting the deepest visited ply

The requested search depth does not describe every branch we visit.
Quiescence can continue past the normal horizon to examine captures,
promotions, and check evasions. We now print `seldepth` alongside `depth`
to show the deepest ply reached anywhere in the current iteration.
One ply is one move by either player; the root is ply zero.

For example:

```text
position startpos
go depth 4
info depth 1 seldepth 1 score cp 0 nodes 21 pv b1a3
info depth 2 seldepth 4 score cp 0 nodes 83 pv b1a3 a7a5
info depth 3 seldepth 5 score cp 0 nodes 612 pv b1a3 a7a5 a1b1
info depth 4 seldepth 8 score cp 0 nodes 2506 pv b1a3 a7a5 a1b1 a5a4
bestmove b1a3
```

The last PV has four moves, but some explored branch reached eight plies.
The three measurements answer different questions:

- `depth` is the normal depth requested for this iteration.
- `seldepth` is the largest distance from the root actually visited.
- The PV contains the moves in the selected continuation, including any
  quiescence moves that contribute to that result.

`SearchStats` groups two ordinary counters: `nodes` and `selective_depth`.
Each call to `negamax` increments the node count and compares its `ply`
with the greatest ply seen so far. Updating at entry also counts terminal
leaves, repetition and fifty-move draws, and leaves at the recursion cap.
A terminal root reports zero because no move was searched. The cap keeps
`seldepth` at or below 64.

The counters have different lifetimes. `nodes` accumulates across all
iterations of one `go` command. `selective_depth` starts at zero for each
iteration. Nothing subtracts from it when recursion returns: it remembers
the deepest branch already visited, even if that branch is later rejected.
Both counters start fresh with a new `go` command.

An iteration's selective depth can even decrease as move ordering and
alpha-beta cutoffs change the branches visited. For this position:

```text
position fen 7k/8/3Q4/6r1/8/6p1/8/K7 w - - 0 1
go depth 3
info depth 1 seldepth 6 score cp 900 nodes 66 pv d6h6 h8g8 h6g5 g8f7 g5g3
info depth 2 seldepth 5 score cp 900 nodes 147 pv d6h6 h8g8 h6g5 g8f7 g5g3
info depth 3 seldepth 7 score cp 900 nodes 1170 pv d6h6 h8g8 h6g5 g8f7 g5g3
bestmove d6h6
```

The drop from six to five makes a useful test: carrying the old maximum
into the next iteration would give the wrong report. Other tests cover
terminal roots, quiescence beyond the PV, rule draws, new searches, and the
recursion limit. The earlier score, node-count, and PV checks still pass;
this lesson only observes the search and prints one extra field. Examples
in earlier lessons show the output format at those stages.

This teaching definition differs from the reference's convention. The
reference updates its stored selective depth at principal-variation nodes
in the main search, excludes quiescence from that update, and adds one when
printing the stored value. Our counter deliberately shows all visited
plies so the effect of quiescence is visible. Matching the reference's
precise reporting convention remains part of the later search-parity work.

## Lesson 24: principal variation search

Our move ordering tries promising moves first: captures and promotions,
with the previous iteration's best move moved to the front at the root.
After searching the first move, many later moves will fail to improve it.
Principal variation search (PVS) uses a narrow alpha-beta window to answer
that question before paying for a more detailed search.

The first move still uses the whole available window. For later moves in
the normal search, we first search with the window `alpha` to `alpha + 1`.
Scores are integers, so no score lies strictly between these endpoints.
This is often called a zero-window search or a scout search. It establishes
a bound on the score instead of finding an exact score inside the window.

Suppose alpha is 100 and beta is 500. The scout uses 100 to 101, asking
whether this move can do better than 100. Its result determines our next step:

| Scout result, from the current player's perspective | Next step |
| --- | --- |
| At most alpha | The move cannot improve alpha; continue with the next move. |
| Greater than alpha, but less than beta | Search this move again with the whole available window. |
| At least beta | The bound already proves a cutoff; skip the remaining moves. |

A successful scout only gives a lower bound on the move's score. For
example, a result of 200 does not prove that 200 is its exact value. If
that result is still below beta, the full re-search supplies the score and
continuation we need, or establishes a cutoff at beta. The re-search uses
the current `alpha` and `beta`, before this move changes either value.

The child sees the opposite player's scores. Negating and reversing the
scout window gives `-alpha - 1` to `-alpha`. The full re-search uses
`-beta` to `-alpha`, just as our ordinary alpha-beta search did. Both calls
search the same child at the same depth; PVS does not shorten the search.

In `negamax`, one `if` selects the scout for later normal-search moves.
Another `if` decides whether its result needs a full re-search. Quiescence
keeps its existing alpha-beta loop. An incoming window already one point
wide also keeps that loop, since it already asks the narrow question.
At positive depth every generated legal move is searched unless a cutoff
occurs, so `i > 0` identifies a later move without another counter.

We append the child to repetition history once, before either call, and
remove it once after both calls. A failed scout skips the re-search. A
failed re-search follows the same cleanup path. Neither failure publishes
an incomplete iteration. Both searches contribute to the node count and
the iteration's maximum visited ply. A full re-search replaces the scout's
temporary PV before the parent copies it.

For the defended-rook example from earlier lessons, the current output is:

```text
position fen 7k/8/4p3/3r4/8/8/8/K2Q4 w - - 0 1
go depth 3
info depth 1 seldepth 4 score cp 300 nodes 38 pv d1b1
info depth 2 seldepth 4 score cp 300 nodes 189 pv d1b1 d5d2
info depth 3 seldepth 6 score cp 400 nodes 1099 pv d1e1 d5d3 e1e6
bestmove d1e1
```

The score and PV agree with lesson 23. The node counts differ: that lesson
used 37, 208, and 1005 cumulative nodes. At depth one, the first capture
`Qxd5` loses to `exd5`; a later `Qb1` improves the score and needs a
re-search. Good ordering can save work, but re-searches can also cost more.
For the castling test position, depth three instead drops from 20762 to
5809 cumulative nodes. PVS is useful without guaranteeing a reduction for
every position. Selective depth can change too, because cutoffs change
which branches are visited.

Tests retain exact score, PV, and node expectations for the current search.
Focused C checks exercise an improving scout, a scout that reaches beta,
an already narrow window, and failure during either pass. The UCI checks
still cover repeated searches, ties, draws, mate distance, and legal PVs.
Forty comparisons against the lesson 23 executable preserved every reported
score, PV, and final best move while allowing nodes and selective depth to
change. Earlier lesson transcripts describe their historical search behavior.

The reference also uses narrow searches followed by full PV searches, but
combines them with reductions, extensions, cached results, and other rules
we have not introduced. This lesson teaches the PVS decision itself;
exact reference node counts and reporting conventions remain later work.

## Lesson 25: aspiration windows between iterations

Iterative deepening gives us a score from the previous completed depth.
Often the next depth will find a similar score. An aspiration window uses
that estimate to start the root search with a smaller alpha-beta window,
allowing some branches to cut off sooner.

The first iteration still searches from `-INFINITE_SCORE` to
`INFINITE_SCORE`. Later iterations start 50 centipawns below and above the
previous score. Fifty centipawns is half a pawn under our teaching values.
For a previous score of 300, the starting window is 250 to 350. We clamp
the endpoints to the infinite limits, which matters near mate scores.

The new `search_iteration` function owns this small retry loop:

| Result from the attempt | Meaning | Next step |
| --- | --- | --- |
| At most alpha | Fail low: an upper bound | Set alpha to negative infinity and retry. |
| At least beta | Fail high: a lower bound | Set beta to positive infinity and retry. |
| Strictly between alpha and beta | Exact score | Complete the iteration. |

Equality with an endpoint also requires a retry. A bound tells us which
side of the window contains the score; it does not establish an exact
score just because its number looks plausible. Only a result strictly
inside the window is copied to the completed result.

For this lesson, widening reopens the failed side all the way to infinity.
The other endpoint stays where it was. Each failed attempt therefore opens
a previously finite side. All our material and mate scores lie strictly
between the two infinite limits, so there can be at most two retries.
Every attempt searches the same requested depth. We do not reduce depth
to compensate for the extra work.

Each attempt uses the root move from the previous completed iteration.
A provisional bound and its PV do not replace that hint. The recursive
search restores repetition history before returning, so each retry begins
with the same game history. If any attempt fails, `search_iteration`
returns failure without changing its output. The existing `go` loop can
then retain the last completed iteration and its best move.

Nodes accumulate across all attempts and all completed depths in one
`go` command. Selective depth resets once at the start of an iteration,
then keeps the maximum over that iteration's attempts. The command prints
one `info` line per completed depth; it does not report provisional bounds
as exact scores. A new `go` starts with a full window and fresh counters.

Here is a fail-low example:

```text
position fen 7k/8/8/8/8/8/8/K2Q4 w - - 98 1
go depth 3
info depth 1 seldepth 2 score cp 900 nodes 28 pv a1b1
info depth 2 seldepth 2 score cp 0 nodes 124 pv a1b1 h8g7
info depth 3 seldepth 2 score cp 0 nodes 173 pv a1b1 h8g7
bestmove a1b1
```

At depth one, White retains a queen advantage. At depth two, every line
reaches the fifty-move threshold, giving a draw. The first depth-two
attempt uses 850 to 950 and fails low. Reopening alpha lets the retry
establish zero as an exact result. Its report includes both attempts'
nodes, and the stored position still has a halfmove clock of 98.

The defended-rook position from lesson 24 illustrates fail high. Its
score rises from 300 at depth two to 400 at depth three. The initial
250-to-350 window fails high, so beta is reopened and the search repeats.
The final PV remains `d1e1 d5d3 e1e6`, but the cumulative depth-three
node count rises from 1099 to 1415. Aspiration windows can also save work:
the castling fixture drops from 5809 to 5695 nodes at depth three. A good
estimate helps; a poor estimate costs retries.

Tests cover both failure directions, scores exactly on the window edges,
mate scores near infinity, retained root preferences, cumulative nodes,
and cleanup when either the first attempt or a retry fails. UCI checks
verify the completed reports, unchanged game state, and fresh searches.
Forty comparisons with lesson 24 preserve scores, PVs, and final best moves;
node counts and selective depth are allowed to change.

The reference uses a score-dependent margin, a running score estimate,
gradual widening, and adjustments to search depth after failures. This
lesson introduces the retry decision with a fixed margin and unchanged
depth. Those reference rules, and exact reference search parity, remain
later work. Earlier lesson transcripts retain their historical counts.

## Lesson 26: pruning with mate-distance bounds

Mate scores already include their distance from the root. Being checkmated
at ply `p` scores `-MATE_SCORE + p`; delivering mate at ply `p` scores
`MATE_SCORE - p`. A quicker win scores higher, and a later loss scores
higher than an earlier loss. These conventions also give us limits on
what a branch can possibly achieve.

At a node `ply` moves from the root:

| Limit | Score | Reason |
| --- | --- | --- |
| Lowest possible score | `-MATE_SCORE + ply` | Being checkmated here is the earliest possible loss. |
| Highest possible score | `MATE_SCORE - ply - 1` | The earliest possible win is mating on our next move. |

For example, at ply five these bounds are -99995 and 99994. Ordinary
material scores are well inside that range. The lower bound is conservative:
we retain it even after checking that this node is not already checkmate.

In `negamax`, two local integers name these limits. We raise alpha to the
lower limit when necessary and lower beta to the upper limit. If alpha is
then at least beta, the window is closed: no further search can find an
exact score inside it. We return alpha as a bound with an empty PV.
This does not claim that the current position has a newly discovered mate.

Consider a root that has already found mate in one, scoring 99999. A later
move's scout asks its child to search from -100000 to -99999. That child
is at ply one, where the lowest possible score is -99999. Raising alpha
to that limit closes the window immediately. Negating the returned bound
gives 99999 at the root, which cannot beat the existing move. Our strict
score comparison keeps the earlier move and its complete mating line.

The new block runs only below the root and only at positive normal-search
depth. The root still chooses a move, and quiescence retains its existing
behavior. This follows the reference's scope for mate-distance pruning.
Terminal positions, rule draws, and the recursion cap are handled first,
so an actual checkmate still takes precedence over a rule draw.

We keep legal move generation in its existing place for readability.
A pruned visit therefore still generates moves and counts as one node,
but it searches no children. Its ply still contributes to selective depth.
The caller removes the child from repetition history through the usual
cleanup path. PVS and aspiration retries need no special cases: they
already distinguish bounds from completed exact results.

Here is a position with mate in one:

```text
position fen 7k/8/5KQ1/8/8/8/8/8 w - - 0 1
go depth 4
info depth 1 seldepth 2 score mate 1 nodes 31 pv g6g7
info depth 2 seldepth 1 score mate 1 nodes 55 pv g6g7
info depth 3 seldepth 1 score mate 1 nodes 79 pv g6g7
info depth 4 seldepth 1 score mate 1 nodes 103 pv g6g7
bestmove g6g7
```

Depth one still enters quiescence for every child, so its report is
unchanged. At later depths, `g6g7` is searched first because it was the
previous iteration's choice. Once that mate is confirmed, the other
children close their windows without descending further. Selective depth
falls to one. Lesson 25 used 1348 cumulative nodes at depth four; this
lesson uses 103, with the same score, PV, and best move.

The tests also retain longer mating lines. With White's king on f6, queen
on e5, and Black's king on h8, White to move finds mate in two at depth
three. With Black to move, depth four reports `mate -2`. Their
published PVs are checked by playing every move through to checkmate.

Focused C checks exercise both limits at several plies, including the
last ply before the recursion cap. At a limit the search cuts off; moving
the window one point inward still searches. Further checks cover empty
PVs on cutoffs, root and quiescence exclusions, terminal results, draws,
and the hard cap. All 40 comparison cases preserve lesson 25's scores,
PVs, and final moves while allowing nodes and selective depth to change.
Earlier transcripts retain their historical counts. Full reference search
parity still requires the remaining search and evaluation mechanisms.

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
Also build and run the focused search checks with the same compiler:

```powershell
& C:\msys64\ucrt64\bin\gcc.exe -std=c11 -Wall -Wextra -Wpedantic -O0 -g tests\test_search.c -o build\test-search.exe
.\build\test-search.exe
```

On other systems, use:

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -O0 -g tests/test_search.c -o build/test-search
./build/test-search
```

The C test file includes `main.c`, temporarily renaming its `main` function
so the tests can supply their own entry point and call the internal search.
It uses standard C assertions; compile it without `NDEBUG` so checks run.

The Python tests talk to a running process through pipes, checking that replies arrive
before input closes. They also check whitespace, unknown commands, oversized
lines, `quit`, and end of input. The board test checks every displayed rank,
piece, and coordinate label, then repeats the display to check that inspection
leaves the position unchanged. FEN tests cover every position field, resetting
the position, en passant without a capturer, counter limits, and rejection of
malformed input without changing the current state.
Attack tests check every target square for isolated pieces of both colors,
central and edge positions, blockers in all eight sliding directions, knight
jumps, pinned pieces, and command validation. Some fixtures intentionally omit
kings to isolate a single piece's geometry; they are not complete legal games.
Check tests cover both colors, each attacking piece, blockers, pinned attackers,
adjacent kings, missing or duplicate kings, and inspection without changing
the position.
Move-generation tests cover both sides, ordinary piece patterns, captures,
pins, check evasions, king safety after captures, and inspection without
changing any position fields. They also check missing kings. Pawn tests cover
both colors, blocked advances, starting
ranks, captures, board edges, pins, and check evasions. Promotion tests cover
every choice for both colors, forward and
capture promotions, blockers, king safety, move suffixes, and preservation of
the original position. The starting-position test continues to expect 20 moves.
En passant tests cover both colors, both capture directions, edge files,
inconsistent FEN targets, discovered rook and bishop attacks, check evasions,
and preservation of the original position.
Castling tests cover both sides and colors, individual rights, home-piece
placement, every intervening square, attacks along the king's path, attacked
rooks, and preservation of the position.
Move-application tests cover opening sequences, both colors of every special
move, captures, castling-right loss, en passant expiry, clock updates, counter
limits, and rollback when a move in a sequence fails.
Perft tests cover the reference fixtures, depths 0 through 4 at the starting
position, terminal positions, counter independence, input validation, and
preservation of board state and en passant rights. Individual perft replies
have a longer test timeout than ordinary command replies.
Material tests cover balance, both score perspectives, captures, all promotion
choices, en passant, unchanged material after castling, and inspection without
changing the position. They also check that piece placement and unrelated
metadata do not affect this deliberately limited evaluator.
Search tests cover deterministic ties and node counts, both colors, captures,
a recapture visible only at depth two, promotions, en passant, mate at the
horizon, mate distance, terminal roots, validation, and position preservation.
