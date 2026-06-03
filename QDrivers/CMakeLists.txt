add_library(QDrivers STATIC
    Src/QDrvDisplayDevice.cpp
    Src/QDrvTimer.cpp
    Src/QDrvBGA.cpp
    Src/QDrvVmwareSVGA.cpp
)

target_include_directories(QDrivers PUBLIC Include)
target_link_libraries(QDrivers PUBLIC QCommon QKernel QArch)
target_compile_options(QDrivers PRIVATE ${KERNEL_COMPILE_FLAGS})
