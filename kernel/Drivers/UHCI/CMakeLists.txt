add_library(QKDrvUHCI STATIC
    QKDrvUHCI.cpp
)

target_include_directories(QKDrvUHCI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(QKDrvUHCI PUBLIC QCommon QArch)
target_compile_options(QKDrvUHCI PRIVATE ${KERNEL_COMPILE_FLAGS})
