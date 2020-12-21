/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/compiler/xla/service/gpu/gpu_compiler.h"
#include "tensorflow/compiler/xla/service/gpu/gpu_device_info.h"
#include "tensorflow/compiler/xla/service/gpu/target_constants.h"
#include "tensorflow/compiler/xla/service/hlo_module.h"
#include "tensorflow/compiler/xla/status.h"
#include "tensorflow/compiler/xla/tools/hlo_module_loader.h"
#include "tensorflow/core/platform/init_main.h"
#include "tensorflow/core/platform/logging.h"
#include "tensorflow/core/util/command_line_flags.h"

const char* const kUsage = R"(
This tool reads in an HloMoudle from a file, compiles it using the NVPTX
compiler and prints out the LLVM IR generated by the IR emitter.  The LLVM IR is
not optimized by the LLVM pass pipeline, so this tool can be used to unit test
the XLA GPU IR emitters.

Note that the LLVM IR does not contain the *full* module, but only parts that
will be code generated into PTX.  The NVPTX compiler also generates a
GpuExecutable on the size that is not printed.)";

namespace {
xla::Status CompileAndPrintLlvmIr(const std::string& hlo_text) {
  TF_ASSIGN_OR_RETURN(
      std::unique_ptr<xla::HloModule> hlo_module,
      xla::LoadModuleFromData(/*data=*/hlo_text, /*format=*/"hlo"));
  llvm::LLVMContext llvm_context;

  // For now we pretend we're compiling for V100.  This can be generalized
  // later.

  xla::gpu::GpuDeviceInfo gpu_device_info{};
  gpu_device_info.threads_per_block_limit = 1024;
  gpu_device_info.threads_per_warp = 32;
  gpu_device_info.shared_memory_per_block = 49152;
  gpu_device_info.core_count = 80;
  gpu_device_info.threads_per_core_limit = 2048;

  xla::gpu::CudaComputeCapability cuda_compute_capability;
  cuda_compute_capability.cc_major = 7;
  cuda_compute_capability.cc_minor = 0;
  std::string target_triple = "nvptx64-nvidia-cuda";
  std::string datalayout = "nvptx64-nvidia-cuda";
  TF_ASSIGN_OR_RETURN(std::unique_ptr<llvm::Module> llvm_module,
                      xla::gpu::CompileModuleToLlvmIr(
                          hlo_module.get(), &llvm_context,
                          /*target_triple=*/xla::gpu::nvptx::kTargetTriple,
                          /*data_layout=*/xla::gpu::nvptx::kDataLayout,
                          /*platform_name=*/"CUDA", gpu_device_info,
                          cuda_compute_capability, /*pointer_size=*/8));

  llvm_module->print(llvm::outs(), nullptr);
  return xla::Status::OK();
}

xla::Status CompileAndPrintLlvmIrFromFile(const std::string& file_name) {
  std::string full_text;
  TF_RETURN_IF_ERROR(tensorflow::ReadFileToString(tensorflow::Env::Default(),
                                                  file_name, &full_text));

  std::vector<std::string> hlo_module_texts =
      absl::StrSplit(full_text, "// -----");
  for (const std::string& hlo_module_text : hlo_module_texts) {
    TF_RETURN_IF_ERROR(CompileAndPrintLlvmIr(hlo_module_text));
  }

  return xla::Status::OK();
}
}  // namespace

int main(int argc, char** argv) {
  std::vector<tensorflow::Flag> flag_list;
  xla::AppendDebugOptionsFlags(&flag_list);
  // The usage string includes the message at the top of the file, the
  // DebugOptions flags and the flags defined above.
  const std::string kUsageString = absl::StrCat(
      kUsage, "\n\n", tensorflow::Flags::Usage(argv[0], flag_list));
  bool parse_ok = tensorflow::Flags::Parse(&argc, argv, flag_list);
  tensorflow::port::InitMain(kUsageString.c_str(), &argc, &argv);
  if (!parse_ok) {
    LOG(QFATAL) << kUsageString;
  }

  QCHECK(argc == 2) << "Must specify a single input file";
  TF_CHECK_OK(CompileAndPrintLlvmIrFromFile(argv[1]));

  return 0;
}
