# SACAI- A Distinct MDNN & MCTS Chess Engine
SACAI is a neural chess engine using the Monte-Carlo Tree Search algorithm with intensive neural-pruning for advancements in chess SNN application and exhaustive position evaluation. The engine uses a novel advancement in chess neural networking and was built primarily with Microsoft's [dotNet](https://dotnet.microsoft.com/en-us/download/dotnet-framework) v4.7.2 framework with the utilization of [LeelaChessZero's](https://github.com/LeelaChessZero/lc0) trained neural nets (see [weights](weights/)). 

This project heavily relied on the previous advancements made by several established and newfangled chess engines, such as Google's own [AlphaZero](https://deepmind.com/blog/article/alphazero-shedding-new-light-grand-games-chess-shogi-and-go). This project was primarily developed to productively appease boredom and to advance neural-networking in chess engines. Although there are several well-established EUNN chess engines that currently exist, this particular engine aimed to:
* facilitate neural-pruning progression in chess programming.
* enhance node search speed
* amplify the power of node-debug in SNN engines
* serve as an establishment for developing further algorithmic innovations

## Development and Implementations
SACAI was first initialized as a functioning UCI-capable engine near the end of 2021. The neural networks of SACAI were virtually untrainable due to hardware limitations, resulting in the engine being currently dependent on the trained LeelaChessZero neural networks. During its initial phase of development, several advancements in its MDNN were made, including:Ceres
* the ability to function on lower-performance devices
* implementation of an optimized node-search theory
* several algorithmic advancements including aggressive pruning techniques
* tested sub-node analytical postulations for more explorative node searches

At the time of release, SACAI was competitive with established neural chess engines such as Houdini, Stockfish, and LeelaChessZero. The engine uses a variety of optimized sub-tree searching algorithms, including a depth-first search with postorder traversal.

## Acknowledgement and Thanks
The development of SACAI was extremely dependent on the works of many developers, most notably:
* **Ceres**, for the basic MCTS search algorithms and much of the evaluation theory
* **LeelaChessZero**, for the openly-available neural networks
* **Alex Graves, Santiago Fernández, and Jürgen Schmidhuber**, for their extensive and illuminating research on MDNN implementation (see paper [here](https://people.idsia.ch/~juergen/icann_2007.pdf))
* the variety of other developers who have contributed to the establishment of neural networking in chess research
