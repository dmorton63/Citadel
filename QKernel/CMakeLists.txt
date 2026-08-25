add_library(QKernel STATIC
    Src/QKKernel.cpp
    Src/QKTaskManager.cpp
    Src/QKScheduler.cpp
    Src/QKInterrupts.cpp
    Src/QKSyscallABI.cpp
    Src/QKCommandCenter.cpp
    Src/QKTime.cpp
    Src/QKSystemPump.cpp
    Src/QKShutdownController.cpp
    Src/QKSecureStore.cpp
    Src/QKSecurityCenter.cpp
    Src/QKAIRuntime.cpp
    Src/QKEntropy.cpp
    Src/QKImageReader.cpp
    Src/QKImagePipeline.cpp
    Src/QKModuleLoader.cpp
    Src/QKSimpleDb.cpp
    Src/QKCmdAuth.cpp
    Src/QKCmdParse.cpp
    Src/QKCmdPathFs.cpp
    Src/QKCmdBuiltins.cpp
    Src/QKCmdDebugTest.cpp
    Src/QKCmdNet.cpp
)

target_include_directories(QKernel PUBLIC Include)
target_link_libraries(QKernel PUBLIC QCommon QArch QEvent QFileSystem QNetwork QQuantum QSecurityCenter QGraphics QCQL QWindowing)
target_compile_options(QKernel PRIVATE ${KERNEL_COMPILE_FLAGS})
