# e1000 Network Driver - CMakeLists.txt

add_library(QKDrvE1000 STATIC
    QKDrvE1000.cpp
)

target_include_directories(QKDrvE1000 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(QKDrvE1000 PUBLIC
    QArch
    QCommon
    QKMemory
    QNetwork
)

target_compile_options(QKDrvE1000 PRIVATE ${KERNEL_COMPILE_FLAGS})
