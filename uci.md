## UCI Protocol

SACAI engine supports the use of the UCI (Universal Chess Interface) protocol between the executable application and any UCI-supported chess GUI. With UCI, the engine is able to successfully communicate between a variety of different GUIs, including (but not limited to):
* ArenaChess GUI
* Cutechess
* Chessbase

The following UCI commands can be passed into SACAI:
* **uci**- This tells SACAI to use the build-in UCI protocol
* **debug [on/off]**- This turns the debug nature of SACAI on or off, depending on the passed parameter.
* **isready**- When using a compatible chess GUI, this command will synchronize the engine with the GUI, allowing SACAI to receive and transmit UCI-compatible commands to and from the GUI.
* **ucinewgame**- This begins a new game with the UCI protocol.
* **position [startpos/fen] {moves}**- This determines the position that is input into SACAI. If the second parameter of the command is _startpos_, the engine will set the position to the default start position on the chessboard. The _startpos_ command can also be used with a succeeding _moves_ command, which highlights the moves that occur after the board has been set to the starting position. Additionally, the _fen_ command (Forsyth-Edwards Notation) can be following with a compatible FEN string that sets the board position. You can learn more about FEN strings [here](https://en.wikipedia.org/wiki/Forsyth%E2%80%93Edwards_Notation).
* **go**- This commands tells SACAI to analyze the given position and find the best move with the weights provided. This command must always be preceded with a complete _position_ command.
  * **ponder**- This command allows the engine to analyze a position while the opponents time is running. This will decrease the total time that SACAI spends on its own clock, but it will increase threading usage.
  * **wtime/btime**- This command passes the remaining time for white and black (respectively) to the engine.
  * **depth**- This command sets the depth ply to a set value. The depth is the height of the plies between the "root" and the horizon node (nodes at depth=0).
  * **nodes**- This command tells the engine to only search the passed number of nodes. After pruning those nodes, the engine will return the best move only within those nodes. A smaller amount of nodes may result in the output of a suboptimal move, but the engine will be able to analyze the position significantly faster. 
  * **mate**- If a mate is known (i.e. in a puzzle), the engine will only search for a "bestmove" that results in a mate in the number of moves (passed parameter: i.e. mate 4).
  * **movetime**- The engine will only search for the bestmove for the passed number of milliseconds.
  * **movestogo**- The engine will search for moves that extend the game until the next time control occurs.
* **stop**- This command stops the node search immediately and outputs the best move at the time when the command was passed.
* **quit**- This command exits the application.

