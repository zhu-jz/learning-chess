"""Lesson positions. Scores are for the player making the first capture."""

# Name, board, en passant square, move, teaching score, highest passing
# reference threshold. All positions have White to move and no castling rights.
SEE_CASES = [
    ("undefended rook", "7k/8/8/3r4/8/8/8/K2Q4", "-", "d1d5", 500, 1276),
    ("pawn defends rook", "7k/8/4p3/3r4/8/8/8/K2Q4", "-", "d1d5", -400, -1262),
    ("decline recapture", "3q3k/8/8/3r4/2P5/8/8/K2R4", "-", "c4d5", 500, 1276),
    ("rook behind queen", "7k/8/4p3/3r4/8/3Q4/8/K2R4", "-", "d3d5", -300, -1054),
    ("cheapest reply", "6k1/8/4p3/3r4/2Bq4/8/K7/7Q", "-", "h1d5", -400, -1262),
    ("pinned defender", "4k3/8/4p3/3r4/8/8/8/K2QR3", "-", "d1d5", 500, 1276),
    ("en passant opens file", "7k/8/8/3pP3/8/8/K7/3r4", "d6", "e5d6", 0, 0),
    ("en passant wins pawn", "7k/8/8/3pP3/8/8/K7/8", "d6", "e5d6", 100, 0),
    ("capture and promote", "1r5k/P7/8/8/8/8/8/K7", "-", "a7b8n", 700, 0),
    ("reply promotes", "7k/8/8/8/R7/8/1p5K/n7", "-", "a4a1", -1000, -495),
    ("king can recapture", "8/8/4k3/3r4/8/8/8/K2Q4", "-", "d1d5", -400, -1262),
    ("king cannot recapture", "8/8/4k3/3r4/2P5/8/8/K2Q4", "-", "d1d5", 500, 1276),
]
