add_subdirectory(Drivers)

add_executable(citadel_kernel
    QKMain.cpp
    QKConsole.cpp
    Boot/QKBoot.cpp
    Boot/Arch/QKBootArchInit.cpp
    Boot/ACPI/QKBootACPITables.cpp
    Boot/Config/QKBootJson.cpp
    Boot/Config/QKBootSysConfig.cpp
    Boot/Config/QKBootConfigTier.cpp
    Boot/Config/QKBootStagedConfig.cpp
    Boot/Config/QKBootLog.cpp
    Boot/Config/QKBootEventLog.cpp
    Boot/Config/QKBootStartupConfig.cpp
    Boot/Config/QKBootRamdiskFile.cpp
    Boot/Security/QKBootSha256.cpp
    Boot/Security/QKBootJsonSigKey.cpp
    Boot/Security/QKBootJsonSignature.cpp
    Boot/Ramdisk/QKBootRamdiskMount.cpp
    Boot/Desktop/QKBootDesktopSession.cpp
    Boot/Memory/QKBootAddressMapping.cpp
    Boot/Memory/QKBootEarlyMemory.cpp
    Boot/TPM/QKBootTPMSecureStore.cpp
    Boot/Limine/QKBootLimineRequests.cpp
    Boot/Limine/QKBootLimineModules.cpp
    Debug/Serial/QKDebugSerial.cpp
    Debug/QKDebugPanic.cpp
    Debug/Terminal/QKDebugLimineTerminal.cpp
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
