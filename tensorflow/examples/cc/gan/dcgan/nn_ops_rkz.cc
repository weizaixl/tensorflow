/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");

You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#include <algorithm>
#include <vector>

#include "tensorflow/cc/ops/const_op.h"
#include "tensorflow/cc/ops/nn_ops.h"
#include "tensorflow/cc/ops/nn_ops_internal.h"
#include "tensorflow/cc/ops/standard_ops.h"

#include "tensorflow/examples/cc/gan/dcgan/nn_ops_rkz.h"
#include "tensorflow/examples/cc/gan/dcgan/util.h"

namespace tensorflow {
namespace ops {

// tf.nn.batch_normalization
// def batch_normalization(x,
//                         mean,
//                         variance,
//                         offset,
//                         scale,
//                         variance_epsilon,
//                         name=None):
//   with ops.name_scope(name, "batchnorm", [x, mean, variance, scale, offset]):
//     inv = math_ops.rsqrt(variance + variance_epsilon)
//     if scale is not None:
//       inv *= scale
//     # Note: tensorflow/contrib/quantize/python/fold_batch_norms.py depends on
//     # the precise order of ops that are generated by the expression below.
//     return x * math_ops.cast(inv, x.dtype) + math_ops.cast(
//         offset - mean * inv if offset is not None else -mean * inv, x.dtype)
BatchNormalization::BatchNormalization(
    const ::tensorflow::Scope& scope, const ::tensorflow::Input& x,
    const ::tensorflow::Input& mean, const ::tensorflow::Input& variance,
    const ::tensorflow::Input& offset, const ::tensorflow::Input& scale,
    const ::tensorflow::Input& variance_epsilon) {
  auto inv = Rsqrt(scope, Add(scope, variance, variance_epsilon));
  LOG(INFO) << "Node building status: " << scope.status();

  auto ret1 = Multiply(scope, x, Cast(scope, inv, DT_FLOAT));
  LOG(INFO) << "Node building status: " << scope.status();

  auto ret2 = Multiply(scope, mean, inv);
  LOG(INFO) << "Node building status: " << scope.status();

  this->output =
      Add(scope, ret1, Cast(scope, Sub(scope, offset, ret2), DT_FLOAT));
}

Dropout::Dropout(const ::tensorflow::Scope& scope, const ::tensorflow::Input x,
                 const int rate) {
  float keep_prob = 1 - rate;
  auto random_value5 = RandomUniform(scope, Shape(scope, x), DT_FLOAT);
  LOG(INFO) << "Node building status: " << scope.status();

  auto random_tensor =
      Add(scope, random_value5, Const<float>(scope, {keep_prob}));
  LOG(INFO) << "Node building status: " << scope.status();

  auto binary_tensor = Floor(scope, random_tensor);
  LOG(INFO) << "Node building status: " << scope.status();

  this->output = Multiply(
      scope, Div(scope, x, Const<float>(scope, {keep_prob})), binary_tensor);
}

// python code:
//     # The logistic loss formula from above is
//     #   x - x * z + log(1 + exp(-x))
//     # For x < 0, a more numerically stable formula is
//     #   -x * z + log(1 + exp(x))
//     # Note that these two expressions can be combined into the following:
//     #   max(x, 0) - x * z + log(1 + exp(-abs(x)))
//     # To allow computing gradients at zero, we define custom versions of max
//     and # abs functions. zeros = array_ops.zeros_like(logits,
//     dtype=logits.dtype) cond = (logits >= zeros) relu_logits =
//     array_ops.where(cond, logits, zeros) neg_abs_logits =
//     array_ops.where(cond, -logits, logits) return math_ops.add(
//         relu_logits - logits * labels,
//         math_ops.log1p(math_ops.exp(neg_abs_logits)),
//         name=name)
SigmoidCrossEntropyWithLogits::SigmoidCrossEntropyWithLogits(
    const ::tensorflow::Scope& scope, const ::tensorflow::Input labels,
    const ::tensorflow::Input logits) {
  auto zeros = ZerosLike(scope, logits);
  auto cond = GreaterEqual(scope, logits, zeros);
  auto relu_logits = SelectV2(scope, cond, logits, zeros);
  auto neg_abs_logits = SelectV2(scope, cond, Negate(scope, logits), logits);

  this->output =
      Add(scope, Sub(scope, relu_logits, Multiply(scope, logits, labels)),
          Log1p(scope, Exp(scope, neg_abs_logits)));
}

// Only DT_FLOAT and 2D/4D shape is supported for now
GlorotUniform::GlorotUniform(const ::tensorflow::Scope& scope,
                             const std::initializer_list<int64>& shape) {
  // RandomUniform
  auto random_value =
      RandomUniform(scope, Const(scope, Input::Initializer(shape)), DT_FLOAT);
  LOG(INFO) << "Node building status: " << scope.status();

  std::vector<int64> shape_vec(shape);

  // For 2D
  float fan_in = shape_vec[0];
  float fan_out = shape_vec[1];

  // For 4D
  if (shape_vec.size() == 4) {
    float receptive_field_size = 1.0f * shape_vec[0] * shape_vec[1];
    fan_in = receptive_field_size * shape_vec[2];
    fan_out = receptive_field_size * shape_vec[3];
  }

  // Python code:
  //   scale /= max(1., (fan_in + fan_out) / 2.)
  //   limit = math.sqrt(3.0 * scale) => minval is -limit, maxval is limit
  //   result = math_ops.add(rnd * (maxval - minval), minval, name=name)
  float scale = 1.0f / std::max(1.0f, (fan_in + fan_out) / 2.0f);
  float limit = std::sqrt(3.0f * scale);
  float maxval = limit;
  float minval = -limit;
  auto result =
      Add(scope,
          Multiply(scope, random_value, Const<float>(scope, (maxval - minval))),
          Const<float>(scope, minval));
  LOG(INFO) << "Node building status: " << scope.status();

  this->output = result;
}

// Conv2DTranspose
Conv2DTranspose::Conv2DTranspose(const ::tensorflow::Scope& scope,
                                 const ::tensorflow::Input& input_sizes,
                                 const ::tensorflow::Input& filter,
                                 const ::tensorflow::Input& out_backprop,
                                 const gtl::ArraySlice<int>& strides,
                                 const StringPiece padding) {
  // Conv2DBackpropInput
  this->output = Conv2DBackpropInput(scope, input_sizes, filter, out_backprop,
                                     strides, padding);
}

// Generator
// TODO(Rock): handle the case of is_training false
Generator::Generator(const ::tensorflow::Scope& scope, const int batch_size) {
  // random noise input
  auto noise = RandomNormal(scope, {batch_size, NOISE_DIM}, DT_FLOAT);
  LOG(INFO) << "Node building status: " << scope.status();

  // dense 1
  this->w1 = Variable(scope, {NOISE_DIM, UNITS}, DT_FLOAT);
  LOG(INFO) << "Node building status: " << scope.status();

  auto rate = Const(scope, {0.01f});
  auto random_value = RandomNormal(scope, {NOISE_DIM, UNITS}, DT_FLOAT);
  this->assign_w1 =
      Assign(scope, this->w1, Multiply(scope, random_value, rate));
  auto dense = MatMul(scope, noise, this->w1);
  LOG(INFO) << "Node building status: " << scope.status();

  this->w1_wm = Variable(scope, {NOISE_DIM, UNITS}, DT_FLOAT);
  this->assign_w1_wm = Assign(scope, this->w1_wm, ZerosLike(scope, this->w1));
  this->w1_wv = Variable(scope, {NOISE_DIM, UNITS}, DT_FLOAT);
  this->assign_w1_wv = Assign(scope, this->w1_wv, ZerosLike(scope, this->w1));

  // BatchNormalization
  auto mean = Const<float>(scope, {0.0f});
  auto variance = Const<float>(scope, {1.0f});
  auto offset = Const<float>(scope, {0.0f});
  auto scale = Const<float>(scope, {1.0f});
  auto variance_epsilon = Const<float>(scope, {0.001f});
  auto batchnorm = BatchNormalization(scope, dense, mean, variance, offset,
                                      scale, variance_epsilon);
  LOG(INFO) << "Node building status: " << scope.status();

  // LeakyReLU
  auto leakyrelu =
      internal::LeakyRelu(scope, batchnorm, internal::LeakyRelu::Alpha(0.3f));
  LOG(INFO) << "Node building status: " << scope.status();

  // Reshape
  auto reshape1 = Reshape(scope, leakyrelu, {batch_size, 7, 7, 256});
  LOG(INFO) << "Node building status: " << scope.status();

  // Conv2DTranspose 1
  auto input_sizes = Const<int>(scope, {batch_size, 7, 7, 128});
  // filter, aka kernel
  this->filter = Variable(scope, {5, 5, 128, 256}, DT_FLOAT);
  auto random_value1 = GlorotUniform(scope, {5, 5, 128, 256});
  this->assign_filter = Assign(scope, this->filter, random_value1);

  this->filter_wm = Variable(scope, {5, 5, 128, 256}, DT_FLOAT);
  this->assign_filter_wm =
      Assign(scope, this->filter_wm, ZerosLike(scope, this->filter));
  this->filter_wv = Variable(scope, {5, 5, 128, 256}, DT_FLOAT);
  this->assign_filter_wv =
      Assign(scope, this->filter_wv, ZerosLike(scope, this->filter));

  // out_backprop, aka input. here it's reshape1
  auto deconv1 = Conv2DTranspose(scope, input_sizes, filter, reshape1,
                                 {1, 1, 1, 1}, "SAME");
  LOG(INFO) << "Node building status: " << scope.status();

  // BatchNormalization 1, use FusedBatchNorm
  // For inference, need to compute the running mean and variance
  auto mean1 = Const<float>(scope, {});
  auto variance1 = Const<float>(scope, {});
  auto offset1 = BroadcastTo(scope, 0.0f, {128});
  auto scale1 = BroadcastTo(scope, 1.0f, {128});
  auto batchnorm1 = FusedBatchNorm(scope, deconv1, scale1, offset1, mean1,
                                   variance1, FusedBatchNorm::Epsilon(0.001f));
  // auto batchnorm1 = BatchNormalization(scope, deconv1, mean, variance,
  // offset,
  //                                     scale, variance_epsilon);
  LOG(INFO) << "Node building status: " << scope.status();

  // LeakyReLU 1
  auto leakyrelu1 = internal::LeakyRelu(scope, batchnorm1.y,
                                        internal::LeakyRelu::Alpha(0.3f));
  LOG(INFO) << "Node building status: " << scope.status();

  // Conv2DTranspose 2
  auto input_sizes2 = Const(scope, {batch_size, 14, 14, 64});
  // filter, aka kernel
  this->filter2 = Variable(scope, {5, 5, 64, 128}, DT_FLOAT);
  auto random_value2 = GlorotUniform(scope, {5, 5, 64, 128});
  this->assign_filter2 = Assign(scope, filter2, random_value2);

  this->filter2_wm = Variable(scope, {5, 5, 64, 128}, DT_FLOAT);
  this->assign_filter2_wm =
      Assign(scope, this->filter2_wm, ZerosLike(scope, this->filter2));
  this->filter2_wv = Variable(scope, {5, 5, 64, 128}, DT_FLOAT);
  this->assign_filter2_wv =
      Assign(scope, this->filter2_wv, ZerosLike(scope, this->filter2));

  auto deconv2 = Conv2DTranspose(scope, input_sizes2, filter2, leakyrelu1,
                                 {1, 2, 2, 1}, "SAME");
  LOG(INFO) << "Node building status: " << scope.status();

  // BatchNormalization 2, use FusedBatchNorm
  // For inference, need to compute the running mean and variance
  auto offset2 = BroadcastTo(scope, 0.0f, {64});
  auto scale2 = BroadcastTo(scope, 1.0f, {64});
  auto batchnorm2 = FusedBatchNorm(scope, deconv2, scale2, offset2, mean1,
                                   variance1, FusedBatchNorm::Epsilon(0.001f));
  // auto batchnorm2 = BatchNormalization(scope, deconv2, mean, variance,
  // offset,
  //                                     scale, variance_epsilon);
  LOG(INFO) << "Node building status: " << scope.status();

  // LeakyReLU 2
  auto leakyrelu2 = internal::LeakyRelu(scope, batchnorm2.y,
                                        internal::LeakyRelu::Alpha(0.3f));
  LOG(INFO) << "Node building status: " << scope.status();

  // Conv2DTranspose 3
  auto input_sizes3 = Const(scope, {batch_size, 28, 28, NUM_CHANNELS});
  // filter, aka kernel
  this->filter3 = Variable(scope, {5, 5, NUM_CHANNELS, 64}, DT_FLOAT);
  auto random_value3 = GlorotUniform(scope, {5, 5, NUM_CHANNELS, 64});
  this->assign_filter3 = Assign(scope, this->filter3, random_value3);

  this->filter3_wm = Variable(scope, {5, 5, NUM_CHANNELS, 64}, DT_FLOAT);
  this->assign_filter3_wm =
      Assign(scope, this->filter3_wm, ZerosLike(scope, this->filter3));
  this->filter3_wv = Variable(scope, {5, 5, NUM_CHANNELS, 64}, DT_FLOAT);
  this->assign_filter3_wv =
      Assign(scope, this->filter3_wv, ZerosLike(scope, this->filter3));

  this->output = Conv2DTranspose(scope, input_sizes3, filter3, leakyrelu2,
                                 {1, 2, 2, 1}, "SAME");
  LOG(INFO) << "Node building status: " << scope.status();
}

// Discriminator
Discriminator::Discriminator(const ::tensorflow::Scope& scope,
                             const ::tensorflow::Input& inputs,
                             const int batch_size) {
  this->conv1_weights = Variable(scope, {5, 5, NUM_CHANNELS, 64}, DT_FLOAT);
  auto random_value = GlorotUniform(scope, {5, 5, NUM_CHANNELS, 64});
  this->assign_conv1_weights = Assign(scope, this->conv1_weights, random_value);

  this->conv1_biases = Variable(scope, {64}, DT_FLOAT);
  Tensor b_zero_tensor(DT_FLOAT, TensorShape({64}));
  b_zero_tensor.vec<float>().setZero();
  this->assign_conv1_biases = Assign(scope, this->conv1_biases, b_zero_tensor);

  this->conv2_weights = Variable(scope, {5, 5, 64, 128}, DT_FLOAT);
  auto random_value2 = GlorotUniform(scope, {5, 5, 64, 128});
  this->assign_conv2_weights =
      Assign(scope, this->conv2_weights, random_value2);

  this->conv2_biases = Variable(scope, {128}, DT_FLOAT);
  this->assign_conv2_biases = Assign(
      scope, this->conv2_biases, Const<float>(scope, 0.0f, TensorShape({128})));

  int s1 = IMAGE_SIZE;
  s1 = s1 / 4;
  s1 = std::pow(s1, 2) * 128;
  this->fc1_weights = Variable(scope, {s1, 1}, DT_FLOAT);
  auto random_value3 = GlorotUniform(scope, {s1, 1});
  this->assign_fc1_weights = Assign(scope, this->fc1_weights, random_value3);

  this->fc1_biases = Variable(scope, {1}, DT_FLOAT);
  this->assign_fc1_biases = Assign(scope, this->fc1_biases,
                                   Const<float>(scope, 0.0f, TensorShape({1})));

  // Gradient accum parameters start here
  this->conv1_wm = Variable(scope, {5, 5, NUM_CHANNELS, 64}, DT_FLOAT);
  this->assign_conv1_wm =
      Assign(scope, this->conv1_wm, ZerosLike(scope, this->conv1_weights));
  this->conv1_wv = Variable(scope, {5, 5, NUM_CHANNELS, 64}, DT_FLOAT);
  this->assign_conv1_wv =
      Assign(scope, this->conv1_wv, ZerosLike(scope, this->conv1_weights));

  this->conv1_bm = Variable(scope, {64}, DT_FLOAT);
  this->assign_conv1_bm =
      Assign(scope, this->conv1_bm, ZerosLike(scope, this->conv1_biases));
  this->conv1_bv = Variable(scope, {64}, DT_FLOAT);
  this->assign_conv1_bv =
      Assign(scope, this->conv1_bv, ZerosLike(scope, this->conv1_biases));

  this->conv2_wm = Variable(scope, {5, 5, 64, 128}, DT_FLOAT);
  this->assign_conv2_wm =
      Assign(scope, this->conv2_wm, ZerosLike(scope, this->conv2_weights));
  this->conv2_wv = Variable(scope, {5, 5, 64, 128}, DT_FLOAT);
  this->assign_conv2_wv =
      Assign(scope, this->conv2_wv, ZerosLike(scope, this->conv2_weights));

  this->conv2_bm = Variable(scope, {128}, DT_FLOAT);
  this->assign_conv2_bm =
      Assign(scope, conv2_bm, ZerosLike(scope, conv2_biases));
  this->conv2_bv = Variable(scope, {128}, DT_FLOAT);
  this->assign_conv2_bv =
      Assign(scope, conv2_bv, ZerosLike(scope, conv2_biases));

  this->fc1_wm = Variable(scope, {s1, 1}, DT_FLOAT);
  this->assign_fc1_wm =
      Assign(scope, this->fc1_wm, ZerosLike(scope, this->fc1_weights));
  this->fc1_wv = Variable(scope, {s1, 1}, DT_FLOAT);
  this->assign_fc1_wv =
      Assign(scope, this->fc1_wv, ZerosLike(scope, this->fc1_weights));

  this->fc1_bm = Variable(scope, {1}, DT_FLOAT);
  this->assign_fc1_bm =
      Assign(scope, this->fc1_bm, ZerosLike(scope, this->fc1_biases));
  this->fc1_bv = Variable(scope, {1}, DT_FLOAT);
  this->assign_fc1_bv =
      Assign(scope, this->fc1_bv, ZerosLike(scope, this->fc1_biases));

  // Convnet Model begin
  auto conv2d_1 = Conv2D(scope, inputs, this->conv1_weights,
                         gtl::ArraySlice<int>{1, 2, 2, 1}, "SAME");
  LOG(INFO) << "Node building status: " << scope.status();

  auto relu_1 =
      internal::LeakyRelu(scope, BiasAdd(scope, conv2d_1, this->conv1_biases),
                          internal::LeakyRelu::Alpha(0.3f));
  LOG(INFO) << "Node building status: " << scope.status();

  auto dropout_1 = Dropout(scope, relu_1, 0.3f);
  LOG(INFO) << "Node building status: " << scope.status();

  auto conv2d_2 = Conv2D(scope, dropout_1, this->conv2_weights,
                         gtl::ArraySlice<int>{1, 2, 2, 1}, "SAME");
  LOG(INFO) << "Node building status: " << scope.status();

  auto relu_2 =
      internal::LeakyRelu(scope, BiasAdd(scope, conv2d_2, this->conv2_biases),
                          internal::LeakyRelu::Alpha(0.3f));
  LOG(INFO) << "Node building status: " << scope.status();

  auto dropout_2 = Dropout(scope, relu_2, 0.3f);
  LOG(INFO) << "Node building status: " << scope.status();

  // reshape
  auto reshape1 = Reshape(scope, dropout_2, {batch_size, s1});
  LOG(INFO) << "Node building status: " << scope.status();

  // model output
  this->output = BiasAdd(scope, MatMul(scope, reshape1, this->fc1_weights),
                         this->fc1_biases);
  // Convnet Model ends
}

Discriminator::Discriminator(const ::tensorflow::Scope& scope,
                             const Discriminator& disc,
                             const ::tensorflow::Input& inputs,
                             const int batch_size) {
  // Convnet Model begin
  auto conv2d_1 = Conv2D(scope, inputs, disc.conv1_weights,
                         gtl::ArraySlice<int>{1, 2, 2, 1}, "SAME");
  LOG(INFO) << "Node building status: " << scope.status();

  auto relu_1 =
      internal::LeakyRelu(scope, BiasAdd(scope, conv2d_1, disc.conv1_biases),
                          internal::LeakyRelu::Alpha(0.3f));
  LOG(INFO) << "Node building status: " << scope.status();

  auto dropout_1 = Dropout(scope, relu_1, 0.3f);
  LOG(INFO) << "Node building status: " << scope.status();

  auto conv2d_2 = Conv2D(scope, dropout_1, disc.conv2_weights,
                         gtl::ArraySlice<int>{1, 2, 2, 1}, "SAME");
  LOG(INFO) << "Node building status: " << scope.status();

  auto relu_2 =
      internal::LeakyRelu(scope, BiasAdd(scope, conv2d_2, disc.conv2_biases),
                          internal::LeakyRelu::Alpha(0.3f));
  LOG(INFO) << "Node building status: " << scope.status();

  auto dropout_2 = Dropout(scope, relu_2, 0.3f);
  LOG(INFO) << "Node building status: " << scope.status();

  int s1 = IMAGE_SIZE;
  s1 = s1 / 4;
  s1 = std::pow(s1, 2) * 128;

  // reshape
  auto reshape1 = Reshape(scope, dropout_2, {batch_size, s1});
  LOG(INFO) << "Node building status: " << scope.status();

  // model output
  this->output = BiasAdd(scope, MatMul(scope, reshape1, disc.fc1_weights),
                         disc.fc1_biases);
  // Convnet Model ends
}

}  // namespace ops
}  // namespace tensorflow
