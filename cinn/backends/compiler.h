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

#pragma once

#include <absl/strings/string_view.h>

#include <memory>
#include <string>

#include "cinn/backends/llvm/codegen_llvm.h"
#include "cinn/backends/llvm/execution_engine.h"
#include "cinn/backends/llvm/simple_jit.h"
#include "cinn/lang/packed_func.h"
#ifdef CINN_WITH_CUDA
#include "cinn/runtime/cuda/cuda_module.h"
#endif

namespace cinn {
namespace backends {

class Compiler final {
 public:
  static std::unique_ptr<Compiler> Create(const Target& target) {
    return std::unique_ptr<Compiler>(new Compiler(target));
  }

  /**
   * Compile and link to a CINN module.
   */
  void Build(const ir::Module& module, const std::string& code = "");

  std::string GetSourceCode(const ir::Module& module);

  void BuildDefault(const ir::Module& module);

  /**
   * Retrieve a function by \p fn_name.
   * @return function address or null if not exists.
   */
  lower_func_ptr_t Lookup(absl::string_view fn_name);

 private:
  void CompileCudaModule(const ir::Module& module, const std::string& code = "");

  void CompileX86Module(const ir::Module& module);

  explicit Compiler(const Target& target) : target_(target), engine_(ExecutionEngine::Create(ExecutionOptions())) {}

  CINN_DISALLOW_COPY_AND_ASSIGN(Compiler);

 private:
  Target target_;
  std::unique_ptr<ExecutionEngine> engine_;

#ifdef CINN_WITH_CUDA
  std::unique_ptr<runtime::cuda::CUDAModule> cuda_module_;
#endif
};

}  // namespace backends
}  // namespace cinn
