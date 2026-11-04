# ACIRCore: generated type/attribute definitions plus the analysis-free
# grammar and contract TUs below the operations layer (B2 step-2 design
# r9 §1). GFIRDialect links exactly this plus the shared interfaces.
add_acir_library(ACIRCore SOURCES
  ACIRCoreDefs.cpp
  ACIRPacketTypes.cpp
  ACIRIdentityContracts.cpp
  ACIRStaticContracts.cpp
  SourceAttributeContracts.cpp
  SourceSyntaxSiteContracts.cpp
  SourceValueSemantics.cpp
  ACIRStaticEvaluation.cpp
  SourceStaticRecordConstructors.cpp
  SourceStaticDomains.cpp
  SourceStaticValueContracts.cpp
  ACIRSourceContracts.cpp
  "${PYCIRCUIT_SOURCE_DIR}/lib/Compiler/SourceIdentifier.cpp"
  "${PYCIRCUIT_SOURCE_DIR}/lib/IR/StateDeclarationContracts.cpp"
)
add_dependencies(ACIRCore
  ACIRDialectIncGen
  ACIRAttributesIncGen
  ACIRTypesIncGen
)
target_link_libraries(ACIRCore PUBLIC MLIRFuncDialect)
target_include_directories(ACIRCore PUBLIC
  "$<BUILD_INTERFACE:${PYCIRCUIT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${PYCIRCUIT_BINARY_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
target_include_directories(ACIRCore PRIVATE
  "${PYCIRCUIT_SOURCE_DIR}/lib"
)

# The staged common analysis layer (B2 step-2 design r9 §5): every
# HardwareAnalysis-based TU. ACIRDialect (operations) links ACIRCore and
# stays BELOW this target; nothing in ACIRAnalysis may be referenced from
# ACIRDialect or ACIRCore (enforced by pycircuit_verify_layering and the
# ops/core probes).
add_acir_library(ACIRAnalysis SOURCES
  HardwareAnalysis.cpp
  HardwareAnalysisDependencies.cpp
  HardwareAnalysisValidation.cpp
  HardwareDependencySerialization.cpp
  HardwarePhysicalProjection.cpp
  HardwarePhysicalState.cpp
  HardwareQueueResources.cpp
  HardwareSampledAdmission.cpp
  HardwareSourceCheckPreflight.cpp
  HardwareSourceChecks.cpp
  HardwareValueAvailability.cpp
  HardwareWorkPartition.cpp
  PackageVerification.cpp
  SourceHelperAnalysis.cpp
  SourceMethodAnalysis.cpp
  SourceMethodResolution.cpp
  SourceTransactionStateAnalysis.cpp
  SourceTransactionSummary.cpp
  TransactionalContracts.cpp
  EntryBoundary.cpp
  ACIRTransactionOps.cpp
  ACIRModuleOps.cpp
  ACIRModuleContracts.cpp
  ACIRQueueOps.cpp
  ACIRSourceControlFlowOps.cpp
  ACIRSystemOps.cpp
  ACIRPacketOps.cpp
  ACIRBitsOps.cpp
  CollectionAnalysis.cpp
  EnumAnalysis.cpp
  BitsAnalysis.cpp
  SourceUnitValidation.cpp
  FinalSourceUnitValidation.cpp
  SourceNamespaceValidation.cpp
  "${PYCIRCUIT_SOURCE_DIR}/lib/Analysis/LogicalStateValidation.cpp"
)
add_dependencies(ACIRAnalysis
  ACIRDialectIncGen
  ACIRAttributesIncGen
  ACIRTypesIncGen
  ACIROpsIncGen
)
target_link_libraries(ACIRAnalysis PUBLIC
  ACIRDialect
  PycircuitStateInterfaces
)
target_include_directories(ACIRAnalysis PRIVATE
  "${PYCIRCUIT_SOURCE_DIR}/lib"
)

add_acir_library(ACIRDialect SOURCES
  CollectionLocalVerifies.cpp
  ACIREnumOps.cpp
  ACIRConstantOps.cpp
  ACIRDialect.cpp
  ACIRMathOps.cpp
  ACIRNumericProofOps.cpp
  ACIRRegistration.cpp
  SourceEffects.cpp
  SourceRegistrationEntry.cpp
  HardwareGrantProof.cpp
  "${PYCIRCUIT_SOURCE_DIR}/lib/Compiler/SourceIdentifier.cpp"
  "${PYCIRCUIT_SOURCE_DIR}/lib/Analysis/LogicalStateValidation.cpp"
  "${PYCIRCUIT_SOURCE_DIR}/lib/IR/StateDeclarationContracts.cpp"
)
add_dependencies(ACIRDialect
  ACIRDialectIncGen
  ACIRAttributesIncGen
  ACIRTypesIncGen
  ACIROpsIncGen
)
target_link_libraries(ACIRDialect PUBLIC
  ACIRCore
  PycircuitStateInterfaces
  LLVMCore
  MLIRArithDialect
  MLIRFuncDialect
  MLIRSCFDialect
  MLIRSideEffectInterfaces
)
target_include_directories(ACIRDialect PUBLIC
  "$<BUILD_INTERFACE:${PYCIRCUIT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${PYCIRCUIT_BINARY_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
target_include_directories(ACIRDialect PRIVATE
  "${PYCIRCUIT_SOURCE_DIR}/lib"
)
