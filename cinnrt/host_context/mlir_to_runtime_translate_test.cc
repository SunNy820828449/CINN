#include "cinnrt/host_context/mlir_to_runtime_translate.h"

#include <gtest/gtest.h>
#include <llvm/Support/FormatVariadic.h>

#include "cinn/utils/string.h"
#include "cinnrt/common/global.h"
#include "cinnrt/dialect/mlir_loader.h"
#include "cinnrt/host_context/core_runtime.h"
#include "cinnrt/host_context/kernel_registry.h"
#include "cinnrt/host_context/kernel_utils.h"
#include "cinnrt/host_context/mlir_program_executor.h"
#include "cinnrt/kernel/basic_kernels.h"
#include "cinnrt/kernel/control_flow_kernels.h"
#include "cinnrt/kernel/tensor_kernels.h"
#include "cinnrt/kernel/tensor_shape_kernels.h"
#include "cinnrt/kernel/test_kernels.h"

namespace cinnrt::host_context {

TEST(MlirToRuntimeTranslate, basic) {
  mlir::MLIRContext context;

  auto source = R"ROC(
func @main() -> () {
  %v0 = cinn.constant.f32 1.0
  %v1 = cinn.constant.f32 2.0
  %v2 = "cinn.add.f32"(%v0, %v1) : (f32, f32) -> f32
  %v3 = "cinn.mul.f32"(%v2, %v1) : (f32, f32) -> f32

  "cinn.print.f32"(%v1) : (f32) -> ()

  cinn.return
}
)ROC";

  auto module = dialect::LoadMlirSource(&context, source);
  module->verify();

  KernelRegistry registry;
  kernel::RegisterFloatBasicKernels(&registry);
  kernel::RegisterIntBasicKernels(&registry);

  TestMlir(module.get(), &registry);
}

TEST(TestMlir, basic) {
  mlir::MLIRContext context;

  auto source = R"ROC(
func @main() -> () {
  %v0 = cinn.constant.f32 1.0
  %v1 = cinn.constant.f32 2.0
  %v2 = "cinn.add.f32"(%v0, %v1) : (f32, f32) -> f32
  %v3 = "cinn.mul.f32"(%v2, %v1) : (f32, f32) -> f32

  "cinn.print.f32"(%v1) : (f32) -> ()

  cinn.return
}
)ROC";

  auto module = dialect::LoadMlirSource(&context, source);
  module->verify();

  KernelRegistry registry;
  kernel::RegisterFloatBasicKernels(&registry);
  kernel::RegisterIntBasicKernels(&registry);

  TestMlir(module.get(), &registry);
}

TEST(TestMlir, shadow_copy_tensor_profile) {
  mlir::MLIRContext* context = cinnrt::Global::getMLIRContext();

  auto head = R"ROC(
func @predict(%a: !cinn.tensor<X86, NCHW, F32>, %b: !cinn.tensor<X86, NCHW, F32>) -> (!cinn.tensor<X86, NCHW, F32>, !cinn.tensor<X86, NCHW, F32>) {
)ROC";

  auto tpl0 = "%a{0} = dt.shallow_copy_tensor %a : !cinn.tensor<X86, NCHW, F32> -> !cinn.tensor<X86, NCHW, F32>";
  auto tpl1 = "%b{0} = dt.shallow_copy_tensor %b : !cinn.tensor<X86, NCHW, F32> -> !cinn.tensor<X86, NCHW, F32>";

  auto end = R"ROC(
cinn.return %a0, %b0: !cinn.tensor<X86, NCHW, F32>, !cinn.tensor<X86, NCHW, F32>
}
  )ROC";

  std::stringstream ss;
  ss << head;
  for (int i = 0; i < 2000; i++) {
    ss << llvm::formatv(tpl0, i).str() << "\n";
    ss << llvm::formatv(tpl1, i).str() << "\n";
  }
  ss << end;

  auto content = ss.str();

  // LOG(INFO) << "content: " << content << std::endl;

  auto module = dialect::LoadMlirSource(context, content);
  module->verify();

  host_context::KernelRegistry registry;

  kernel::RegisterBasicKernels(&registry);
  kernel::RegisterTestKernels(&registry);
  kernel::RegisterTensorShapeKernels(&registry);
  kernel::RegisterTensorKernels(&registry);
  kernel::RegisterControlFlowKernels(&registry);

  MlirProgramExecutor executor(*module, &registry);
  executor.BuildFunctions();

  auto* func = executor.LookupFunc("predict");
  ASSERT_TRUE(func);

  std::vector<Value*> in_args;
  std::vector<ValueRef> out_args(
      {ValueRef(new Value(tensor::DenseHostTensor())), ValueRef(new Value(tensor::DenseHostTensor()))});

  auto create_tensor = [] {
    tensor::DenseHostTensor a(tensor::TensorShape{{200, 3000}}, DType(DType::Kind::F32));
    auto* data = reinterpret_cast<float*>(a.raw_data());
    for (int i = 0; i < a.shape().GetNumElements(); i++) {
      data[i] = i;
    }
    return a;
  };

  std::vector<ValueRef> inputs({ValueRef(new Value(create_tensor())), ValueRef(new Value(create_tensor()))});
  in_args.assign({inputs[0].get(), inputs[1].get()});

  for (int i = 0; i < 500; i++) {
    func->Execute(llvm::ArrayRef<Value*>(in_args.data(), in_args.size()),
                  llvm::MutableArrayRef<ValueRef>(out_args.data(), out_args.size()));
  }
}

}  // namespace cinnrt::host_context
