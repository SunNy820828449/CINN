// Copyright (c) 2021 CINN Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cinn/frontend/decomposer/test_helper.h"

namespace cinn::frontend {

TEST(Decomposer, elementwise_add_bcast0) {
  NetBuilder builder("elementwise_add");
  auto x   = builder.CreateInput(Float(32), {4, 1, 20, 10});
  auto y   = builder.CreateInput(Float(32), {10, 20});
  auto out = builder.elementwise_add(x, y, 1);

  auto prog     = builder.Build();
  Target target = GetTarget();
  RunDecomposer(&prog, target);
}

TEST(Decomposer, elementwise_add_grad_bcast0) {
  NetBuilder builder("elementwise_add_grad");
  auto dout      = builder.CreateInput(Float(32), {4, 10, 20, 10});
  auto x         = builder.CreateInput(Float(32), {4, 1, 20, 10});
  auto y         = builder.CreateInput(Float(32), {10, 20});
  auto out_grads = builder.elementwise_add_grad(dout, x, y, 1);

  auto prog     = builder.Build();
  Target target = GetTarget();
  RunDecomposer(&prog, target);
}

TEST(Decomposer, elementwise_add_bcast1) {
  NetBuilder builder("elementwise_add");
  auto x   = builder.CreateInput(Float(32), {4, 1, 20, 1});
  auto y   = builder.CreateInput(Float(32), {10, 1, 10});
  auto out = builder.elementwise_add(x, y, 1);

  auto prog     = builder.Build();
  Target target = GetTarget();
  RunDecomposer(&prog, target);
}

TEST(Decomposer, elementwise_add_grad_bcast1) {
  NetBuilder builder("elementwise_add_grad");
  auto dout      = builder.CreateInput(Float(32), {4, 10, 20, 10});
  auto x         = builder.CreateInput(Float(32), {4, 1, 20, 1});
  auto y         = builder.CreateInput(Float(32), {10, 1, 10});
  auto out_grads = builder.elementwise_add_grad(dout, x, y, 1);

  auto prog     = builder.Build();
  Target target = GetTarget();
  RunDecomposer(&prog, target);
}

TEST(Decomposer, elementwise_add_bcast2) {
  NetBuilder builder("elementwise_add");
  auto x   = builder.CreateInput(Float(32), {32, 16});
  auto y   = builder.CreateInput(Float(32), {1});
  auto out = builder.elementwise_add(x, y);

  auto add_cpu = [](const std::vector<size_t>& lengths, const std::vector<void*>& ptrs) {
    size_t n     = lengths[0];
    float* x     = static_cast<float*>(ptrs[0]);
    float* y     = static_cast<float*>(ptrs[1]);
    float* out   = static_cast<float*>(ptrs[2]);
    float y_data = y[0];
    for (size_t i = 0; i < n; ++i) {
      out[i] = x[i] + y_data;
    }
  };

  std::vector<std::string> input_names  = {x.id().data(), y.id().data()};
  std::vector<std::string> output_names = {out->id};
  RunAndCheck<float>(builder, input_names, output_names, add_cpu);
}

TEST(Decomposer, elementwise_add_grad_bcast2) {
  NetBuilder builder("elementwise_add_grad");
  auto dout      = builder.CreateInput(Float(32), {32, 16});
  auto x         = builder.CreateInput(Float(32), {32, 16});
  auto y         = builder.CreateInput(Float(32), {1});
  auto out_grads = builder.elementwise_add_grad(dout, x, y);

  auto add_grad_cpu = [](const std::vector<size_t>& lengths, const std::vector<void*>& ptrs) {
    size_t n    = lengths[0];
    float* dout = static_cast<float*>(ptrs[0]);
    float* dx   = static_cast<float*>(ptrs[1]);
    float* dy   = static_cast<float*>(ptrs[2]);
    for (size_t i = 0; i < n; ++i) {
      float tmp = dout[i];
      dx[i]     = tmp;
      dy[0] += tmp;
    }
  };

  std::vector<std::string> input_names  = {dout.id().data()};
  std::vector<std::string> output_names = {out_grads[0]->id, out_grads[1]->id};
  RunAndCheck<float>(builder, input_names, output_names, add_grad_cpu);
}

TEST(Decomposer, elementwise_add_same_dims) {
  NetBuilder builder("elementwise_add");
  auto x   = builder.CreateInput(Float(32), {32, 16});
  auto y   = builder.CreateInput(Float(32), {32, 16});
  auto out = builder.elementwise_add(x, y);

  auto add_cpu = [](const std::vector<size_t>& lengths, const std::vector<void*>& ptrs) {
    size_t n   = lengths[0];
    float* x   = static_cast<float*>(ptrs[0]);
    float* y   = static_cast<float*>(ptrs[1]);
    float* out = static_cast<float*>(ptrs[2]);
    for (size_t i = 0; i < n; ++i) {
      out[i] = x[i] + y[i];
    }
  };

  std::vector<std::string> input_names  = {x.id().data(), y.id().data()};
  std::vector<std::string> output_names = {out->id};
  RunAndCheck<float>(builder, input_names, output_names, add_cpu);
}

TEST(Decomposer, elementwise_add_grad_same_dims) {
  NetBuilder builder("elementwise_add_grad");
  auto dout      = builder.CreateInput(Float(32), {32, 16});
  auto x         = builder.CreateInput(Float(32), {32, 16});
  auto y         = builder.CreateInput(Float(32), {32, 16});
  auto out_grads = builder.elementwise_add_grad(dout, x, y);

  auto add_grad_cpu = [](const std::vector<size_t>& lengths, const std::vector<void*>& ptrs) {
    size_t n    = lengths[0];
    float* dout = static_cast<float*>(ptrs[0]);
    float* dx   = static_cast<float*>(ptrs[1]);
    float* dy   = static_cast<float*>(ptrs[2]);
    for (size_t i = 0; i < n; ++i) {
      float tmp = dout[i];
      dx[i]     = tmp;
      dy[i]     = tmp;
    }
  };

  std::vector<std::string> input_names  = {dout.id().data()};
  std::vector<std::string> output_names = {out_grads[0]->id, out_grads[1]->id};
  RunAndCheck<float>(builder, input_names, output_names, add_grad_cpu);
}

}  // namespace cinn::frontend
