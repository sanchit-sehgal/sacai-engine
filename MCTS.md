## MCTS Eval

For its primary evaluation form (besides [EUNN](EUNN.md)), SACAI uses the Monte-Carlo Tree Search algorithm. MCTS is an empirical search algorithm that combines traditional search formulae (such as alpha-beta pruning) with machine learning to most efficiently output the best move in any given position. Additionally, SACAI conjucts the MCTS algorithm with UCT (Upper Confidence bounds applied to Trees) to most effectively analyze the moves (nodes) that will ultimately lead to a winning position for the engine.


<p align="center">
  <img src="https://miro.medium.com/max/2000/1*Ntm0xHhJ5jOgsL9AdB2kNw.jpeg" />
</p>


The image above is a visual representation of each step in SACAI's MCTS evaluation. By recursively analyzing thousands of nodes per seconds, SACAI is able to quickly analyze positions that it has seen before with its SNN. To learn more about the MCTS search algorithm (and the source of the image), visit [this site](https://medium.com/@quasimik/monte-carlo-tree-search-applied-to-letterpress-34f41c86e238).
