/*
  This file is part of Leela Chess Zero.
  Copyright (C) 2018-2019 The LCZero Authors

  Leela Chess is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Leela Chess is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Leela Chess.  If not, see <http://www.gnu.org/licenses/>.

  Additional permission under GNU GPL version 3 section 7

  If you modify this Program, or any covered work, by linking or
  combining it with NVIDIA Corporation's libraries from the NVIDIA CUDA
  Toolkit and the NVIDIA CUDA Deep Neural Network library (or a
  modified version of those libraries), containing parts covered by the
  terms of the respective license agreement, the licensors of this
  Program grant you additional permission to convey the resulting work.
*/
#include <algorithm>
#include <cassert>
#include <functional>
#include <list>
#include <memory>
#include <mutex>

#include "cuda_common.h"
#include "inputs_outputs.h"
#include "kernels.h"
#include "layers.h"
#include "neural/factory.h"
#include "neural/network_legacy.h"
#include "neural/shared/policy_map.h"
#include "utils/bititer.h"
#include "utils/exception.h"

#include "chess/board.h"
#include "chess/position.h"
#include "src/syzygy/syzygy.h"

#include <iostream>
#include <fstream>

//#define DEBUG_RAW_NPS
#pragma pack(4)
struct CeresInputPlane {
  uint64_t mask;
  float value;
};

const int MAX_MOVES = 96;

const int NUM_PLANES_PER_POSITION = 112;
const int MAX_POSITIONS_PER_BATCH = 1024;

const int CERES_INPUT_PLANE_SIZE_NUM_ELEMENTS =
NUM_PLANES_PER_POSITION * MAX_POSITIONS_PER_BATCH;

#pragma pack(4)
struct ItemIn {
  uint64_t Masks[NUM_PLANES_PER_POSITION];
  float Values[NUM_PLANES_PER_POSITION];

  uint64_t Hash;
  uint32_t NumMoves;
  uint16_t Moves[MAX_MOVES];
};

#pragma pack(4)
struct CeresTransferBlockIn {
  ItemIn Items[MAX_POSITIONS_PER_BATCH];
};

#pragma pack(4)
struct ItemOut {
  float Q;
  float D;
  float P[MAX_MOVES];
  float M;
};

#pragma pack(4)
struct CeresTransferBlockOut {
  ItemOut ItemsOut[MAX_POSITIONS_PER_BATCH];
};

#pragma pack(4)
struct CeresTransferBlock {
  CeresTransferBlockIn BlockIn;
  CeresTransferBlockOut BlockOut;
};

static bool logInfo = true; // if GPU device info should be logged

namespace lczero {
  using namespace cudnn_backend;

  template <typename DataType>
  class CudaNetwork;

  template <typename DataType>
  class CudaNetworkComputation : public NetworkComputation {
  public:
    CudaNetworkComputation(CudaNetwork<DataType>* network, bool wdl,
      bool moves_left);
    ~CudaNetworkComputation();

    void AddInput(InputPlanes&& input) override {
      const auto iter_mask =
        &inputs_outputs_->input_masks_mem_[batch_size_ * kInputPlanes];
      const auto iter_val =
        &inputs_outputs_->input_val_mem_[batch_size_ * kInputPlanes];

      int i = 0;
      for (const auto& plane : input) {
        iter_mask[i] = plane.mask;
        iter_val[i] = plane.value;
        i++;
      }

      batch_size_++;
    }

    void ComputeBlocking() override;

    int GetBatchSize() const override { return batch_size_; }

    float GetQVal(int sample) const override {
      if (wdl_) {
        auto w = inputs_outputs_->op_value_mem_[3 * sample + 0];
        auto l = inputs_outputs_->op_value_mem_[3 * sample + 2];
        return w - l;
      }
      else {
        return inputs_outputs_->op_value_mem_[sample];
      }
    }

    float GetDVal(int sample) const override {
      if (wdl_) {
        auto d = inputs_outputs_->op_value_mem_[3 * sample + 1];
        return d;
      }
      else {
        return 0.0f;
      }
    }

    float GetPVal(int sample, int move_id) const override {
      return inputs_outputs_->op_policy_mem_[sample * kNumOutputPolicy + move_id];
    }

    float GetMVal(int sample) const override {
      if (moves_left_) {
        return inputs_outputs_->op_moves_left_mem_[sample];
      }
      return 0.0f;
    }

  private:
    // Memory holding inputs, outputs.
    std::unique_ptr<InputsOutputs> inputs_outputs_;
    int batch_size_;
    bool wdl_;
    bool moves_left_;

    CudaNetwork<DataType>* network_;
  };

  template <typename DataType>
  class CudaNetwork : public Network {
  public:
    CudaNetwork(const WeightsFile& file, const OptionsDict& options)
      : capabilities_{ file.format().network_format().input(),
                      file.format().network_format().moves_left() } {
      LegacyWeights weights(file.weights());
      gpu_id_ = options.GetOrDefault<int>("gpu", 0);

      conv_policy_ = file.format().network_format().policy() ==
        pblczero::NetworkFormat::POLICY_CONVOLUTION;

      max_batch_size_ = options.GetOrDefault<int>("max_batch", 1024);

      if (logInfo) showInfo();

      int total_gpus;
      ReportCUDAErrors(cudaGetDeviceCount(&total_gpus));

      if (gpu_id_ >= total_gpus)
        throw Exception("Invalid GPU Id: " + std::to_string(gpu_id_));

      cudaDeviceProp deviceProp = {};
      cudaGetDeviceProperties(&deviceProp, gpu_id_);
      if (logInfo) showDeviceInfo(deviceProp);

      // Select GPU to run on (for *the current* thread).
      ReportCUDAErrors(cudaSetDevice(gpu_id_));

      ReportCUBLASErrors(cublasCreate(&cublas_));

      // Default layout is nchw.
      bool hasTensorCores = false;

      if (std::is_same<half, DataType>::value) {
        // Check if the GPU support FP16.

        if ((deviceProp.major == 6 && deviceProp.minor != 1) ||
          (deviceProp.major == 5 && deviceProp.minor == 3)) {
          // FP16 without tensor cores supported on GP100 (SM 6.0) and Jetson
          // (SM 5.3 and 6.2). SM 6.1 GPUs also have FP16, but slower than FP32.
          ;
        }
        else if (deviceProp.major >= 7) {
          // Some GPUs (GTX 16xx) are SM 7.5 but don't have tensor cores
          // enabling TENSOR_OP_MATH for them works but is very very slow
          // (likely because the system emulates it).
          if (!strstr(deviceProp.name, "GTX 16")) {
            hasTensorCores = true;
          }
        }
        else {
          throw Exception("Your GPU doesn't support FP16");
        }
      }

      if (hasTensorCores)
        ReportCUBLASErrors(cublasSetMathMode(cublas_, CUBLAS_TENSOR_OP_MATH));

      const int kNumInputPlanes = kInputPlanes;
      const int kNumFilters = (int)weights.input.biases.size();
      numBlocks_ = (int)weights.residual.size();

      // Warn if the memory required for storing transformed weights is
      // going to exceed 40% of total video memory, force custom_winograd off
      // if it's going to exceed 50% of memory.
      size_t residual_single_layer_weight_size =
        3 * 3 * kNumFilters * kNumFilters * sizeof(DataType);
      size_t residual_weight_size =
        residual_single_layer_weight_size * numBlocks_ * 2;
      size_t transformed_residual_weight_size = residual_weight_size * 4;

      if (transformed_residual_weight_size > 0.4 * deviceProp.totalGlobalMem) {
        CERR << "WARNING: Low GPU video memory. You may run into OOM errors. Try "
          "using a smaller network.";
      }

      // Disable res block fusing for > 384 filters (the fused output input
      // transform kernel runs out of register space) and for fp32 for now.
      if (kNumFilters <= 384 && std::is_same<half, DataType>::value) {
        use_res_block_winograd_fuse_opt_ = true;
      }
      else {
        use_res_block_winograd_fuse_opt_ = false;
      }
      // Override if set in backend-opts.
      if (!options.IsDefault<bool>("res_block_fusing")) {
        use_res_block_winograd_fuse_opt_ = options.Get<bool>("res_block_fusing");
      }

      const bool use_gemm_ex = deviceProp.major >= 5;

      // 0. Check for SE.
      has_se_ = false;
      if (weights.residual[0].has_se) {
        has_se_ = true;
      }

      // Have some minumum as we also use this for transforming weights.
      size_t max_weight_size = 128 * 1024 * 1024;

      // parts from scratch allocation are suballocated to hold various weights
      // and biases when transforming winograd weights (one layer at a time), 128
      // MB is way more than that what we need but make sure it's at least 3x of
      // single layer's weight size to be safe.
      if (max_weight_size < 3 * residual_single_layer_weight_size)
        max_weight_size = 3 * residual_single_layer_weight_size;

      scratch_size_ = max_weight_size;

      // Need additional space for transformed input/outputs which are 36/16
      // times size (4x4 block transformed into 6x6).
      const size_t transformed_tensor_size = (size_t)(
        max_batch_size_ * kNumFilters * 64 * (36.0 / 16.0) * sizeof(DataType));
      scratch_size_ = std::max(scratch_size_, 2 * transformed_tensor_size);

      ReportCUDAErrors(cudaMalloc(&scratch_mem_, scratch_size_));
#ifdef DEBUG_RAW_NPS
      CERR << "allocated " << scratch_size_ << " bytes for scratch memory";
#endif

      // 2. Build the network, and copy the weights to GPU memory.

      // Input.
      {
        auto inputConv = std::make_unique<FusedWinogradConvSELayer<DataType>>(
          nullptr, kNumFilters, 8, 8, kNumInputPlanes, true, true, false,
          false, 0, use_gemm_ex);
        inputConv->LoadWeights(&weights.input.weights[0],
          &weights.input.biases[0], scratch_mem_);
        network_.emplace_back(std::move(inputConv));
      }

      // Residual block.
      for (int block = 0; block < numBlocks_; block++) {
        bool has_se = weights.residual[block].has_se;
        int se_k = (int)weights.residual[block].se.b1.size();

        if (use_res_block_winograd_fuse_opt_) {
          auto layer = std::make_unique<ResidualBlock<DataType>>(
            getLastLayer(), kNumFilters, has_se, se_k, use_gemm_ex,
            block == 0, block == (numBlocks_ - 1));
          layer->LoadWeights0(&weights.residual[block].conv1.weights[0],
            &weights.residual[block].conv1.biases[0],
            scratch_mem_);
          layer->LoadWeights1(&weights.residual[block].conv2.weights[0],
            &weights.residual[block].conv2.biases[0],
            scratch_mem_);
          if (has_se)
            layer->LoadSEWeights(&weights.residual[block].se.w1[0],
              &weights.residual[block].se.b1[0],
              &weights.residual[block].se.w2[0],
              &weights.residual[block].se.b2[0], scratch_mem_);
          network_.emplace_back(std::move(layer));
        }
        else {
          auto conv1 = std::make_unique<FusedWinogradConvSELayer<DataType>>(
            getLastLayer(), kNumFilters, 8, 8, kNumFilters, true, true, false,
            false, 0, use_gemm_ex);
          conv1->LoadWeights(&weights.residual[block].conv1.weights[0],
            &weights.residual[block].conv1.biases[0],
            scratch_mem_);
          network_.emplace_back(std::move(conv1));

          auto conv2 = std::make_unique<FusedWinogradConvSELayer<DataType>>(
            getLastLayer(), kNumFilters, 8, 8, kNumFilters, true, true, true,
            has_se, se_k, use_gemm_ex);
          conv2->LoadWeights(&weights.residual[block].conv2.weights[0],
            &weights.residual[block].conv2.biases[0],
            scratch_mem_);
          if (has_se)
            conv2->LoadSEWeights(&weights.residual[block].se.w1[0],
              &weights.residual[block].se.b1[0],
              &weights.residual[block].se.w2[0],
              &weights.residual[block].se.b2[0],
              scratch_mem_);
          network_.emplace_back(std::move(conv2));
        }
      }

      resi_last_ = getLastLayer();

      // Policy head.
      if (conv_policy_) {
        auto conv1 = std::make_unique<FusedWinogradConvSELayer<DataType>>(
          resi_last_, kNumFilters, 8, 8, kNumFilters, true, true, false,
          false, 0, use_gemm_ex);
        conv1->LoadWeights(&weights.policy1.weights[0],
          &weights.policy1.biases[0], scratch_mem_);
        network_.emplace_back(std::move(conv1));

        auto pol_channels = weights.policy.biases.size();

        // No relu
        auto conv2 = std::make_unique<FusedWinogradConvSELayer<DataType>>(
          getLastLayer(), pol_channels, 8, 8, kNumFilters, false, true, false,
          false, 0, use_gemm_ex);
        conv2->LoadWeights(&weights.policy.weights[0], &weights.policy.biases[0],
          scratch_mem_);
        network_.emplace_back(std::move(conv2));

        auto policymap = std::make_unique<PolicyMapLayer<DataType>>(
          getLastLayer(), kNumOutputPolicy, 1, 1, 73 * 8 * 8);
        policymap->LoadWeights(kConvPolicyMap, scratch_mem_);

        network_.emplace_back(std::move(policymap));
      }
      else {
        auto convPol = std::make_unique<Conv1Layer<DataType>>(
          resi_last_, weights.policy.biases.size(), 8, 8, kNumFilters, true,
          true, use_gemm_ex);
        convPol->LoadWeights(&weights.policy.weights[0],
          &weights.policy.biases[0], scratch_mem_);
        network_.emplace_back(std::move(convPol));

        auto FCPol = std::make_unique<FCLayer<DataType>>(
          getLastLayer(), weights.ip_pol_b.size(), 1, 1, false, true);
        FCPol->LoadWeights(&weights.ip_pol_w[0], &weights.ip_pol_b[0],
          scratch_mem_);
        network_.emplace_back(std::move(FCPol));
      }
      policy_out_ = getLastLayer();

      // Value head.
      {
        auto convVal = std::make_unique<Conv1Layer<DataType>>(
          resi_last_, weights.value.biases.size(), 8, 8, kNumFilters, true,
          true, use_gemm_ex);
        convVal->LoadWeights(&weights.value.weights[0], &weights.value.biases[0],
          scratch_mem_);
        network_.emplace_back(std::move(convVal));

        auto FCVal1 = std::make_unique<FCLayer<DataType>>(
          getLastLayer(), weights.ip1_val_b.size(), 1, 1, true, true);
        FCVal1->LoadWeights(&weights.ip1_val_w[0], &weights.ip1_val_b[0],
          scratch_mem_);
        network_.emplace_back(std::move(FCVal1));

        wdl_ = file.format().network_format().value() ==
          pblczero::NetworkFormat::VALUE_WDL;
        auto fc2_tanh = !wdl_;

        auto FCVal2 = std::make_unique<FCLayer<DataType>>(
          getLastLayer(), weights.ip2_val_b.size(), 1, 1, false, true,
          fc2_tanh);
        FCVal2->LoadWeights(&weights.ip2_val_w[0], &weights.ip2_val_b[0],
          scratch_mem_);
        network_.emplace_back(std::move(FCVal2));
      }
      value_out_ = getLastLayer();

      // Moves left head
      moves_left_ = (file.format().network_format().moves_left() ==
        pblczero::NetworkFormat::MOVES_LEFT_V1) &&
        options.GetOrDefault<bool>("mlh", true);
      if (moves_left_) {
        auto convMov = std::make_unique<Conv1Layer<DataType>>(
          resi_last_, weights.moves_left.biases.size(), 8, 8, kNumFilters,
          true, true, use_gemm_ex);
        convMov->LoadWeights(&weights.moves_left.weights[0],
          &weights.moves_left.biases[0], scratch_mem_);
        network_.emplace_back(std::move(convMov));

        auto FCMov1 = std::make_unique<FCLayer<DataType>>(
          getLastLayer(), weights.ip1_mov_b.size(), 1, 1, true, true);
        FCMov1->LoadWeights(&weights.ip1_mov_w[0], &weights.ip1_mov_b[0],
          scratch_mem_);
        network_.emplace_back(std::move(FCMov1));

        auto FCMov2 = std::make_unique<FCLayer<DataType>>(getLastLayer(), 1, 1, 1,
          true, true);
        FCMov2->LoadWeights(&weights.ip2_mov_w[0], &weights.ip2_mov_b[0],
          scratch_mem_);
        network_.emplace_back(std::move(FCMov2));
      }
      moves_left_out_ = getLastLayer();

      // 3. Allocate GPU memory for running the network:
      //    - three buffers of max size are enough (one to hold input, second to
      //      hold output and third to hold skip connection's input).

      // size of input to the network
      size_t maxSize = max_batch_size_ * kNumInputPlanes * 64 * sizeof(DataType);

      // take max size of all layers
      for (auto& layer : network_) {
        maxSize = std::max(maxSize, layer->GetOutputSize(max_batch_size_));
      }

      if (use_res_block_winograd_fuse_opt_ && scratch_size_ > maxSize)
        maxSize = scratch_size_;

      for (auto& mem : tensor_mem_) {
        ReportCUDAErrors(cudaMalloc(&mem, maxSize));
        ReportCUDAErrors(cudaMemset(mem, 0, maxSize));
      }

#ifdef DEBUG_RAW_NPS
      CERR << "allocated " << 3 * maxSize
        << " bytes of GPU memory to run the network";
#endif
    }

    void forwardEval(InputsOutputs* io, int batchSize) {
      std::unique_lock<std::mutex> lock(lock_);

#ifdef DEBUG_RAW_NPS
      auto t_start = std::chrono::high_resolution_clock::now();
#endif

      // Expand packed planes to full planes.
      uint64_t* ipDataMasks = io->input_masks_mem_gpu_;
      float* ipDataValues = io->input_val_mem_gpu_;

      bool fp16 = std::is_same<half, DataType>::value;
      if (fp16) {
        expandPlanes_Fp16_NCHW((half*)(tensor_mem_[0]), ipDataMasks,
          ipDataValues, batchSize * kInputPlanes);
      }
      else {
        expandPlanes_Fp32_NCHW((float*)(tensor_mem_[0]), ipDataMasks,
          ipDataValues, batchSize * kInputPlanes);
      }

      float* opPol = io->op_policy_mem_gpu_;
      float* opVal = io->op_value_mem_gpu_;
      float* opMov = io->op_moves_left_mem_gpu_;

      int l = 0;
      // Input.
      network_[l++]->Eval(
        batchSize,
        use_res_block_winograd_fuse_opt_ ? tensor_mem_[1] : tensor_mem_[2],
        tensor_mem_[0], nullptr, scratch_mem_, scratch_size_, nullptr,
        cublas_);  // input conv

    // Residual block.
      for (int block = 0; block < numBlocks_; block++) {
        if (use_res_block_winograd_fuse_opt_) {
          network_[l++]->Eval(batchSize, tensor_mem_[2], tensor_mem_[1], nullptr,
            scratch_mem_, scratch_size_, nullptr,
            cublas_);  // block
        }
        else {
          network_[l++]->Eval(batchSize, tensor_mem_[0], tensor_mem_[2], nullptr,
            scratch_mem_, scratch_size_, nullptr,
            cublas_);  // conv1

          network_[l++]->Eval(batchSize, tensor_mem_[2], tensor_mem_[0],
            tensor_mem_[2], scratch_mem_, scratch_size_, nullptr,
            cublas_);  // conv2
        }
      }

      // Policy head.
      if (conv_policy_) {
        network_[l++]->Eval(batchSize, tensor_mem_[0], tensor_mem_[2], nullptr,
          scratch_mem_, scratch_size_, nullptr,
          cublas_);  // policy conv1

        network_[l++]->Eval(batchSize, tensor_mem_[1], tensor_mem_[0], nullptr,
          scratch_mem_, scratch_size_, nullptr,
          cublas_);  // policy conv2

        if (fp16) {
          network_[l++]->Eval(batchSize, tensor_mem_[0], tensor_mem_[1], nullptr,
            scratch_mem_, scratch_size_, nullptr,
            cublas_);  // policy map layer
          copyTypeConverted(opPol, (half*)(tensor_mem_[0]),
            batchSize * kNumOutputPolicy);  // POLICY output
        }
        else {
          network_[l++]->Eval(batchSize, (DataType*)opPol, tensor_mem_[1],
            nullptr, scratch_mem_, scratch_size_, nullptr,
            cublas_);  // policy map layer  // POLICY output
        }
      }
      else {
        network_[l++]->Eval(batchSize, tensor_mem_[0], tensor_mem_[2], nullptr,
          scratch_mem_, scratch_size_, nullptr,
          cublas_);  // pol conv

        if (fp16) {
          network_[l++]->Eval(batchSize, tensor_mem_[1], tensor_mem_[0], nullptr,
            scratch_mem_, scratch_size_, nullptr,
            cublas_);  // pol FC

          copyTypeConverted(opPol, (half*)(tensor_mem_[1]),
            batchSize * kNumOutputPolicy);  // POLICY
        }
        else {
          network_[l++]->Eval(batchSize, (DataType*)opPol, tensor_mem_[0],
            nullptr, scratch_mem_, scratch_size_, nullptr,
            cublas_);  // pol FC  // POLICY
        }
      }

      // Copy policy output from device memory to host memory.
      ReportCUDAErrors(cudaMemcpyAsync(
        io->op_policy_mem_, io->op_policy_mem_gpu_,
        sizeof(float) * kNumOutputPolicy * batchSize, cudaMemcpyDeviceToHost));

      // value head
      network_[l++]->Eval(batchSize, tensor_mem_[0], tensor_mem_[2], nullptr,
        scratch_mem_, scratch_size_, nullptr,
        cublas_);  // value conv

      network_[l++]->Eval(batchSize, tensor_mem_[1], tensor_mem_[0], nullptr,
        scratch_mem_, scratch_size_, nullptr,
        cublas_);  // value FC1

      if (wdl_) {
        if (fp16) {
          network_[l++]->Eval(batchSize, tensor_mem_[0], tensor_mem_[1], nullptr,
            scratch_mem_, scratch_size_, nullptr,
            cublas_);  // value FC2    // VALUE
          copyTypeConverted(opVal, (half*)(tensor_mem_[0]),
            3 * batchSize);  // VALUE
        }
        else {
          network_[l++]->Eval(batchSize, (DataType*)opVal, tensor_mem_[1],
            nullptr, scratch_mem_, scratch_size_, nullptr,
            cublas_);  // value FC2    // VALUE
        }
      }
      else {
        if (fp16) {
          // TODO: consider fusing the bias-add of FC2 with format conversion.
          network_[l++]->Eval(batchSize, tensor_mem_[0], tensor_mem_[1], nullptr,
            scratch_mem_, scratch_size_, nullptr,
            cublas_);  // value FC2
          copyTypeConverted(opVal, (half*)(tensor_mem_[0]), batchSize);  // VALUE
        }
        else {
          network_[l++]->Eval(batchSize, (DataType*)opVal, tensor_mem_[1],
            nullptr, scratch_mem_, scratch_size_, nullptr,
            cublas_);  // value FC2    // VALUE
        }
      }

      if (moves_left_) {
        // Moves left head
        network_[l++]->Eval(batchSize, tensor_mem_[0], tensor_mem_[2], nullptr,
          scratch_mem_, scratch_size_, nullptr,
          cublas_);  // moves conv

        network_[l++]->Eval(batchSize, tensor_mem_[1], tensor_mem_[0], nullptr,
          scratch_mem_, scratch_size_, nullptr,
          cublas_);  // moves FC1

// Moves left FC2
        if (fp16) {
          // TODO: consider fusing the bias-add of FC2 with format conversion.
          network_[l++]->Eval(batchSize, tensor_mem_[0], tensor_mem_[1], nullptr,
            scratch_mem_, scratch_size_, nullptr, cublas_);
          copyTypeConverted(opMov, (half*)(tensor_mem_[0]), batchSize);
        }
        else {
          network_[l++]->Eval(batchSize, (DataType*)opMov, tensor_mem_[1],
            nullptr, scratch_mem_, scratch_size_, nullptr,
            cublas_);
        }
      }

      ReportCUDAErrors(cudaDeviceSynchronize());
      // The next thread can start using the GPU now.
      lock.unlock();

      if (wdl_) {
        // Value softmax done cpu side.
        for (int i = 0; i < batchSize; i++) {
          float w = std::exp(io->op_value_mem_[3 * i + 0]);
          float d = std::exp(io->op_value_mem_[3 * i + 1]);
          float l = std::exp(io->op_value_mem_[3 * i + 2]);
          float sum = w + d + l;
          w /= sum;
          l /= sum;
          d = 1.0f - w - l;
          io->op_value_mem_[3 * i + 0] = w;
          io->op_value_mem_[3 * i + 1] = d;
          io->op_value_mem_[3 * i + 2] = l;
        }
      }

#ifdef DEBUG_RAW_NPS
      const int reportingCalls = 100;
      static int numCalls = 0;
      static int sumBatchSize = 0;
      static double totalTime = 0;

      sumBatchSize += batchSize;
      numCalls++;

      auto t_end = std::chrono::high_resolution_clock::now();

      double dt = std::chrono::duration<double>(t_end - t_start).count();
      totalTime += dt;
      if (numCalls == reportingCalls) {
        double avgBatchSize = ((double)sumBatchSize) / numCalls;
        double nps = sumBatchSize / totalTime;
        CERR << "Avg batch size: " << avgBatchSize
          << ", NN eval time: " << totalTime << " seconds per " << sumBatchSize
          << " evals. NPS: " << nps;
        sumBatchSize = 0;
        totalTime = 0;
        numCalls = 0;
      }
#endif
    }

    ~CudaNetwork() {
      for (auto mem : tensor_mem_) {
        if (mem) ReportCUDAErrors(cudaFree(mem));
      }
      if (scratch_mem_) ReportCUDAErrors(cudaFree(scratch_mem_));
      cublasDestroy(cublas_);
    }

    const NetworkCapabilities& GetCapabilities() const override {
      return capabilities_;
    }

    std::unique_ptr<NetworkComputation> NewComputation() override {
      // Set correct gpu id for this computation (as it might have been called
      // from a different thread).
      ReportCUDAErrors(cudaSetDevice(gpu_id_));
      return std::make_unique<CudaNetworkComputation<DataType>>(this, wdl_,
        moves_left_);
    }

    std::unique_ptr<InputsOutputs> GetInputsOutputs() {
      std::lock_guard<std::mutex> lock(inputs_outputs_lock_);
      if (free_inputs_outputs_.empty()) {
        return std::make_unique<InputsOutputs>(max_batch_size_, wdl_,
          moves_left_);
      }
      else {
        std::unique_ptr<InputsOutputs> resource =
          std::move(free_inputs_outputs_.front());
        free_inputs_outputs_.pop_front();
        return resource;
      }
    }

    void ReleaseInputsOutputs(std::unique_ptr<InputsOutputs> resource) {
      std::lock_guard<std::mutex> lock(inputs_outputs_lock_);
      free_inputs_outputs_.push_back(std::move(resource));
    }

    // Apparently nvcc doesn't see constructor invocations through make_unique.
    // This function invokes constructor just to please complier and silence
    // warning. Is never called (but compiler thinks that it could).
    void UglyFunctionToSilenceNvccWarning() { InputsOutputs io(0, false, false); }

  private:
    const NetworkCapabilities capabilities_;
    cublasHandle_t cublas_;
    int gpu_id_;
    int max_batch_size_;
    bool wdl_;
    bool moves_left_;
    bool use_res_block_winograd_fuse_opt_;    // fuse operations inside the residual tower

    // Currently only one NN Eval can happen a time (we can fix this if needed
    // by allocating more memory).
    mutable std::mutex lock_;

    int numBlocks_;
    bool has_se_;
    bool conv_policy_;
    std::vector<std::unique_ptr<BaseLayer<DataType>>> network_;
    BaseLayer<DataType>* getLastLayer() { return network_.back().get(); }

    BaseLayer<DataType>* resi_last_;
    BaseLayer<DataType>* policy_out_;
    BaseLayer<DataType>* value_out_;
    BaseLayer<DataType>* moves_left_out_;

    DataType* tensor_mem_[3];
    void* scratch_mem_;
    size_t scratch_size_;

    mutable std::mutex inputs_outputs_lock_;
    std::list<std::unique_ptr<InputsOutputs>> free_inputs_outputs_;

    void showInfo() const {
      int version;
      int ret = cudaRuntimeGetVersion(&version);
      switch (ret) {
      case cudaErrorInitializationError:
        throw Exception("CUDA driver and/or runtime could not be initialized");
      case cudaErrorInsufficientDriver:
        throw Exception("No CUDA driver, or one older than the CUDA library");
      case cudaErrorNoDevice:
        throw Exception("No CUDA-capable devices detected");
      }
      int major = version / 1000;
      int minor = (version - major * 1000) / 10;
      int pl = version - major * 1000 - minor * 10;
      CERR << "CUDA Runtime version: " << major << "." << minor << "." << pl;
      if (version != CUDART_VERSION) {
        major = CUDART_VERSION / 1000;
        minor = (CUDART_VERSION - major * 1000) / 10;
        pl = CUDART_VERSION - major * 1000 - minor * 10;
        CERR << "WARNING: CUDA Runtime version mismatch, was compiled with "
          "version "
          << major << "." << minor << "." << pl;
      }
      cudaDriverGetVersion(&version);
      major = version / 1000;
      minor = (version - major * 1000) / 10;
      pl = version - major * 1000 - minor * 10;
      CERR << "Latest version of CUDA supported by the driver: " << major << "."
        << minor << "." << pl;
      if (version < CUDART_VERSION) {
        CERR << "WARNING: code was compiled with unsupported CUDA version.";
      }
    }

    void showDeviceInfo(const cudaDeviceProp& deviceProp) const {
      CERR << "GPU: " << deviceProp.name;
      CERR << "GPU memory: " << deviceProp.totalGlobalMem / std::pow(2.0f, 30)
        << " Gb";
      CERR << "GPU clock frequency: " << deviceProp.clockRate / 1e3f << " MHz";
      CERR << "GPU compute capability: " << deviceProp.major << "."
        << deviceProp.minor;

      if (std::is_same<float, DataType>::value && deviceProp.major >= 7) {
        CERR << "WARNING: you will probably get better performance from the "
          "cuda-fp16 backend.";
      }
    }
  };

  template <typename DataType>
  CudaNetworkComputation<DataType>::CudaNetworkComputation(
    CudaNetwork<DataType>* network, bool wdl, bool moves_left)
    : wdl_(wdl), moves_left_(moves_left), network_(network) {
    batch_size_ = 0;
    inputs_outputs_ = network_->GetInputsOutputs();
  }

  template <typename DataType>
  CudaNetworkComputation<DataType>::~CudaNetworkComputation() {
    network_->ReleaseInputsOutputs(std::move(inputs_outputs_));
  }

  template <typename DataType>
  void CudaNetworkComputation<DataType>::ComputeBlocking() {
    network_->forwardEval(inputs_outputs_.get(), GetBatchSize());
  }

  template <typename DataType>
  std::unique_ptr<Network> MakeCudaNetwork(const std::optional<WeightsFile>& w,
    const OptionsDict& options) {
    if (!w) {
      throw Exception(
        "The cuda" +
        std::string(std::is_same<half, DataType>::value ? "-fp16" : "") +
        " backend requires a network file.");
    }
    const WeightsFile& weights = *w;
    if (weights.format().network_format().network() !=
      pblczero::NetworkFormat::NETWORK_CLASSICAL_WITH_HEADFORMAT &&
      weights.format().network_format().network() !=
      pblczero::NetworkFormat::NETWORK_SE_WITH_HEADFORMAT) {
      throw Exception(
        "Network format " +
        std::to_string(weights.format().network_format().network()) +
        " is not supported by the CUDA backend.");
    }
    if (weights.format().network_format().policy() !=
      pblczero::NetworkFormat::POLICY_CLASSICAL &&
      weights.format().network_format().policy() !=
      pblczero::NetworkFormat::POLICY_CONVOLUTION) {
      throw Exception("Policy format " +
        std::to_string(weights.format().network_format().policy()) +
        " is not supported by the CUDA backend.");
    }
    if (weights.format().network_format().value() !=
      pblczero::NetworkFormat::VALUE_CLASSICAL &&
      weights.format().network_format().value() !=
      pblczero::NetworkFormat::VALUE_WDL) {
      throw Exception("Value format " +
        std::to_string(weights.format().network_format().value()) +
        " is not supported by the CUDA backend.");
    }
    if (weights.format().network_format().moves_left() !=
      pblczero::NetworkFormat::MOVES_LEFT_NONE &&
      weights.format().network_format().moves_left() !=
      pblczero::NetworkFormat::MOVES_LEFT_V1) {
      throw Exception(
        "Movest left head format " +
        std::to_string(weights.format().network_format().moves_left()) +
        " is not supported by the CUDA backend.");
    }
    return std::make_unique<CudaNetwork<DataType>>(weights, options);
  }

  std::unique_ptr<Network> MakeCudaNetworkAuto(
    const std::optional<WeightsFile>& weights, const OptionsDict& options) {
    int gpu_id = options.GetOrDefault<int>("gpu", 0);
    cudaDeviceProp deviceProp = {};
    // No error checking here, this will be repeated later.
    cudaGetDeviceProperties(&deviceProp, gpu_id);

    // Check if the GPU supports FP16.
    if (deviceProp.major >= 7 ||
      (deviceProp.major == 6 && deviceProp.minor != 1) ||
      (deviceProp.major == 5 && deviceProp.minor == 3)) {
      if (logInfo) CERR << "Switching to [cuda-fp16]...";
      return MakeCudaNetwork<half>(weights, options);
    }
    if (logInfo) CERR << "Switching to [cuda]...";
    return MakeCudaNetwork<float>(weights, options);
  }

  // static std::unique_ptr<CudnnNetwork<half>> _network = nullptr;
  static std::unique_ptr<Network> _network[32];


  void ProcessExternalNNRequest(int sessionIndex, int numPositions,
    CeresTransferBlockIn* inputs,
    CeresTransferBlockOut* outputs) {
    //    CeresTransferBlock* block = (CeresTransferBlock*)transferBlock;

  //  std::unique_ptr<Network> network = std::move(_network[sessionIndex]);

    // printf("here\r\n");
    auto computation = _network[sessionIndex]->NewComputation();

    //  if (_network[sessionIndex] == nullptr)
    //    _network[sessionIndex] = std::move(network);

    int numItems = 0;

    // For each requested position...
    for (int i = 0; i < numPositions; i++) {
      ItemIn* blockInItem = &inputs->Items[i];

      numItems++;

      // Transfer moves
      //std::vector<uint16_t> moves;
      //moves.insert(moves.end(), &blockInItem->Moves[0],
      //             &blockInItem->Moves[blockInItem->NumMoves]);

      // Transfer over inputs
      std::vector<InputPlane> planes(NUM_PLANES_PER_POSITION);
      for (int j = 0; j < NUM_PLANES_PER_POSITION; j++) {
        planes[j].mask = blockInItem->Masks[j];
        planes[j].value = blockInItem->Values[j];
      }

      // Add this position as input
      computation->AddInput(std::move(planes));
    }

    // Compute
    computation->ComputeBlocking();

    // Retrieve results
    for (int i = 0; i < numItems; i++) {
      outputs->ItemsOut[i].Q = computation->GetQVal(i);
      outputs->ItemsOut[i].D = computation->GetDVal(i);
      outputs->ItemsOut[i].M = computation->GetMVal(i);

      // Retrieve moves
      for (int m = 0; m < inputs->Items[i].NumMoves; m++) {
        short move = inputs->Items[i].Moves[m];
        outputs->ItemsOut[i].P[m] = computation->GetPVal(i, move);
      }
    }
    computation->~NetworkComputation();
  }

  static bool haveRedirected = false;

#define ERROR_INVALID_GPU_ID -2;

  // ----------------------------------------------------------------------------------------------
  int Alloc(int sessionIndex, char* networkFilename, int gpuID) {

    int total_gpus;
    ReportCUDAErrors(cudaGetDeviceCount(&total_gpus));

    if (gpuID >= total_gpus) return ERROR_INVALID_GPU_ID;

    const WeightsFile weights = LoadWeightsFromFile(networkFilename);

    if (_network[sessionIndex] != nullptr) {
      printf("LC0 DLL error: session already allocated %f (Alloc)\r\n", (float)sessionIndex);
      return -1;
    }
    else {
      OptionsDict options(nullptr);
      options.Set<int>("gpu", gpuID);

      //  int gpu_id = options.GetOrDefault<int>("gpu", 0);
      //  printf("GPU ID %f\r\n", (float)gpu_id);

      const WeightsFile& wf = weights;
      const OptionsDict& od = options;

      //_network = std::make_unique<CudnnNetwork<half>>(weights, od);

      logInfo = false;

      _network[sessionIndex] = MakeCudaNetworkAuto(weights, od);

      return 0;
    }
  }

  void Free(int sessionIndex) {
    if (_network[sessionIndex] == nullptr) {
      printf("LC0 DLL error: unallocated session %f (Free)\r\n", (float)sessionIndex);
    }
    else {
      _network[sessionIndex].release();
      _network[sessionIndex] = nullptr;
    }
  }

  int Compute(int sessionIndex, int batchSize, CeresTransferBlockIn* inputs,
    CeresTransferBlockOut* outputs) {
    //   printf("here %f\r\n", (float)batchSize);

    if (_network[sessionIndex] == nullptr) {
      printf("LC0 DLL error: unallocated session %f (Compute)\r\n", (float)sessionIndex);
    }
    else {
      ProcessExternalNNRequest(sessionIndex, batchSize, inputs, outputs);
      //   printf("done\r\n");
      return 0;
    }
  }




  static std::unique_ptr<SyzygyTablebase> syzygy_tb_[32];

#define TB_INITIALIZE_FAIL 0
#define TB_INITIALIZE_OK_WDL_ONLY 1
#define TB_INITIALIZE_OK_WDL_DTZ 2


  int TBInitialize(int sessionIndex, char* paths) {
    lczero::InitializeMagicBitboards();

    syzygy_tb_[sessionIndex] = std::make_unique<SyzygyTablebase>();
    if (!syzygy_tb_[sessionIndex]->init(paths)) {
      CERR << "Failed to load Syzygy tablebases!";
      return TB_INITIALIZE_FAIL;
    }
    else {
      return TB_INITIALIZE_OK_WDL_DTZ;
    }
  }

  void TBFree(int sessionIndex) {
    if (syzygy_tb_[sessionIndex] == nullptr) {
      printf("LC0 DLL error: unallocated session %f (TBFree)\r\n",
        (float)sessionIndex);
    }
    else {
      syzygy_tb_[sessionIndex].release();
      syzygy_tb_[sessionIndex] = nullptr;
    }
  }

  int MaxCardinality(int sessionIndex) {
    if (syzygy_tb_[sessionIndex] == nullptr) {
      printf("LC0 DLL error: unallocated session %f (MaxCardinality)\r\n", (float)sessionIndex);
    }
    else
      return syzygy_tb_[sessionIndex]->max_cardinality();
  }


  int ProbeDTZ(int sessionIndex, char* fen) {
    if (syzygy_tb_[sessionIndex] == nullptr) {
      printf("LC0 DLL error: unallocated session %f (ProbeDTZ)\r\n",
        (float)sessionIndex);
    }
    else {
      ChessBoard board;
      PositionHistory history;
      board.SetFromFen(fen);
      history.Reset(board, 0, 1);

      MoveList root_moves;
      if (syzygy_tb_[sessionIndex]->root_probe(
        history.Last(),
        true, /*fast_play || history.DidRepeatSinceLastZeroingMove(),*/
        &root_moves))
      {
        return root_moves[0].as_packed_int();//      .as_nn_index(0);
      }
      else
        return -1;

      //    ProbeState result;
      //    return syzygy_tb_[sessionIndex]->probe_dtz(history.Last(), &result);
    }
  }


  int ProbeWDL(int sessionIndex, char* fen)
  {
    if (syzygy_tb_[sessionIndex] == nullptr) {
      printf("LC0 DLL error: unallocated session %f (ProbeWDL)\r\n", (float)sessionIndex);
    }
    else {
      ChessBoard board;
      PositionHistory history;
      board.SetFromFen(fen);
      history.Reset(board, 0, 1);

      ProbeState result;
      WDLScore score =
        syzygy_tb_[sessionIndex]->probe_wdl(history.Last(), &result);

      return (result + 10) * 256 + (score + 10);
    }
  }

  REGISTER_NETWORK("cuda-auto", MakeCudaNetworkAuto, 104)
    REGISTER_NETWORK("cuda", MakeCudaNetwork<float>, 103)
    REGISTER_NETWORK("cuda-fp16", MakeCudaNetwork<half>, 102)

}  // namespace lczero

extern "C" {
  __declspec(dllimport) int Alloc(int sessionIndex, char* networkFilename,
    int gpuID);
}
extern "C" {
  __declspec(dllimport) void Free(int sessionIndex);
}
extern "C" {
  __declspec(dllimport) int Compute(int sessionIndex, int batchSize,
    CeresTransferBlockIn* inputs,
    CeresTransferBlockOut* outputs);
}

extern "C" {
  __declspec(dllimport) int TBInitialize(int sessionIndex, char* paths);
  __declspec(dllimport) void TBFree(int sessionIndex);
  __declspec(dllimport) int MaxCardinality(int sessionIndex);
  __declspec(dllimport) int ProbeWDL(int sessionIndex, char* fen);
  __declspec(dllimport) int ProbeDTZ(int sessionIndex, char* fen);
}


int Alloc(int sessionIndex, char* networkFilename, int gpuID) {
  return lczero::Alloc(sessionIndex, networkFilename, gpuID);
}
void Free(int sessionIndex) { lczero::Free(sessionIndex); }

int Compute(int sessionIndex, int batchSize, CeresTransferBlockIn* inputs,
  CeresTransferBlockOut* outputs) {
  return lczero::Compute(sessionIndex, batchSize, inputs, outputs);
}

int TBInitialize(int sessionIndex, char* paths) { return lczero::TBInitialize(sessionIndex, paths); }
void TBFree(int sessionIndex) { lczero::TBFree(sessionIndex); }
int MaxCardinality(int sessionIndex) { return lczero::MaxCardinality(sessionIndex); }

int ProbeWDL(int sessionIndex, char* fen) { return lczero::ProbeWDL(sessionIndex, fen); }
int ProbeDTZ(int sessionIndex, char* fen) { return lczero::ProbeDTZ(sessionIndex, fen); }
