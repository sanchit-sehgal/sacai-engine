# SACAI- A Distinct MDNN & MCTS Chess Engine
SACAI is a neural chess engine using the Monte-Carlo Tree Search algorithm with intensive neural-pruning for advancements in chess SNN application and exhaustive position evaluation. The engine uses a novel advancement in chess neural networking and was built primarily with Microsoft's [dotNet](https://dotnet.microsoft.com/en-us/download/dotnet-framework) v4.7.2 framework with the utilization of [LeelaChessZero's](https://github.com/LeelaChessZero/lc0) trained neural nets (see [weights](weights/)). 

This project heavily relied on the previous advancements made by several established and newfangled chess engines, such as Google's own [AlphaZero](https://deepmind.com/blog/article/alphazero-shedding-new-light-grand-games-chess-shogi-and-go). This project was primarily developed to productively appease boredom and to advance neural-networking in chess engines. Although there are several well-established EUNN chess engines that currently exist, this particular engine aimed to:
* facilitate neural-pruning progression in chess programming.
* enhance node search speed
* amplify the power of node-debug in SNN engines
* serve as an establishment for developing further algorithmic innovations

## Development and Implementations
SACAI was first initialized as a functioning UCI-capable engine near the end of 2021. The neural networks of SACAI were virtually untrainable due to hardware limitations, resulting in the engine being currently dependent on the trained LeelaChessZero neural networks. During its initial phase of development, several advancements in its MDNN were made, including:
* the ability to function on lower-performance devices
* implementation of an optimized node-search theory
* several algorithmic advancements including aggressive pruning techniques
* tested sub-node analytical postulations for more explorative node searches

At the time of release, SACAI was competitive with established neural chess engines such as Houdini, Stockfish, and LeelaChessZero. The engine uses a variety of optimized sub-tree searching algorithms, including a depth-first search with postorder traversal.

## Acknowledgements and Thanks
The development of SACAI was extremely dependent on the works of many developers, most notably:
* **Ceres**, for the advanced MCTS search algorithms and much of the board evaluation theory used in SACAI
* **LeelaChessZero**, for the openly-available neural networks and initial frameworks for engine development
* **Alex Graves, Santiago Fernández, and Jürgen Schmidhuber**, for their extensive and illuminating research on MDNN implementation (see paper [here](https://people.idsia.ch/~juergen/icann_2007.pdf))
* the variety of other developers who have contributed to the establishment of neural networking in chess research

## Installation and Configuration
At this time, the source code compilation is quite complicated and installing a simple executable is far simpler. To install SACAI without the source code, visit the [releases](https://github.com/sanchit-sehgal/sacai-engine/releases/tag/latest) page and download the `<version_name>.zip` or `<version_name>.tar.gz` file. Extract the files appropriately and navigate to the directory of extraction. Run the command `./sacai --config=sacai.cfg` to ensure that the files have been properly extracted and work as expected.

To build SACAI from its source code, visit the [releases](https://github.com/sanchit-sehgal/sacai-engine/releases/tag/latest) tag and install the file titled `Source Code.zip` or `Source Code.tar.gz`. Unpack the file and navigate to the appropriate directory. Within the unpacked folder, find `/sacai-engine-main /buildtest/sacai/` and execute the build file with the `./build.sh`. Once the file is compiled, copy the contents of the weights folder from the initial unpacked directory into `/buildtest/sacai/build/release`. Additionally, copy `sacai.cfg` from the initial unpacked directory into the `/buildtest/sacai/build/release` as well so that both the weights and config file are located in the `/release` directory. Then, run `./sacai --config=sacai.cfg` and ensure the config file loads appropriately.

For OS-dependent installation instructions, view [setup.md](setup.md).

## Contributing
SACAI is proudly open-source and invites developers to further advance its algorithms. The engine uses the Apache License and can be redistributed or modified within the stated terms. See [LICENSE.md](LICENSE.md) for the specific terms and conditions.
