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

#include "cinn/backends/extern_func_emitter.h"

#include <absl/hash/hash.h>
#include <glog/raw_logging.h>

#include <functional>
#include <iostream>
#include <string>

#include "cinn/backends/extern_func_emitter_builtin.h"
#include "cinn/backends/llvm/runtime_symbol_registry.h"
#include "cinn/runtime/cpu/host_intrinsics.h"
#include "cinn/utils/string.h"

namespace cinn {
namespace backends {

ExternFunctionEmitterRegistry& ExternFunctionEmitterRegistry::Global() {
  static ExternFunctionEmitterRegistry x;
  return x;
}

void ExternFunctionEmitterRegistry::Register(const ExternFuncID& name, ExternFunctionEmitter* x) {
#ifdef CINN_WITH_DEBUG
  RAW_LOG_INFO("Register extern function emitter [%s]", utils::GetStreamCnt(name).c_str());
#endif  // CINN_WITH_DEBUG
  CHECK(x);
  data_[name] = std::unique_ptr<ExternFunctionEmitter>(x);
}

ExternFunctionEmitter* ExternFunctionEmitterRegistry::Lookup(const ExternFuncID& name) const {
  auto it = data_.find(name);
  if (it != data_.end()) {
    return it->second.get();
  }
  return nullptr;
}

std::ostream& operator<<(std::ostream& os, const ExternFuncID& x) {
  os << x.name << ":" << x.backend_id;
  return os;
}

ExternFunctionEmitterRegistry::ExternFunctionEmitterRegistry() {}

const FunctionProto& ExternFunctionEmitter::func_proto() const {
  auto* proto = ExternFunctionProtoRegistry::Global().Lookup(func_name());
  CHECK(proto) << "No prototype of function [" << func_name() << "]";
  return *proto;
}

}  // namespace backends
}  // namespace cinn

namespace std {

size_t hash<cinn::backends::ExternFuncID>::operator()(const cinn::backends::ExternFuncID& x) const {
  return absl::Hash<absl::string_view>{}(x.name) ^ absl::Hash<absl::string_view>{}(x.backend_id);
}

}  // namespace std
