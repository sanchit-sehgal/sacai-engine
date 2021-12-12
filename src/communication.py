import sys
import chess
import argparse
from movegeneration import next_move
from fendict import fen_dict
import random

def talk():
    """
    The main input/output loop.
    This implements a slice of the UCI protocol.
    """
    board = chess.Board()
    depth = get_depth()

    while True:
        msg = input()
        command(depth, board, msg)


def command(depth: int, board: chess.Board, msg: str):
    """
    Accept UCI commands and respond.
    The board state is also updated.
    """
    msg = msg.strip()
    tokens = msg.split(" ")
    while "" in tokens:
        tokens.remove("")

    if msg == "quit":
        sys.exit()

    if msg == "uci":
        print("id name Sacai")  
        print("id author Sanchit Sehgal")
        print("uciok")
        return

    if msg == "isready":
        print("readyok")
        return

    if msg == "ucinewgame":
        return

    if msg.startswith("position"):
        if len(tokens) < 2:
            return

        # Set starting position
        if tokens[1] == "startpos":
            board.reset()
            moves_start = 2
        elif tokens[1] == "fen":
            fen = " ".join(tokens[2:8])
            revised_fen = " ".join(tokens[2:6])
            board.set_fen(fen)
            moves_start = 8
            if tokens[1] == "fen":
                if revised_fen in fen_dict:
                    _move = fen_dict.get(revised_fen)
                    print(f"bestmove {_move}")
                    return
                else:
                    pass
        else:
            return

        # Apply moves
        if len(tokens) <= moves_start or tokens[moves_start] != "moves":
            return

        for move in tokens[(moves_start+1):]:
            board.push_uci(move)

    if msg == "d":
        # Non-standard command, but supported by Stockfish and helps debugging
        print(board)
        print(board.fen())

    if msg[0:2] == "go":
        fen = board.fen()
        fen = fen.split(" ")
        new_fen = " ".join(fen[0:4])
        if new_fen in fen_dict:
            _move = fen_dict.get(new_fen)
            print(f"bestmove {_move}")
            return
        else:
            _move = next_move(depth, board)
            print(f"bestmove {_move}")
        return


def get_depth() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--depth", default=1, help="provide an integer (default: 9)")
    args = parser.parse_args()
    return max([2, int(args.depth)])