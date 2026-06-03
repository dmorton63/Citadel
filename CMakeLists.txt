cmake_minimum_required(VERSION 3.20)
project(Citadel VERSION 1.0.0 LANGUAGES C CXX ASM)

# Helps editors (VS Code, clangd) mirror CMake's exact include paths/defines.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable NASM for assembly files
enable_language(ASM_NASM)
set(CMAKE_ASM_NASM_OBJECT_FORMAT elf64)
set(CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> -f ${CMAKE_ASM_NASM_OBJECT_FORMAT} -o <OBJECT> <SOURCE>")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)

option(CITADEL_PRODUCTION "Enable fail-closed secure boot behavior" OFF)
option(CITADEL_PAL_USE_QFS_VFS "Enable CitadelPAL QFileSystem VFS integration paths" ON)
option(CITADEL_QCSQL_USE_QCQL_ENGINE "Enable QCSQL service bindings to QCQL::Engine" OFF)
option(CITADEL_QCSQL_USE_CQL_ENGINE "Enable QCSQL service bindings to CQL_Database_Engine::Database" ON)

# Freestanding kernel flags (only for C/C++, not assembly)
set(KERNEL_COMPILE_FLAGS
    $<$<COMPILE_LANGUAGE:CXX,C>:-ffreestanding>
    $<$<COMPILE_LANGUAGE:CXX,C>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX,C>:-nostdlib>
    $<$<COMPILE_LANGUAGE:CXX,C>:-mno-red-zone>
    $<$<COMPILE_LANGUAGE:CXX,C>:-mcmodel=kernel>
    $<$<COMPILE_LANGUAGE:CXX,C>:-fno-pic>
    $<$<COMPILE_LANGUAGE:CXX,C>:-fno-pie>
)

# Disable PIC/PIE globally for kernel code
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

# Add subdirectories
#
# NOTE: This project uses CMake; you do not create per-folder Makefiles manually.
# Each module directory provides a CMakeLists.txt, and CMake generates build files.

# Umbrella target kept for compatibility with existing kernel/module linking.
add_library(QCommon INTERFACE)
target_compile_options(QCommon INTERFACE ${KERNEL_COMPILE_FLAGS})
target_compile_definitions(QCommon INTERFACE
    $<$<BOOL:${CITADEL_PAL_USE_QFS_VFS}>:CITADEL_PAL_USE_QFS_VFS=1>
    $<$<BOOL:${CITADEL_QCSQL_USE_QCQL_ENGINE}>:CITADEL_QCSQL_USE_QCQL_ENGINE=1>
    $<$<BOOL:${CITADEL_QCSQL_USE_CQL_ENGINE}>:CITADEL_QCSQL_USE_CQL_ENGINE=1>
)

# Core/support libraries (folder restructure)
add_subdirectory(QCCore)
add_subdirectory(QCMath)
add_subdirectory(QCSerialization)
add_subdirectory(QUICommon)
add_subdirectory(QCommand)
add_subdirectory(QCQL)
add_subdirectory(QJFunctions)
add_subdirectory(CQL_Database_Engine)

target_link_libraries(QCommon INTERFACE
    QCCore
    QCMath
    QCSerialization
    QUICommon
    QCommand
)

add_subdirectory(QKernel)
add_subdirectory(QKMemory)
add_subdirectory(QArch)
add_subdirectory(QEvent)
add_subdirectory(QDrivers)
# add_subdirectory(QUSB)  # Replaced by kernel/Drivers/UHCI and XHCI
add_subdirectory(QFileSystem)
add_subdirectory(QNetwork)
add_subdirectory(QGraphics)
add_subdirectory(QGfx)
add_subdirectory(QC3D)
add_subdirectory(QWindowing)
add_subdirectory(QWInterfaces)
add_subdirectory(QWControls)
add_subdirectory(QCMS)
add_subdirectory(QDesktop)
add_subdirectory(QPR)
add_subdirectory(QQuantum)
add_subdirectory(QSecurityCenter)
add_subdirectory(kernel)
