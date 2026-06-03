# Kernel Drivers - CMakeLists.txt

add_subdirectory(PS2)
add_subdirectory(UHCI)
add_subdirectory(XHCI)
add_subdirectory(E1000)

add_library(QKDrivers STATIC
    QKDrvManager.cpp
    QKStorageRegistry.cpp
    QKStorageProbe.cpp
    AHCI/QKDrvAHCI.cpp
    IDE/QKDrvIDE.cpp
)

target_include_directories(QKDrivers PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/QFileSystem/Include
    ${PROJECT_SOURCE_DIR}/limine
)
target_link_libraries(QKDrivers PUBLIC
    QKDrvPS2
    QKDrvUHCI
    QKDrvXHCI
    QKDrvE1000
    QKMemory
    QCommon
    QArch
)
target_compile_options(QKDrivers PRIVATE ${KERNEL_COMPILE_FLAGS})
