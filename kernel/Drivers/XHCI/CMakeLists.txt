add_library(QKDrvXHCI STATIC
    QKDrvXHCI.cpp
)

target_include_directories(QKDrvXHCI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(QKDrvXHCI PUBLIC QCommon QArch QKMemory)
target_compile_options(QKDrvXHCI PRIVATE ${KERNEL_COMPILE_FLAGS})
