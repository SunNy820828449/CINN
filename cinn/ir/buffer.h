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

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "cinn/common/common.h"
#include "cinn/ir/ir.h"

namespace cinn {
namespace ir {

class _Buffer_;
class Tensor;
class _Tensor_;

//! The memory access mode.
enum class AccessMask : int {
  kRead = 1,
  kWrite,
};

//! Get its buffer's name given a tensor.
std::string TensorGetBufferName(const _Tensor_* tensor);
//! Get its tensor's name given a buffer.
std::string BufferGetTensorName(const _Buffer_* buffer);

/**
 * Buffer is a symbolic multi-dimensional data structure, it is a node in IR.
 * It is a composition of primitive symbolic types, used to specify the memory layout of the Tensor used in the program
 * input. User can create a buffer and bind to multiple Tensors to specify that the tensors are not inlined and persist
 * data to this buffer.
 */
class Buffer : public IrNodeRef {
 public:
  Buffer() = default;
  explicit Buffer(IrNode* n) : IrNodeRef(n) {}
  operator Expr() const { return Expr(get()); }

  //! Some expressions on operating the buffer.
  //! All the IR-wise operations are collected below.
  // TODO(Superjom) Abandon them.
  // @{
  //! Expression to destroy the buffer.
  Expr DestroyExpr() const;
  // @}

  const _Buffer_* operator->() const;
  _Buffer_* operator->();
};

class _Buffer_ : public ExprNode<_Buffer_> {
 public:
  //! The shape of the buffer.
  std::vector<Expr> shape;
  //! The strides of each dimension.
  // This can be empty, indicating that the array is contiguous.
  std::vector<Expr> strides;
  //! The name of the buffer.
  std::string name;
  //! The storage scope of the buffer, empty if global.
  std::string scope;
  //! The offset in terms of number of dtype elements (including lanes).
  Expr elem_offset;
  //! Factor of elem_offset field.
  // elem_offset is guaranteed to be multiple of offset_factor.
  int offset_factor{0};
  //! The place the buffer locates.
  Target target{UnkTarget()};
  //! Aignment requirement of data pointer in bytes.
  mutable int data_alignment{0};
  //! The memory type of the buffer.
  MemoryType memory_type{MemoryType::Heap};

  //! The data type of the elements.
  //! This is different from `type`, a buffer's type should always be `cinn_buffer_t*`.
  Type dtype;

  _Buffer_() : elem_offset(Expr(0)) { set_type(type_of<cinn_buffer_t*>()); }

  static Buffer Make(Var data,
                     Type dtype,
                     const std::vector<Expr>& shape,
                     const std::vector<Expr>& strides,
                     Expr elem_offset,
                     const std::string& name,
                     const std::string& scope,
                     int data_alignment,
                     int offset_factor,
                     Target target = UnkTarget());

  static Buffer Make(const std::string& name, const std::vector<Expr>& shape = {});

  static Buffer Make(const std::string& name, Type type) {
    CHECK(!type.is_void());
    CHECK(!type.is_unk());
    auto n   = make_shared<_Buffer_>();
    n->name  = name;
    n->dtype = type;
    return Buffer(n);
  }

  //! Make an empty buffer.
  static Buffer Make();

  bool is_on_gpu() const { return memory_type == MemoryType::GPULocal || memory_type == MemoryType::GPUShared; }
  bool is_on_host() const { return !is_on_gpu(); }

  void BindTo(const Tensor& tensor);
  void BindTo(const _Tensor_* tensor);
  void Unbind(const _Tensor_* tensor);

  const std::set<std::string>& binded_tensor_names() const { return binded_tensors_names_; }

  Var buffer_addr() const;

  IrNodeTy node_type() const override;

  void Verify() const override;

  static const IrNodeTy _node_type_ = IrNodeTy::_Buffer_;

  // Copy the meta infos to other.
  void CopyMeta(_Buffer_* other) const { other->binded_tensors_names_ = binded_tensors_names_; }

 private:
  std::set<std::string> binded_tensors_names_;
};

static bool operator<(const ir::Buffer& a, const ir::Buffer& b) { return a->name < b->name; }

}  // namespace ir
}  // namespace cinn
