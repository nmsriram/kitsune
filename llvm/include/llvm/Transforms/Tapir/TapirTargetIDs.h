//===- TapirTargetIDs.h - Tapir target ID's --------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file enumerates the available Tapir lowering targets.
//
//===----------------------------------------------------------------------===//

#ifndef TAPIR_TARGET_IDS_H_
#define TAPIR_TARGET_IDS_H_

namespace llvm {

enum class TapirTargetID {
  Off,      // Completely disabled (i.e., no -ftapir argument was present). 
  None,     // Perform no lowering
  Serial,   // Lower to serial projection
  Cheetah,  // Lower to the Cheetah ABI
  Cilk,     // Lower to the Cilk Plus ABI
  CilkR,    // Lower to the CilkR ABI
  Cuda,     // Lower to Cuda ABI
  KitCuda,  // Lower to Kitsune's Cuda ABI 
  OpenCilk, // Lower to OpenCilk ABI
  OpenMP,   // Lower to OpenMP
  Qthreads, // Lower to Qthreads
  Realm,    // Lower to Realm
  OpenCL,   // Lower to OpenCL 
  Last_TapirTargetID
};

} // end namespace llvm

#endif
