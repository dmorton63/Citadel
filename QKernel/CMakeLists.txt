add_library(QKernel STATIC
    Src/QKKernel.cpp
    Src/QKTaskManager.cpp
    Src/QKScheduler.cpp
    Src/QKInterrupts.cpp
    Src/QKCommandCenter.cpp
    Src/QKTime.cpp
    Src/QKSystemPump.cpp
    Src/QKShutdownController.cpp
    Src/QKSecureStore.cpp
    Src/QKSecurityCenter.cpp
    Src/QKEntropy.cpp
)

target_include_directories(QKernel PUBLIC Include)
target_link_libraries(QKernel PUBLIC QCommon QEvent QFileSystem QNetwork)
target_compile_options(QKernel PRIVATE ${KERNEL_COMPILE_FLAGS})
