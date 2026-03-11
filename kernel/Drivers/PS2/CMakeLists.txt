add_library(QKDrvPS2 STATIC
    QKDrvPS2Mouse.cpp
    QKDrvPS2Keyboard.cpp
)

target_include_directories(QKDrvPS2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(QKDrvPS2 PUBLIC QCommon QArch QKernel)
target_compile_options(QKDrvPS2 PRIVATE ${KERNEL_COMPILE_FLAGS})
