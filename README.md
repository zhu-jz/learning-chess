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

## Lesson 27: remembering quiet moves that caused cutoffs

Captures are easy to recognize as promising moves, but a strong move need
not capture anything. A quiet move that reaches beta has already shown
that the caller can reject this branch. At another position the same
distance from the root, that move may provide a useful reply again.
The killer-move heuristic remembers such moves and tries them earlier.

`KillerMoves` contains a plain two-dimensional array:
`moves[MAX_SEARCH_PLY][2]`. Each ply has two slots. Slot zero is the newest
quiet cutoff, and slot one is the preceding distinct move. `clear_killers`
fills both slots with a move whose `from` square is -1, marking an empty
hint. The table is local to one `go` command and is passed explicitly
through the search calls.

When a normal-search move causes a beta cutoff, `is_quiet_move` checks
that its destination was empty, it was not en passant, and it was not a
promotion. Quiet pawn moves and castling qualify. Captures and promotions
do not. A quiet check evasion can qualify in normal search, but quiescence
neither uses nor updates this table. Improving alpha without reaching beta
does not record a killer.

`remember_killer` needs only a comparison and two assignments. If the move
is already newest, the slots stay unchanged. Otherwise, the old newest
move becomes second and the new move becomes first. Reusing the older
move swaps their order; a third distinct move replaces the oldest one.
Moves at one ply do not replace the slots at another ply.

Normal search now orders moves in these stages:

| Priority | Moves |
| --- | --- |
| First, at the root | The previous completed iteration's preferred move, if present |
| Next | Captures and promotions, in the existing material-gain order |
| Next | The newest legal quiet killer, then the older one |
| Last | Other quiet moves, preserving their relative generation order |

The code first performs the existing material sort. Every capture and
promotion has a positive priority, while quiet moves have zero, so the
quiet moves form the final part of that list. `order_killers` finds its
first index, then passes `&moves[first_quiet]` and the remaining count to
`search_move_first`. This is the same array, starting at a later element;
no second move list is needed. Moving the older hint first, then the newer
one, places the newer one ahead. The root preference is applied last so
it remains first overall.

A saved hint is never played directly. `same_move` compares its starting
square, destination, and promotion choice against generated legal moves.
The current legal entry supplies castling and en passant details. A hint
that is illegal here is absent from the list and does nothing. A hint that
has become a capture is outside the quiet part and keeps its ordinary
capture ordering. No move is added or duplicated.

The table persists across siblings, iterative-deepening iterations, PVS
re-searches, and aspiration retries in the same `go`. These hints describe
what worked during this search, so they are not undone with the board or
repetition history. A new `go` clears them, even when there is no new
`position` command. The recursion-cap return occurs before table access,
so the last possible row is ply 63; there is no row for ply 64.

For the starting position, the current output is:

```text
position startpos
go depth 4
info depth 1 seldepth 1 score cp 0 nodes 21 pv b1a3
info depth 2 seldepth 4 score cp 0 nodes 83 pv b1a3 a7a5
info depth 3 seldepth 5 score cp 0 nodes 612 pv b1a3 a7a5 a1b1
info depth 4 seldepth 6 score cp 0 nodes 2166 pv b1a3 a7a5 a1b1 a5a4
bestmove b1a3
```

Lesson 26 used 2494 cumulative nodes at depth four. Here the hints save
work, but they are guesses: in the defended-rook fixture, depth-three work
instead rises from 1415 to 1716 nodes. Its score remains 400, while the PV
changes from `d1e1 d5d3 e1e6` to `d1e1 h8g7 e1e6`. Both Black replies have
the same searched score. Changing the order can change which tied move
is encountered first, so PVs need not match the preceding lesson.

Tests cover distinct slots, replacement order, per-ply independence, stale
hints, stable move lists, castling details, non-quiet exclusions, and the
root preference. They also check quiescence, failed child searches, the
recursion cap, and repeated `go` commands. Earlier focused C tests pass
`NULL` for the table to isolate their original mechanism; every UCI search
uses a real table. The 40 comparison cases preserve all iteration scores
and final best moves. Two PVs change tied replies, and the UCI checks play
those lines to verify their legality and scores.

The reference also keeps two killer slots, but its wider history-update
logic can record a quiet best move after an alpha improvement, and it
clears some deeper slots as search enters new branches. Its staged move
picker also treats captures and evasions differently. This lesson uses
only quiet beta cutoffs and keeps the table for the whole `go`; matching
those additional reference policies remains later work. Earlier lesson
examples retain their historical ordering and counts.

## Lesson 28: ranking quiet moves with a history table

Killer moves remember two recent cutoffs at a particular ply. The history
heuristic keeps a broader record: a quiet move that helped at one ply can
gain priority at another. We now keep an integer for each color, starting
square, and destination in `quiet_history[2][64][64]`.

For example, White's `e2e4` uses `quiet_history[WHITE][12][28]`. The entry
does not depend on the current ply or the piece's type. White and Black
have separate entries, even for identical square coordinates. This table
is separate from `PositionHistory`, which records boards for repetition
detection. The history heuristic records move-ordering hints.

When a quiet normal-search move causes a beta cutoff, it earns a bonus
of `depth * depth`, using the remaining depth at that node. A cutoff at
depth one adds 1; one at depth three adds 9. Deeper cutoffs receive more
weight because they may save more work. These numbers only affect move
ordering. They are not centipawns and never enter the evaluation or the
reported search score.

`reward_quiet_move` adds the bonus and caps the entry at `HISTORY_LIMIT`,
which is 10000. A capped entry stays at the limit on later rewards instead
of overflowing. With the current maximum depth of four, the largest bonus
is 16, so the addition itself fits easily in an `int`. This first version
only adds credit; it does not penalize unsuccessful moves or reduce old
credit. Entries at the limit can tie, and a new `go` clears the table.

The old `KillerMoves` structure is now named `MoveHints`. It contains both
the killer array and the quiet-history array. `clear_move_hints` clears
both through ordinary loops, and the search passes one pointer to that
structure. Hints remain available across siblings, iterations, PVS
re-searches, and aspiration retries within one `go` command.

Move ordering now happens in four explicit steps:

1. Sort captures and promotions by the existing material-gain rule.
2. Sort the final quiet part of the list by descending history score.
3. Move the two killers to the front of that quiet part, newest first.
4. Move the previous completed root choice to the very front, if present.

`order_quiet_history` uses insertion sort, just like our earlier material
sort. It moves an entry only past strictly lower scores. Equal history
scores therefore preserve generation order before the killer and root
hints are applied. The sort stays within the quiet part, so even a history
score of 10000 cannot move a quiet move ahead of a capture or promotion.
The legal list still supplies every move; table entries cannot introduce
an illegal move or change castling details.

The update sits beside the killer update inside the beta-cutoff branch.
Captures, en passant, and promotions earn no quiet-history credit. Quiet
pawn moves and castling qualify, with castling indexed by the king's start
and destination. An alpha improvement without a cutoff earns no credit.
Quiescence neither reads nor updates the table, including when a quiet
check evasion causes a cutoff. A failed child search supplies no score
and cannot produce a reward for that move.

For the defended-rook example:

```text
position fen 7k/8/4p3/3r4/8/8/8/K2Q4 w - - 0 1
go depth 3
info depth 1 seldepth 4 score cp 300 nodes 38 pv d1b1
info depth 2 seldepth 4 score cp 300 nodes 145 pv d1b1 d5d2
info depth 3 seldepth 8 score cp 400 nodes 1687 pv d1e1 h8g7 e1e6
bestmove d1e1
```

The previous lesson used 1716 cumulative nodes at depth three. The history
table saves some work here, while many shallow fixtures remain unchanged:
captures, killers, and the root preference already determine their useful
ordering. History is a guess about which moves to try first, so it does
not guarantee a node reduction for every position. Different ordering can
also change tied choices, as the killer-move lesson demonstrated.

Focused tests reward a move at one ply and reuse it at another with no
killer present. They check color separation, weighted rewards, saturation,
complete resets, stable ties, legal-list preservation, and the priority of
captures, killers, and root hints. Existing cutoff tests now also check
history exclusions and quiescence behavior. The UCI suite exercises the
real tables and repeated searches. All 40 comparisons with lesson 27 keep
the same iteration scores, PVs, and final best moves. The UCI tests retain
exact node-count expectations for their regression fixtures.

The reference also indexes its main history by color and move squares,
but uses signed, damped updates, different bonuses, penalties, and other
history tables in its move picker. Our capped positive rewards introduce
the basic idea with simple arithmetic. Those reference update rules and
full fixed-depth parity remain later work. Earlier lesson transcripts
retain their historical results.

## Lesson 29: penalizing quiet alternatives before a cutoff

The previous lesson only rewarded quiet moves that caused beta cutoffs.
We now also lower the history scores of earlier quiet moves tried at the
same node. Those moves failed to reach beta before the successful move
was found. That is useful ordering evidence, even though those moves may
still be good in other positions.

The update rule is deliberately small:

| Move at this node | History change after a quiet cutoff |
| --- | --- |
| The quiet move that caused the cutoff | Add `depth * depth` |
| An earlier quiet move that was searched | Subtract `depth * depth` |
| An earlier capture or promotion | No quiet-history change |
| Any later, unsearched move | No change |

The reward helper is renamed `update_quiet_history` and now accepts a
signed change. It clamps the result between `-HISTORY_LIMIT` and
`HISTORY_LIMIT`, or -10000 and 10000. A reward can raise a negative entry,
and a penalty can lower an entry already at the positive limit. The
largest change remains 16 at our maximum normal depth of four, so adding
or subtracting it fits easily in an `int` before clamping.

`order_quiet_history` already compares integer scores, so it needs no
changes. Positive entries sort before zero entries, which sort before
negative entries. Equal negative scores retain their generation order.
The capture and promotion prefix, killer priorities, and preferred root
move still take precedence over history ordering.

The new loop sits beside the existing reward inside the quiet-cutoff
branch. If move `i` causes that cutoff, we examine indices zero through
`i - 1` in the node's ordered move array. At positive depth, every one of
those moves was searched: quiescence is the only place our loop skips
quiet moves. A failure would already have returned, and an earlier cutoff
would already have stopped the loop. We can therefore reuse this array
instead of allocating another list of searched quiet moves.

The move array stays in the order chosen at node entry, even while deeper
searches update the shared hints. Each earlier quiet move receives one
penalty in this call. PVS may have searched it twice, but it still occupies
one array entry. An aspiration retry is a new search call and can supply
new feedback of its own. Only the current player's history entries are
changed by this node's update.

For a small example, consider the defended-rook position with a root
window ending at beta 200:

```text
7k/8/4p3/3r4/8/8/8/K2Q4 w - - 0 1
```

At depth one, the ordered search tries `Qxd5` first and sees `exd5`.
Three quiet king moves also fail to reach beta. Then `Qb1` scores 300 and
causes the cutoff. `Qb1` earns one point; `a1b1`, `a1a2`, and `a1b2`
each lose one. The earlier capture keeps its history score, and later
queen moves receive no update because they were never searched. At depth
two, the same root alternatives lose four points each instead.

With ordinary UCI iterative deepening, the current output is:

```text
position fen 7k/8/4p3/3r4/8/8/8/K2Q4 w - - 0 1
go depth 3
info depth 1 seldepth 4 score cp 300 nodes 38 pv d1b1
info depth 2 seldepth 4 score cp 300 nodes 137 pv d1b1 d5d2
info depth 3 seldepth 8 score cp 400 nodes 1656 pv d1e1 h8g7 e1e6
bestmove d1e1
```

Lesson 28 used 1687 cumulative nodes at depth three. The new ordering
saves work here, while the losing mate-in-two fixture rises from 547 to
551 nodes. A history penalty is a guess about useful ordering, and does
not guarantee fewer nodes.

The policy only runs after a successful quiet beta cutoff in normal
search. A capture or promotion cutoff does not trigger quiet penalties
in this lesson. A node that finishes without a cutoff, a failed child or
PVS re-search, and quiescence do not apply this update. Completed feedback
from deeper nodes remains in the shared table; we do not roll it back.
Each new `go` still clears both positive and negative scores.

Tests check signed saturation and recovery, ordering around zero, stable
negative ties, depth-weighted penalties, and the exact entries changed by
the example. They also check unsearched moves, earlier captures, no-cutoff
searches, capture cutoffs after a quiet alternative, and failures during
a scout or re-search. All 40 comparisons preserve lesson 28's scores,
PVs, and final best moves. The UCI fixtures keep explicit current node
counts and test repeated searches.

The reference also penalizes quiet alternatives, but uses different
bonus and penalty formulas, damped signed updates, and broader conditions
for updating its history tables. This lesson adds the negative feedback
with ordinary subtraction and a bounded score. Those remaining policies
and exact reference parity are still later work. Earlier lesson examples
retain their historical ordering and counts.

## Lesson 30: damping quiet-move history

Suppose a quiet move has earned 5,000 history points. Another cutoff at
depth four gives it a reward of 16. How much weight should that reward
have? We now let each new observation retire a fraction of the move's
old score. Repeated successes then add progressively less credit, while
a penalty can bring a once-promising move down the order sooner.

The calculation in `update_quiet_history` uses the existing limit of
10,000. For a change of either +16 or -16, it forgets 16/10,000 of the
old score before applying the signed change.

| Old score | Change | Signed amount forgotten | New score |
| ---: | ---: | ---: | ---: |
| 5,000 | +16 | 8 | 5,008 |
| 5,000 | -16 | 8 | 4,976 |
| -5,000 | +16 | -8 | -4,976 |
| -5,000 | -16 | -8 | -5,008 |

The sign of the amount forgotten comes from the old score. Subtracting
that amount moves the old score toward zero. The sign of the change
comes from the search outcome and determines the direction of the new
evidence. Only the move receiving the reward or penalty gets updated.

We multiply before dividing so that integer arithmetic keeps the useful
part of the fraction. The product can reach 160,000, so the multiplication
uses a C `long`. Division truncates toward zero. For example, with a
change of magnitude 16, a score of 624 loses zero points to decay and a
score of 625 loses one. The two bounds from lesson 29 still apply.

The reference uses this same principle with a scaled table and tuned
update sizes. Here the scale and depth-squared updates carry over from
lesson 29 so the effect of damping can be studied on its own. All 40
fixed-depth comparison runs produced identical UCI transcripts in these
two lessons, including node counts. The focused C checks exercise larger
positive and negative scores where the effect is visible.

## Lesson 31: remembering a reply to the opponent's move

Suppose White plays `e2e4` and Black finds a quiet reply that reaches beta.
Later in the search, a white pawn again arrives on e4. Black can try the
remembered reply early, even at a different ply. This is a *countermove*.

`MoveHints` now stores one reply per colored piece and destination square.
After `e2e4`, the entry is `countermove[WHITE_PAWN][28]`. Each recursive
call passes the destination of the move it just played, including on a PVS
re-search. The child reads the piece on that square from its own board.
For a promotion, this selects the promoted piece. At the root, a value of
-1 marks the start of the search path.

The move list already contains the legal moves for the current position.
Within its quiet part, the search tries the two killers, the remembered
reply, and then the remaining moves in history order. Moving an existing
entry to the front preserves a single copy when a killer and a countermove
name the same reply. A reply that has become illegal or is now a capture
has no match in the quiet part. The previous iteration's root choice
continues to take first priority at the root.

A quiet beta cutoff in normal search replaces the entry for the preceding
move. One table serves all iterations and aspiration retries of a single
`go`. The next `go` starts with an empty table. This teaching policy uses
the reference's piece-and-square key. The reference also updates replies
through its broader quiet-history rules.

Across 40 fixed-depth comparisons with lesson 30, every iteration agreed
on the score and PV, and each search chose the same final move. Ordering
can change how much work it takes to reach that result. The defended-rook
fixture went from 1,656 to 1,659 nodes at depth three. In the losing
mate-in-two test, depth four went from 551 to 522 nodes.

## Lesson 32: choosing which piece takes the rook

Put a white queen on d1, a white pawn on c4, and an undefended black rook
on d5. Both `Qxd5` and `cxd5` gain 500 centipawns. Which capture should
we try first? We choose the pawn. If there were a defender on d5, losing
a pawn on the next move would cost less than losing a queen.

`order_moves` still tries the larger immediate gain first, including any
gain from promotion. When gains tie, it tries the cheaper attacker first.
The king comes last among attackers. Knights and bishops have the same
value here, so their original order breaks a tie. Quiet moves also keep
their order for the later quiet-move rules.

Try `position fen 7k/8/8/3r4/2P5/8/8/K2Q4 w - - 0 1` followed by
`go depth 1`. Lesson 31 chose `d1d5`; this lesson chooses `c4d5`. Both
searches scored the position at +1000 and visited 28 nodes. The first
capture searched wins the tie.

Static exchange evaluation (SEE) asks a further question: after the
capture, what material can each side recover by recapturing on that
square? Our ordering rule never looks for a defender. The reference's
`movepick.py` first ranks captures using the victim and capture history,
then uses `see_test` to decide which captures to try early and which to
save for later.

Across the 40 existing fixed-depth comparisons with lesson 31, all scores,
principal variations, and best moves stayed the same. In the castling
fixture at depth three, the node count fell from 5,695 to 5,373.

## Lesson 33: when should you stop recapturing?

The teaching command `see <capture>` estimates a legal capture's material
gain in centipawns, from the moving side's point of view. Try these three
positions. In each, White takes the rook on d5.

| Position after `position fen` | Command | Score | Why |
| --- | --- | ---: | --- |
| `7k/8/8/3r4/8/8/8/K2Q4 w - - 0 1` | `see d1d5` | +500 | No black piece can recapture. |
| `7k/8/4p3/3r4/8/8/8/K2Q4 w - - 0 1` | `see d1d5` | −400 | After Qxd5 exd5, White has gained a rook (500) and lost a queen (900). |
| `3q3k/8/8/3r4/2P5/8/8/K2R4 w - - 0 1` | `see c4d5` | +500 | Black can play Qxd5 for a pawn, but White would answer Rxd5. Black declines. |

`static_exchange` plays the given capture on a copy of the board. It
looks for the cheapest legal recapture on the same square and asks the
same question from the opponent's side. If that reply gains material,
subtract the gain. If it loses material, the opponent can stop, so
subtract nothing. Only the first capture is compulsory. Each capture
removes a piece, so the calculation ends without a depth limit.

Scanning the new board after each capture reveals sliding pieces that
were blocked before. Checking king safety excludes pinned recaptures
that expose the king. Equal-value attackers keep board scan order. The
first capture uses the player's chosen promotion; a hypothetical pawn
recapturing on its last rank becomes a queen.

This is a material estimate for one square. It considers one attacker
per turn and ignores threats elsewhere, including what happens if a
player leaves the exchange to answer a check. The reference's
`see_test` answers a threshold question and treats special moves
separately. Our command returns a number so we can check it by hand.
Search does not call it yet.

## Lesson 34: finding the reference's SEE boundary

After Qxd5 exd5 in lesson 33, White has traded a queen for a rook. Our
`see d1d5` prints −400: 500 for the rook minus 900 for the queen. The
reference's `see_test` takes a threshold and answers yes or no. Does it
accept −400 and reject −399? It rejects both. Its rook is worth 1,276 and
its queen 2,538. It accepts **−1,262** and rejects **−1,261**. The two
engines agree on the trade and price it differently.

| Exchange | Teaching score | Highest passing reference threshold |
| --- | ---: | ---: |
| Queen takes undefended rook | 500 | 1,276 |
| Queen takes rook, pawn recaptures | −400 | −1,262 |
| Pawn takes rook, opposing queen declines to recapture | 500 | 1,276 |
| En passant wins a pawn outright | 100 | 0 |
| Pawn takes rook and promotes to knight | 700 | 0 |
| Rook takes knight, opposing pawn recaptures and promotes | −1,000 | −495 |

The last three rows show where changing piece values alone would fail.
For an initial en passant capture or promotion, the reference checks
whether zero reaches the threshold. For the last row, the first move is
an ordinary capture. The reference counts the knight taken and the rook
lost (781 − 1,276), with no promotion bonus for the recapturing pawn.
The teaching calculation includes that bonus.

With the November 5 reference folder in the project root and the C engine
built, run this optional comparison:

```powershell
.\.venv\Scripts\python.exe -B tests\compare_see.py build\learning-chess.exe
```

The script sends each of ten positions to the C engine, then calls the
reference function on the same move. It checks the teaching score and the
next integer as thresholds, then checks both sides of the expected
reference boundary. These are SEE checks only; search still follows
lesson 32's capture order.

## Lesson 35: does the exchange reach a given value?

The new diagnostic `see_test <capture> <threshold>` answers yes or no
using the reference's exchange prices. Try the defended rook from lesson 34:

```text
position fen 7k/8/4p3/3r4/8/8/8/K2Q4 w - - 0 1
see_test d1d5 -1262
info string see_test yes
see_test d1d5 -1261
info string see_test no
```

White takes a rook worth 1,276. Even losing the queen leaves White with
1,276 − 2,538 = −1,262, so the first question has an immediate answer.
For the second, Black needs to recover **2,538** to make White fall short:
recovering only 2,537 would leave White exactly at −1,261. The pawn on
e6 can take the queen, and White has no recapture. This is why the
calculation asks for one more than the margin when it switches sides.

`exchange_reaches` keeps the square being fought over empty on a copy
of the board. The price of the piece on offer is kept in a variable.
Removing each attacker from its old square reveals any sliding piece
behind it. The cheapest available attacker goes first, and a king can
recapture only when the square is undefended. For pins we follow the
reference's rule: while any original enemy pinner remains, skip this
side's originally pinned attackers. Initial en passant captures and
capture-promotions use a boundary of zero. A pawn recapturing on its
last rank keeps its pawn price in this calculation.

Run lesson 34's comparison script to check both C commands. It now also
includes a king that can take an undefended queen and a king kept away by
a pawn. The ordinary UCI tests check the expected boundaries without
requiring the Python reference. Search still uses lesson 32's ordering.

## Lesson 36: try a quiet move before giving up a queen

In the defended-rook position from lesson 35, White can play Qxd5 exd5.
The rook is worth 1,276 and the queen 2,538, so SEE puts this exchange
at −1,262. Should White try it before a quiet move like Qb1?

Normal search now gives each capture a loss allowance: multiply the
victim's reference price by seven and divide by sixteen, rounding down.
For the rook the allowance is 558. Qxd5 fails the −558 threshold, so
White tries Qb1 earlier and keeps Qxd5 for later. A small loss can still
pass: trading a bishop (825) for a knight (781) costs only 44, within
the knight's allowance of 341. This formula uses the victim part of the
reference's capture score; we do not yet keep its capture-history term.

After ordering the captures and quiet moves, `defer_losing_captures`
walks the list once. Each passing capture, non-capturing promotion, or
quiet move goes immediately before the captures deferred so far. The
relative order within each group stays intact. A deferred capture is
still searched if the earlier moves do not produce a cutoff. The
previous iteration's preferred root move still has first choice.
Quiescence and nodes where the king is in check keep their earlier order.

Across 40 fixed-depth comparisons with lesson 35, every iteration kept
its score and principal variation, and all final best moves agreed.
Three searches changed their node counts, all at depth three:

| Position | Lesson 35 | Lesson 36 |
| --- | ---: | ---: |
| Defended rook | 1,659 | 1,639 |
| Castling fixture | 5,373 | 5,080 |
| Opening after Bb5 and ...a6 | 1,234 | 1,366 |

SEE judges a sequence of captures on one square. It cannot tell us
which move will cause the earliest cutoff in the full search.

## Lesson 37: remembering which captures worked

Suppose a pawn and a queen can both take a rook on d5. With no history,
we try the pawn first. Now suppose Qxd5 was searched first at depth two
and reached beta. The search adds 290 to the entry for **white queen,
d5, rook**. A queen coming from a different square can reuse that
experience. A pawn taking the same rook uses a different entry.

In an ordinary search node outside check, we rank captures and promotions
by `(7 * victim_price + history) / 16`. For this rook, the queen's score
rises from 558 to 576 while the pawn's stays at 558. Next time, the
queen goes first. Equal scores still use the earlier material and attacker
tie-breaks. The same score sets the loss allowance from lesson 36. The
board square before the move supplies the victim type: en passant and a
promotion without a capture both use the empty-square entry.

At the end of a normal search node, if the best move raised alpha, we
update the history for that move when it was a capture or promotion.
A beta cutoff qualifies, as does an improvement confirmed by searching
the whole list. Among the first 32 moves tried, captures and promotions
that failed to raise alpha receive a penalty, even if the winner was a
quiet move. A scout and its re-search together supply one result.

The update sizes come from the reference. At depth one the adjustment
for a best capture is **−27**; at depth two it is **+290**. An unsuccessful
depth-one capture gets **−134**. Each update also forgets a fraction of the
old entry, so repeating a result has less effect as the entry grows.
All iterations of one `go` share this table. A new `go` clears it.
Quiescence neither reads nor updates capture history in this lesson.

All 40 fixed-depth comparisons with lesson 36 kept the same scores,
principal variations, and final best moves. The castling fixture still
chose Bxa6 at depth three, but visited 6,200 nodes instead of 5,080.
These counts measure the ordering change in our engine; matching one
reference heuristic does not guarantee a smaller search tree.

## Lesson 38: remember captures beyond the horizon

White's queen on d4 can take either of two black rooks. The rook on d2
is defended by a pawn on e3; the rook on g4 is undefended. Both captures
have the same immediate gain. With an empty history table, generation
order puts Qxd2 first. Quiescence looks at ...exd2 before trying Qxg4.
If normal search has already given Qxg4 credit, quiescence can try it
first. In the focused test, beta is +100. Qxg4 scores +300 and reaches
beta in two nodes instead of four.

Outside check, quiescence now reads the same capture score as normal
search: victim price plus the history adjustment, as calculated in
`capture_score`. It still considers the same captures and promotions.
It does not write history or defer captures that fail SEE. When the king
is in check, the engine keeps its existing order for escaping check.

Across 40 fixed-depth comparisons with lesson 37, every iteration kept
its score and every search kept its best move. One principal variation
changed: at depth three in the castling fixture, the line still began
`e2a6 b4c3 b2c3`, but Black continued with `f6d5` instead of `h3g2`.

That position also shows why this ordering can cost nodes. We compared
three ways to rank quiescence captures in the same engine:

| Quiescence capture order | Depth 1 nodes | Depth 3 nodes |
| --- | ---: | ---: |
| Lesson 37: immediate gain, then cheaper attacker | 661 | 6,200 |
| Reference victim prices, with history held at zero in quiescence | 2,462 | 7,930 |
| Lesson 38: reference victim prices and learned history | 2,462 | 9,176 |

Even an empty history can give a different order from lesson 37. For
example, our teaching values price knights and bishops equally, while
the reference prices a bishop above a knight. The middle row separates
the change of scoring rule from the effect of reading learned entries.

## Lesson 39: which reply to check comes first?

Put the white king on e1, a white bishop on b5, a black rook on e8, and
the black king on h8. The rook gives check along the e-file. White has
six legal replies: Bxe8 takes the checker, Be2 blocks the file, and the
king can step to d1, f1, d2, or f2.

The reference uses a separate ordering rule whenever the king is in
check, including in quiescence. Our `order_evasions` starts from the
same legal list as before. Captures come first, ranked by the reference
price of the piece on the destination minus the attacker's type number
(pawn 1 through king 6). En passant counts as a capture even though its
destination is empty. A promotion that blocks check without taking a
piece belongs with the non-captures.

For a non-capturing reply we read its quiet-history entry. In the example,
giving Be2 the highest quiet score orders the replies as **Bxe8, Be2,
Kd1, Kf1, Kd2, Kf2**. The capture stays first and the tied king steps
keep their generation order. The reference also consults history keyed
by the preceding move; we do not yet have that table. Killers, countermoves,
and SEE deferral do not reorder these escapes. As before, the previous
iteration's preferred move can still go first at the root.

Across 40 fixed-depth comparisons with lesson 38, every iteration kept
its score and every final best move agreed. The defended-rook search
at depth three still found `d1e1 h8g7 e1e6`, now in 1,299 nodes instead
of 1,639. In the white promotion position the depth-three line continued
with `h8g7` instead of `h8h7` after `e7e8q`; both searches took 159 nodes.

## Lesson 40: which move gave check?

White can reach the same position by two different checks. In the first
case, a rook moves from g1 to h1 and checks the black king on h8. In the
second, the rook is already on h1: a white knight moves from h2 to f3
and opens the h-file. In both cases White ends up with Ka1, Rh1, Nf3.
Black's only piece is its king, and both Kg7 and Kg8 are legal.

Ordinary quiet history asks how well Kg8 worked in general. **Continuation
history** asks how well it worked *after Rh1*. The previous mover and its
destination select a row. Inside that row, the replying piece and its
destination select an entry. The rook's row and the knight's row can
therefore disagree even when the board and side to move are identical.

In the focused test, normal search learns a value of +2 for Kg8 after
Rh1. With the ordinary history scores tied, the search at the horizon
then chooses **Kg8 after Rh1+**, but **Kg7 after Nf3+**: the knight's
row is untouched, so generation order breaks the tie. Both lines still
score +800 for White.

For a non-capturing escape we add twice this entry to the ordinary quiet
score. This preserves the reference's 128-to-64 weighting. Captures still
come first. At the root there is no preceding search move, so every quiet
escape gets the same neutral adjustment. Quiescence reads the row; normal
search updates it when a quiet best move raises alpha. Quiet alternatives
among the first 32 moves that failed to raise alpha receive a penalty.
The reference scales and dampens these updates before storing each entry
as a signed byte. The table is cleared for each new `go`.

Only the immediately preceding move contributes here. Across the 40
fixed-depth comparisons with lesson 39, all UCI transcripts remained
identical, including node counts. The two-check example isolates the
ordering difference that those fixtures do not expose.

## Lesson 41: remember quiet replies outside check

Suppose Black usually tries ...a6 first, but ...h6 has worked well after
White plays e4. Can the engine use that experience without preferring
...h6 after every opening move?

The focused test gives ...a6 three points in Black's general quiet
history. It gives ...h6 two reply points in the row for a white pawn
arriving on e4. At depth two, the engine tries:

| White's move | Score for ordering ...a6 | Score for ordering ...h6 | First reply |
| --- | ---: | ---: | --- |
| d4 | 3 | 0 | ...a6 |
| e4 | 3 | 4 | ...h6 |

The reply points count twice. For these two terms, the reference uses
weights of 128 for general history and 256 for the immediately preceding
move. Their ratio is the same as for quiet escapes from check, so both
paths now call `quiet_move_score`. With no preceding move at the root,
only general history contributes. Equal scores keep their order. Killers,
countermoves, and the preferred root move retain their earlier priority.

These numbers rank moves; the search still evaluates both opening lines
at zero. This lesson only reads the table learned in lesson 40. The
reference also considers earlier moves and bonuses for escaping threats.

All 40 fixed-depth comparisons against lesson 40 kept their scores,
principal variations, and final best moves. Three searches changed node
counts: the starting position at depth four went from 2,166 to 2,162,
the defended rook at depth three from 1,299 to 1,263, and the white
promotion at depth three from 159 to 165.

## Lesson 42: what did I play on my last turn?

Suppose Black has just played ...a6. White can learn a general reply to
that move, but it can also remember what worked after its *own* last
move. In the lesson test, the ...a6 entry gives a3 one point and the e4
entry gives h3 three. All other relevant entries start at zero.

| First two plies | Ordering score for a3 | Ordering score for h3 | First quiet move tried |
| --- | ---: | ---: | --- |
| d4 a6 | 2 | 0 | a3 |
| e4 a6 | 2 | 3 | h3 |

The ordinary quiet score now adds three terms: general history, twice
the entry for the opponent's last move, and once the entry for our own
last move. These are ordering points. Both opening lines still score
zero in the depth-three search.

We already keep a row for each move along the current search path.
At ply two, the row at index two belongs to Black's move and the row
at index one to White's. At the root neither row is available. At ply
one only the opponent's move is available. Check evasions still read
only the immediate row; outside check the older row is used only at
positive depth.

Learning uses different weights from ordering. When a quiet best move
raises alpha, we update the immediate row with 949/1024 of the change
and the older row with 1003/1024. The same weights apply to penalties
for tried quiet alternatives that failed to raise alpha. For example,
with both rows initially zero and a bonus depth of four, a best move earns
+6 and +7 respectively; a failed alternative gets −11 and −12.

The live reference agreed on both opening preferences and all four
update values. All 40 fixed-depth transcripts matched lesson 41 exactly,
including node counts. The focused test uses known table entries to show
a difference those positions did not expose.

## Lesson 43: which knight retreat escapes the pawns?

Put a white knight on d4 and black pawns on e5 and a4. The e5 pawn
attacks the knight. Nb3 gets away from that pawn but lands on a square
attacked by the a4 pawn. Nf3 escapes both. With equal history scores,
normal search now tries Nf3 before Nb3.

Before sorting quiet moves, we scan the current board once. For each
square we remember whether an enemy pawn, minor piece (knight or bishop),
or rook attacks it, giving the cheaper attacker precedence. The table
below shows when a move earns a bonus. Use the first matching bonus.

| Moving piece | Attack on its starting square | Bonus for the destination |
| --- | --- | --- |
| Queen | Pawn, minor piece, or rook | 51,700 if none of those attack it; otherwise 14,450 if no pawn attacks it |
| Rook | Pawn or minor piece | 25,600 if neither attacks it; otherwise 14,450 if no pawn attacks it |
| Bishop or knight | Pawn | 14,450 if no pawn attacks it |

All other moves get zero bonus. For ordinary quiet moves we multiply
the history score from lesson 42 by 128 before adding the bonus, keeping
the reference's units. Give Nb3 112 points of history: its 14,336 still
loses to Nf3's 14,450. Give it 113 points and its 14,464 wins.
Those are move-ordering scores, not material gains.

The map uses the board *before* the move. For example, a rook on d1
attacks an enemy queen on d5, but the queen blocks its view of d6.
Qd6 gets the full bonus even though the rook could take it on d6.
The reference does the same; the search still has to examine the reply.
This rule applies outside check at positive depth. Captures, promotions,
and check evasions keep their existing scoring rules.

All 40 fixed-depth comparisons with lesson 42 kept the same scores,
principal variations, and final best moves. Six node counts changed.
The opening position went from 1,376 to 1,225 nodes at depth three. The
castling fixture went from 4,469 to 4,490 at depth two and from 9,176
to 9,036 at depth three.

## Lesson 44: what counts as one node?

White has a king on a1. Black has a king on c2 and a pawn on h7. White
cannot play Kb1 or Kb2 because the black king covers both squares. The
only legal move is **Ka2**. Search it to depth one, first with the
halfmove clock at 99, then from a fresh engine with the clock at zero.

| Starting clock | C: UCI nodes | Reference: UCI nodes | Reference: final bench total |
| ---: | ---: | ---: | ---: |
| 99 | 2 | 1 | 2 |
| 0 | 2 | 2 | 3 |

**Clock 99 isolates the counting rule.** Ka2 brings the clock to 100.
Both engines reach the same child and return a draw without trying a
black move. Follow the counters through the depth-one report:

| Event | C counter | Reference counter |
| --- | ---: | ---: |
| Enter the root search | 1 | 0 |
| Play Ka2, before entering the child | 1 | 1 |
| Enter the child at depth zero | 2 | 1 |
| Return the draw and print the iteration | 2 | 1 |

Our [negamax search](main.c) increments the counter on entry. The
reference increments it in [do_move](minifish-reference-2026-11-05/position.py).
Its depth-zero search hands the same position to quiescence, which adds
nothing to the counter until it plays another move.

**Clock zero shows why equal totals can mislead.** Our engine searches
Ka2 once with a full window and reports −100 centipawns. The reference
starts with a narrow window even at depth one. Its first score falls
below that window. It plays Ka2 again in a wider search, reaches
quiescence again, and reports −189 centipawns. Neither quiescence search
plays a black move. Our two nodes are a root and a child; its two nodes
are two applications of Ka2.

The last column is a separate measurement. After printing the iteration,
the reference [plays Ka2 once more](minifish-reference-2026-11-05/search.py)
to look for a ponder reply in its transposition table. That increments
the worker counter before the benchmark reads it. For fixed-depth UCI
parity, compare the **nodes on the iteration's info line**.

Run this comparison against the local reference with one worker:

```powershell
.\.venv\Scripts\python.exe -B tests\compare_nodes.py build\learning-chess.exe
```

This lesson leaves the C search unchanged. Simply subtracting one root
from each report would fail the clock-zero example. Re-searches matter
too: the C search counts another entry when it searches an already-made
child again, while the reference charges a move only when it plays it.

## Lesson 45 — Count a node when search plays a move

Follow the forced **Ka2** from lesson 44. We start the counter at zero.
After `apply_move` succeeds in the search loop, we add one. At clock 99
the child is a draw, so the depth-one report shows exactly one node.
A search that starts in checkmate plays no move and reports zero nodes.

| Halfmove clock before Ka2 | C depth-one nodes | Reference depth-one nodes |
| ---: | ---: | ---: |
| 99 | 1 | 1 |
| 0 | 1 | 2 |

At clock zero the reference restarts the root search with a wider window.
That loop plays Ka2 a second time. Our depth-one search starts with a
full window and plays it once.

Compare that retry with a re-search inside principal variation search.
In the defended-rook test, White tries Qxd5 first and Black replies
exd5. When White later tries Qb1, the narrow search finds that Qb1
improves on Qxd5. The full-window search can examine the child that
already has the queen on b1. Qb1 adds one to the counter across both
calls. Any replies played in either call add their own nodes.

To check the count by hand, run `position startpos` followed by
`go depth 1`. White plays each of its 20 legal moves once, and Black
has no captures in those positions. The report gives 20 nodes.
The separate `seldepth` field still records the deepest ply reached.

## Lesson 46 — Follow Ka2 through a depth-one aspiration window

Set up `8/7p/8/8/8/8/2k5/K7 w - - 0 1`. White has one legal move,
Ka2. A fresh reference worker starts with an estimated score of zero
and searches within 19 units on either side of that estimate. The
table shows the actual arguments and return value of each root call.
Scores in the table use the reference's internal units.

| Halfmove clock | Attempt | Alpha | Beta | Returned score | Nodes before → after |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 1 | −19 | 19 | −395 | 0 → 1 |
| 0 | 2 | −414 | 0 | −395 | 1 → 2 |
| 99 | 1 | −19 | 19 | 0 | 0 → 1 |

At clock zero the first call plays Ka2 and returns −395. This is below
alpha, so the search has established an upper bound on White's score.
The reference sets the new alpha to the returned score minus the current
margin (`−395 − 19 = −414`). It moves beta to the midpoint of the old
window (`(−19 + 19) / 2 = 0`). The second call plays Ka2 again. This
time −395 lies strictly inside the window, so the search accepts the
score. With 208 internal units per pawn, the UCI conversion truncates
`−395 × 100 / 208` toward zero and prints `score cp -189 nodes 2`.

At clock 99, Ka2 brings the halfmove clock to 100. The draw score fits
inside the first window, so the iteration ends after one move. Our C
engine currently starts depth one with a full window. It plays Ka2 once
in both positions and returns −100 cp at clock zero and 0 cp at clock 99.

Run both positions against a fresh reference worker for each clock:

```powershell
.\.venv\Scripts\python.exe -B tests\compare_nodes.py build\learning-chess.exe
```

The reference search computes each result. The test records the window
and counts around the root call, then checks them against the table.

## Lesson 47 — Give depth one an aspiration window

Before the first move of a search, we have no score from a shallower
iteration. We start with an estimate of zero. The fresh reference worker
in lesson 46 used a margin of 19 internal units. A pawn is 208 units in
the reference and 100 centipawns in our evaluator, so we use 9 centipawns
on each side of zero.

Follow the only legal move, Ka2, in the same two positions:

| Halfmove clock | First C window (cp) | First score (cp) | What the C search does | C / reference nodes |
| ---: | ---: | ---: | --- | ---: |
| 99 | −9 to 9 | 0 | Accepts the draw | 1 / 1 |
| 0 | −9 to 9 | −100 | Lowers alpha and plays Ka2 again | 2 / 2 |

At clock zero, White is a pawn down. The first call returns −100, below
alpha. Our retry loop opens the lower side all the way and keeps beta at
9. The second call returns −100 inside that window, so the engine prints
`score cp -100 nodes 2`. The reference uses the narrower retry window
from lesson 46 and reports −189 cp from its evaluator.

From depth two onward, we have the previous iteration's score and use
our existing 50-centipawn margin around it. Each `go` starts again at
depth one. You can reproduce both Ka2 counts with the comparison command
in lesson 46.

## Lesson 48 — Use the returned score to widen the window

In the forced Ka2 example at clock zero, C searches from −9 to 9 and
returns −100. This **fail-low** score gives us an upper bound. For the
retry we set alpha to the returned score minus the margin (−100 − 9)
and beta to the midpoint of the old window (0). Ka2 returns −100 inside
the new window of −109 to 0. The two attempts play one move each.

For a **fail-high** score we keep alpha and set beta to the returned
score plus the margin. Either kind of failure also grows the margin for
the *following* retry. In C we add one quarter of the current margin,
rounded down, plus one centipawn. The reference adds two internal units
after the quarter, about one centipawn on our scale.

Try `position fen 7k/8/5KQ1/8/8/8/8/8 w - - 0 1` and `go depth 1`.
White has a queen on g6 and can mate on g7. The table follows each C
attempt. Node counts are cumulative from the start of the search.

| Attempt | C window | Returned score | C nodes | Reference nodes |
| ---: | ---: | ---: | ---: | ---: |
| 1 | −9 to 9 | 900 | 7 | 1 |
| 2 | −9 to 909 | 99999 | 23 | 17 |
| 3 | −9 to 100001 | 99999 | 51 | 43 |

The first cutoff proves that White can keep at least a queen's worth of
material. The second search finds Qg7#, whose mate score exceeds 909.
By then the margin has grown to 12. Adding it to 99999 takes beta past
the engine's score limit, so we cap beta at 100001. The third search
can return the mate score inside the window. Both engines report
`score mate 1` and `bestmove g6g7`. Their node counts differ from the
first attempt onward.

## Lesson 49 — Why does the first cutoff take seven nodes?

Put the white king on f6, the white queen on g6, and the black king on
h8. White can mate with Qg7#, but the first aspiration window only asks
whether White can beat a small beta. Both engines reach that cutoff on
**Qb1**. Here is the complete first C attempt, in search order.

| Node | White move | What happens at depth zero | Score for White (cp) |
| ---: | --- | --- | ---: |
| 1 | Ke5 | Stalemate | 0 |
| 2 | Kf5 | Stalemate | 0 |
| 3 | Kg5 | Stalemate | 0 |
| 4 | Ke6 | Stalemate | 0 |
| 5 | Ke7 | Stalemate | 0 |
| 6 | Kf7 | Stalemate | 0 |
| 7 | Qb1 | Black has only the quiet Kg8. Quiescence keeps the material score. | 900 |

After each king move, the queen on g6 covers g8, g7, and h7. Black has
no legal move and is outside check. After Qb1, the queen releases g8.
Quiescence plays no reply, so each row costs exactly one node. The six
zeros fall below beta (9 cp), and 900 reaches the cutoff.

Why does Qb1 come seventh? Our generator scans starting squares in board
order. It reaches the king on f6 before the queen on g6, and the quiet
moves keep their order when their scores tie. The reference generates
queen moves before king moves in this position. Its first legal move is
Qb1, and the root move picker gives that move priority. The reference
trace shows Qb1 returning 2390 internal units against beta 19 after one
node. Qg7# is found on a later attempt.

The focused C test runs the same window twice with fresh move hints.
Giving Qb1 priority brings the first cutoff from seven nodes to one.
Both C runs return 900 cp. To see the reference's first move and its
returned score, run this from PowerShell:

```powershell
@('position fen 7k/8/5KQ1/8/8/8/8/8 w - - 0 1', 'bench 1 1 1 current depth', 'quit') |
    .\.venv\Scripts\python.exe -B tests\trace_root_window.py
```

## Lesson 50 — Choose the first root move

At the start of a fresh search, all 20 moves in the starting position
earn the same material score at depth one. Which move gets to keep that
tie? The reference lists one-square pawn pushes first, in square order.
The C search now tries a2a3 first and reports it after 20 nodes.

The choice uses the reference's generation order once, on the first
attempt at depth one. Pawn pushes, promotions, captures, and en passant
each have their place before the other pieces. After pawns come knights,
bishops, rooks, queens, kings, and castling. The selected move goes
through the same preference mechanism as the best move from a shallower
search.

There is one subtlety when the first candidate leaves its king in check.
The reference fills that slot from the end of its list and checks the
replacement again. Our code gets the same result by finding the last
legal candidate in generation order. For example, with White Ka1, Pb2,
Rd1 against Black Bc3, Kh8, the first candidate b2b3 opens the diagonal
to a1. The replacement is Ka2.

Return to the Qg7# position from lesson 49. Each entry counts only the
moves played during that attempt. The final row is the UCI node count.

| Depth-one search | Previous C | This lesson | Reference |
| --- | ---: | ---: | ---: |
| First attempt | 7 | 1 | 1 |
| Second attempt | 16 | 22 | 16 |
| Third attempt | 28 | 28 | 26 |
| Total | 51 | 51 | 43 |

Qb1 now reaches the first cutoff immediately. Previously, the six king
moves that stalemate Black had already been tried and penalized by that
cutoff. This time they still have their initial history scores when the
second attempt begins. C searches Qb1, then those six king moves, before
it reaches Qg7#. Both engines finish with `score mate 1` and
`bestmove g6g7`.

## Lesson 51 — Account for every move in the second attempt

After Qb1 beats the first window, the engines restart the depth-one
search. C opens its window to −9 through 909 cp. The reference uses −19
through 2409 in its internal units. Both try Qb1 again. Count from one
at the start of this retry so the first attempt's node stays out of the
comparison.

| Moves played, in order | C nodes | Reference nodes |
| --- | ---: | ---: |
| Qb1 | 1 | 1 |
| Ke5, Kf5, Kg5, Ke6, Ke7, Kf7 | 6 | 0 |
| Qg1, Qc2, Qg2, Qd3, Qg3, Qe4, Qg4, Qf5, Qg5 | 9 | 9 |
| Qh5+ Kg8 | 2 | 2 |
| Qh6+ Kg8 | 2 | 2 |
| Qf7 | 1 | 1 |
| Qg7# | 1 | 1 |
| **Second attempt** | **22** | **16** |

The kings start on f6 and h8. After Qb1, C's quiet moves with equal
ordering scores keep their board-scan order, which puts the six moves
from f6 ahead of the remaining moves from g6. Each king move leaves the
queen on g6 covering every escape square, so Black is stalemated and
there is no reply to count. The reference's quiet generator emits queen
moves before king moves. Its search reaches Qg7# and cuts off before
trying any king move.

Both engines play 14 queen moves and two replies. In particular, each
checking line with Kg8 costs two nodes, one for each side's move.
Counting the whole depth-one search gives 51 nodes in C and 43 in the
reference. Six of that eight-node difference arise here and two in the
third attempt.

To replay the comparison, build a copy with move tracing enabled. It
prints a record when search counts a move. A second search of an
already-played child adds a record only if it plays another move.

```powershell
& C:\msys64\ucrt64\bin\gcc.exe -std=c11 -Wall -Wextra -Wpedantic -O0 -g -DTRACE_SEARCH_MOVES main.c -o build\trace-moves.exe
.\.venv\Scripts\python.exe -B tests\compare_retry_moves.py build\trace-moves.exe
```

The comparison prints both sequences, indents Black's replies, and
checks that inserting the six king moves after Qb1 reproduces the
entire C trace for the retry.

## Lesson 52 — Break quiet-move ties at the root

After Qb1 raises the lower bound in the Qg7# position, several quiet
moves have equal ordering scores. Previously C kept them in board-scan
order, so six moves from the king on f6 came before the remaining queen
moves. Now the ordinary quiet-move sort at the root compares generation
order when two scores tie. The comparison from lesson 50 places queen
moves before king moves and visits the queen's destinations in square
order. A higher history or escape score still takes priority.

On the second attempt the engines now play the same 14 queen moves and
the same two replies, in the same order. Qg7# reaches the cutoff before
any king move is tried. Counts below start again at zero for each attempt.

| Depth-one search | Previous C | This lesson | Reference |
| --- | ---: | ---: | ---: |
| First attempt | 1 | 1 | 1 |
| Second attempt | 22 | 16 | 16 |
| Third attempt | 28 | 28 | 26 |
| Total reported | 51 | 45 | 43 |

Run the trace comparison from lesson 51 to replay the retry. It now
checks equality of the complete move sequence, including the ply of each
reply. In the third attempt C still plays two more moves than the
reference.

## Lesson 53 — Why does Black play Kg8 twice more in C?

In the Qg7# fixture both engines enter the third attempt with 17 nodes
already counted. White tries Qg7# first. The wider window admits its
mate score, so the root continues checking the alternatives. This time
both engines play the same 23 White moves in the same order. The
difference occurs one ply deeper, when Black is in check.

| Black to move after | C on attempt 3 | Reference on attempt 3 | Bound saved on attempt 2 |
| --- | --- | --- | ---: |
| Qh5+ | Plays Kg8 | Returns from the table | At least −1946 |
| Qh6+ | Plays Kg8 | Returns from the table | At least −1884 |

The bounds are in reference units, from Black's perspective. On attempt
two the reference played Kg8 in each position and saved a lower bound
in its transposition table. On attempt three the scout search asks
whether Black can score at least **−31999**. Even the weaker bound,
−1946, clears that threshold. Each entry also covers the required
quiescence depth. The reference can return the stored bound immediately.
The C search calculates the answer again, playing Kg8 once in each
position.

The replay from lesson 51 now runs a third search as a control. In a
fresh reference process it treats the table lookups after Qh5+ and Qh6+
as misses. The first two attempts still match the ordinary reference
trace. On the third, the reference plays both king replies and matches
all 28 C moves in order.

| Depth-one nodes | C | Reference | Reference with two forced misses |
| --- | ---: | ---: | ---: |
| First two attempts combined | 17 | 17 | 17 |
| Third attempt | 28 | 26 | 28 |
| UCI total | 45 | 43 | 45 |

To reproduce the comparison with the trace build, run
`python tests/compare_retry_moves.py build/trace-moves.exe` using the
project Python environment.

## Lesson 54 — Remember a quiescence bound

Consider Black's turn after Qh5+ in the Qg7# fixture. On the second
attempt C tries Kg8 and establishes that Black can score at least −900
centipawns. On the third attempt White has already found mate in one.
Black only needs to beat a very low threshold. The saved lower bound
answers that question, and search returns before playing Kg8 again.
The same reasoning applies after Qh6+.

A fresh `go` starts with an empty table of 64 entries. A completed
quiescence move loop records a lower bound when it reaches beta.
Otherwise it records an upper bound. The table fills in order and
replaces its oldest entry when full. For a one-point search window, a
matching entry can end the search under the following conditions.

| Saved result | Return the saved score when |
| --- | --- |
| Lower bound | The score is at least beta |
| Upper bound | The score is at most alpha |

For example, a lower bound of −900 is enough when beta is −900. Raise
beta to −899 and Black must search. A wider window also searches the
moves so it can build a principal variation.

The table compares the actual board, side to move, castling and en
passant rights, and halfmove clock. It also checks the ply, since the
distance to mate and the remaining recursion limit depend on it.
Two paths can reach the same board yet offer different opportunities
to draw by repetition. Each entry therefore remembers the earlier
search positions since the last pawn move or capture, back to the
first child of the root. The game history and root are shared for the
whole `go`. Terminal and draw checks still run before the lookup.

In the depth-one Qg7# replay, C and the reference now play the same
moves in the same order on all three attempts. They spend **1, 16, and
26 nodes** and report `score mate 1`, `nodes 43`, and `pv g6g7`. The
lesson 53 control still adds two Kg8 replies when the reference treats
the corresponding lookups as misses. C reports selective depth 2 for
this fixture and the reference reports 1.

## Lesson 55 — Which visit raises selective depth?

In the Qg7# fixture the engines play the same 43 moves at depth one.
Follow nodes 12 and 13 on the second attempt. White plays Qh5+ from
the root, then Black plays Kg8 in quiescence. The king move leaves the
search two plies from the root. C records that distance as soon as it
enters the child. Its maximum was one after Qb1 at node 1, rises to two
at node 13, and stays there through the third attempt.

The reference updates its worker's maximum inside the positive-depth
search at a principal-variation (PV) node. A depth-zero call goes
straight to quiescence, before that update. At depth one the root is
the only positive-depth PV node and its ply is zero. The worker still
holds zero after each of the three attempts. The reference saves that
value with Qg7 and adds one when printing the move's UCI report.

Now continue the same search to depth two. The first move is Qg7# at
node 44. Its child enters the reference's positive-depth PV search at
ply one, which raises the worker maximum to one. The printed value is
two. C starts its depth counter afresh and reaches only ply one during
this iteration.

| Completed depth | Cumulative nodes in each engine | C UCI seldepth | Reference saved depth | Reference UCI seldepth |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 43 | 2 | 0 | 1 |
| 2 | 66 | 1 | 1 | 2 |

Rebuild the trace executable with the command in lesson 51, then run
the comparison. It records the C moves that raised the maximum and
checks the reference worker, selected move, and UCI reports separately.

```powershell
.\.venv\Scripts\python.exe -B tests\compare_seldepth.py build\trace-moves.exe
```

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
