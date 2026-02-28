add_subdirectory(drivers)

add_executable(citadel_kernel
    QKMain.cpp
    QKConsole.cpp
    Boot/QKBoot.cpp
    Boot/Arch/ArchInit.cpp
    Boot/Acpi/AcpiTables.cpp
    Boot/Config/BootJson.cpp
    Boot/Config/SysConfig.cpp
    Boot/Config/StartupConfig.cpp
    Boot/Config/RamdiskFile.cpp
    Boot/Security/Sha256.cpp
    Boot/Security/BootJsonSigKey.cpp
    Boot/Security/BootJsonSignature.cpp
    Boot/Ramdisk/RamdiskMount.cpp
    Boot/Desktop/DesktopSession.cpp
    Boot/Memory/AddressMapping.cpp
    Boot/Memory/EarlyMemory.cpp
    Boot/Tpm/TpmSecureStore.cpp
    Boot/Limine/LimineRequests.cpp
    Boot/Limine/LimineModules.cpp
    Debug/Serial/SerialDebug.cpp
    Debug/Panic.cpp
    Debug/Terminal/LimineTerminal.cpp
)

# Add assembly with custom compile flags
set_source_files_properties(QKBoot.asm PROPERTIES LANGUAGE ASM_NASM)
target_sources(citadel_kernel PRIVATE QKBoot.asm)

target_include_directories(citadel_kernel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/limine
)

target_link_libraries(citadel_kernel PRIVATE
    QCommon
    QKernel
    QKMemory
    QArch
    QEvent
    QDrivers
    QFileSystem
    QJFunctions
    QWindowing
    QWControls
    QDesktop
    QKDrivers
)

target_compile_options(citadel_kernel PRIVATE ${KERNEL_COMPILE_FLAGS})

target_compile_definitions(citadel_kernel PRIVATE
    $<$<BOOL:${CITADEL_PRODUCTION}>:CITADEL_PRODUCTION=1>
)

# Linker script
set_target_properties(citadel_kernel PROPERTIES
    LINK_FLAGS "-T ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld -nostdlib -static"
    OUTPUT_NAME "citadel.elf"
)

# Custom target to create bootable image
add_custom_command(TARGET citadel_kernel POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:citadel_kernel> ${CMAKE_BINARY_DIR}/citadel.bin
    COMMENT "Creating bootable binary image"
)
