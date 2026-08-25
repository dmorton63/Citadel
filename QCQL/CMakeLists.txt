add_library(QCQL STATIC
    Src/QCQLEngine.cpp
    Src/QCQLRuntime.cpp
)

target_include_directories(QCQL PUBLIC Include)
target_link_libraries(QCQL PUBLIC QCommon QFileSystem)
target_compile_options(QCQL PRIVATE ${KERNEL_COMPILE_FLAGS})
