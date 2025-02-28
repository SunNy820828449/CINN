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

TEST(Decomposer, sum) {
  NetBuilder builder("sum");
  auto x   = builder.CreateInput(Float(32), {32, 16});
  auto y   = builder.CreateInput(Float(32), {32, 16});
  auto z   = builder.CreateInput(Float(32), {32, 16});
  auto out = builder.sum({x, y, z});

  auto sum_cpu = [](const std::vector<size_t>& lengths, const std::vector<void*>& ptrs) {
    size_t n   = lengths[0];
    float* x   = static_cast<float*>(ptrs[0]);
    float* y   = static_cast<float*>(ptrs[1]);
    float* z   = static_cast<float*>(ptrs[2]);
    float* out = static_cast<float*>(ptrs[3]);
    for (size_t i = 0; i < n; ++i) {
      out[i] = x[i] + y[i] + z[i];
    }
  };

  std::vector<std::string> input_names  = {x.id().data(), y.id().data(), z.id().data()};
  std::vector<std::string> output_names = {out->id};
  RunAndCheck<float>(builder, input_names, output_names, sum_cpu);
}

}  // namespace cinn::frontend
