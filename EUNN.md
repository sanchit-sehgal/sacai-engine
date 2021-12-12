## EUNN Structures

EUNN, or Efficiently Updatable Neural Network, is an architecture that aims to replace the traditional alpha-beta pruning evaluation form that was commonplace in strong chess engines several years ago. Since then, many of the world's strongest chess engines (including Stockfish and lc0) have since added Neural Network evaluation to their computations. SACAI uses the lc0 neural networks to evaluate many board positions, allowing it to evaluate previously-seen positions in a much more efficient manner. 

In the SNN (stimulated neural network), the engine is able to train previously analyzed positions and effectively remove any nodes that would lead to a better position for the opponent. The neural network is compromised of three distinct segments: the input layer of nodes, the "hidden layer" of nodes, and the output nodes. As the neural network is trained by playing more games, the hidden layer becomes more complex, and thus more receptive. Once the neural network has been effectively trained, the engine is able to cluster a variety of positions (say, 5-piece endgames) with a high velocity, resulting in an optimized output. 

<p align="center">
  <img src="https://1.cms.s81c.com/sites/default/files/2021-01-06/ICLH_Diagram_Batch_01_03-DeepNeuralNetwork-WHITEBG.png" />
</p>

## EUNN Formulae

Each node can be represented as a "neuron" with the following weights:

<p align="center">
  <img src="https://i.stack.imgur.com/VqOpE.jpg" />
</p>

By manipulating the weights to a reasonable extent, the engine is able to formulate its play style based on the weights on its neural network. For example, a weight more suitable for bullet games may be useful in finding a suboptimal move extremely quickly, while a weight more suitable for classical games may find the most optimal move with the greatest future prospects in a lengthy fashion. As the depth of the engine increases, the number of "hidden layers" increases, causing the weights to become more prevalent in the resulting output.

## Multi-dimensional Neural Nets

A single-dimensional neural network is limited to a linear scan of the nodes. Although adding more dimensions to a neural network may seem to complicate the sorting problem significantly, the formulation of the sort remains consistent regardless of the nunber of dimensions. A linear (single-dimensional) neural network can be represented with this simple summative formula:

<p align="center">
  <img src="https://i.stack.imgur.com/4VuGC.png" />
</p>

The particular SNN in SACAI is an example of a backpropagation of a convolutional neural network, where the convolution can be represented with the following equation:

<p align="center">
  <img src="https://miro.medium.com/max/1134/1*ns8pgfCGO9T0dkpi5vgegA.png" />
</p>

To leaarn more about Neural Networks in chess, you can visit [this link](https://www.chessprogramming.org/Neural_Networks).
