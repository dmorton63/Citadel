add_library(QKMemory STATIC
    Src/QKMemPMM.cpp
    Src/QKMemVMM.cpp
    Src/QKMemHeap.cpp
    Src/QKMemPaging.cpp
    Src/QKMemTranslator.cpp
)

target_include_directories(QKMemory PUBLIC Include)
target_link_libraries(QKMemory PUBLIC QCommon)
target_compile_options(QKMemory PRIVATE ${KERNEL_COMPILE_FLAGS})

# Add dependency to ensure proper build order
add_dependencies(QKMemory QCommon)
