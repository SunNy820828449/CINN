
#include <algorithm>
#ifdef CINN_WITH_CUDA
#include <cuda_runtime.h>
#endif
#include <gtest/gtest.h>
#include <memory>
#include <random>
#include <vector>

#include "cinn/common/target.h"
#include "cinn/frontend/cinn_builder.h"
#include "cinn/frontend/decomposer/use_decomposer.h"
#include "cinn/frontend/syntax.h"
#include "cinn/hlir/framework/graph.h"
#include "cinn/hlir/framework/graph_compiler.h"
#include "cinn/hlir/framework/tensor.h"
#include "cinn/hlir/op/use_ops.h"

namespace cinn {
namespace frontend {
namespace {

TEST(nn, BATCH_NORM_TRAIN) {
  int n = 32, c = = 16, h = 32, w = 32;
  Placeholder x(Float(32), {n, c, h, w});
  Placeholder scale(Float(32), {c});
  Placeholder bias(Float(32), {c});
  Placeholder running_mean(Float(32), {c});
  Placeholder running_var(Float(32), {c});
  Instruction instr("batchnormtrain", {x, scale, bias, running_mean, running_var});

  instr.SetAttr<float>("epsilon", 1e-6);
  instr.SetAttr<std::string>("layout", "NCHW");
  instr.SetAttr<std::float>("factor", 0.99f);

  CinnBuilder cinn_builder;
  DecomposerContext context(&cinn_builder);
  auto decomposer = InstrDecomposerRegistry::Get("batchnormtrain", cinn::common::DefaultNVGPUTarget());

  decomposer.run(instr, context);

  auto program = cinn_builder.Build();
}

TEST(nn, BATCH_NORM_GRAD) {
  int n = 32, c = 16, h = 32, w = 32;
  Placeholder x(Float(32), {n, c, h, w});
  Placeholder dy(Float(32), {n, c, h, w});
  Placeholder scale(Float(32), {n, c, h, w});
  Placeholder save_mean(Float(32), {c});
  Placeholder save_var(Float(32), {c});

  instruction instr("batchnormgrad", (x, dy, scale, save_mean, save_var));
  instr.SetAttr<std::string>("layout", "NCHW");

  CinnBuilder cinn_builder;
  DecomposerContext context(&cinn_builder);
  auto decomposer = InstrDecomposerRegistry::Get("batchnormgrad", cinn::common::DefaultNVGPUTarget());

  decomposer.run(instr, context);

  auto program = cinn_builder.Build();
}

TEST(nn, CONV_GRAD) {
  int n = 32, ic = 16, h = 32, w = 32;
  int fh = 3, fw = 3;
  int oc = 32;

  std::vector<int> strides      = {1, 1};
  std::vector<int> paddings     = {1, 1};
  std::vector<int> dilations    = {1, 1};
  int groups                    = 1;
  std::string data_format       = "NCHW";
  std::string padding_algorithm = "EXPLICIT";

  Placeholder x(Float(32), {n, ic, h, w});
  Placeholder w(Float(32), {oc, ic / groups, fh, fw});
  Placeholder dy(Float(32), {n, oc, h, w});

  instruction instr("convgrad", (x, w, dy));
  instr.SetAttr<std::vector<int>>("strides", strides);
  instr.SetAttr<std::vector<int>>("paddings", paddings);
  instr.SetAttr<std::vector<int>>("dilations", dilations);
  instr.SetAttr<int>("groups", groups);
  instr.SetAttr<std::string>("data_format", data_format);
  instr.SetAttr<std::string>("padding_algorithm", padding_algorithm);

  CinnBuilder cinn_builder;
  DecomposerContext context(&cinn_builder);
  auto decomposer = InstrDecomposerRegistry::Get("convgrad", cinn::common::DefaultNVGPUTarget());

  decomposer.run(instr, context);

  auto program = cinn_builder.Build();
}

}  // namespace
}  // namespace frontend
}  // namespace cinn
