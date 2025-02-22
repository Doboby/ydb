
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(lib-Target-X86)
target_compile_options(lib-Target-X86 PRIVATE
  -Wno-everything
)
target_include_directories(lib-Target-X86 PRIVATE
  ${CMAKE_BINARY_DIR}/contrib/libs/llvm12/lib/Target/X86
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86
)
target_link_libraries(lib-Target-X86 PUBLIC
  contrib-libs-cxxsupp
  contrib-libs-llvm12
  libs-llvm12-include
  llvm12-lib-Analysis
  llvm12-lib-CodeGen
  lib-CodeGen-AsmPrinter
  lib-CodeGen-GlobalISel
  lib-CodeGen-SelectionDAG
  llvm12-lib-IR
  llvm12-lib-MC
  llvm12-lib-ProfileData
  llvm12-lib-Support
  llvm12-lib-Target
  Target-X86-MCTargetDesc
  Target-X86-TargetInfo
  lib-Transforms-CFGuard
)
target_sources(lib-Target-X86 PRIVATE
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86AsmPrinter.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86AvoidStoreForwardingBlocks.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86AvoidTrailingCall.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86CallFrameOptimization.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86CallLowering.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86CallingConv.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86CmovConversion.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86DiscriminateMemOps.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86DomainReassignment.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86EvexToVex.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86ExpandPseudo.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86FastISel.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86FixupBWInsts.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86FixupLEAs.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86FixupSetCC.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86FlagsCopyLowering.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86FloatingPoint.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86FrameLowering.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86ISelDAGToDAG.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86ISelLowering.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86IndirectBranchTracking.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86IndirectThunks.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86InsertPrefetch.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86InsertWait.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86InstCombineIntrinsic.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86InstrFMA3Info.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86InstrFoldTables.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86InstrInfo.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86InstructionSelector.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86InterleavedAccess.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86LegalizerInfo.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86LoadValueInjectionLoadHardening.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86LoadValueInjectionRetHardening.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86LowerAMXType.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86MCInstLower.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86MachineFunctionInfo.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86MacroFusion.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86OptimizeLEAs.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86PadShortFunction.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86PartialReduction.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86PreTileConfig.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86RegisterBankInfo.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86RegisterInfo.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86SelectionDAGInfo.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86ShuffleDecodeConstantPool.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86SpeculativeExecutionSideEffectSuppression.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86SpeculativeLoadHardening.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86Subtarget.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86TargetMachine.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86TargetObjectFile.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86TargetTransformInfo.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86TileConfig.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86VZeroUpper.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86WinAllocaExpander.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/Target/X86/X86WinEHState.cpp
)
