add_library(QArch STATIC
    Asm/QArchContextSwitch.asm
    Src/QArchGDT.cpp
    Src/QArchIDT.cpp
    Src/QArchPCI.cpp
    Src/QArchCPU.cpp
    Src/QArchRelax.cpp
)

set_source_files_properties(Asm/QArchContextSwitch.asm PROPERTIES LANGUAGE ASM_NASM)

target_include_directories(QArch PUBLIC Include)
target_link_libraries(QArch PUBLIC QCommon)
target_compile_options(QArch PRIVATE ${KERNEL_COMPILE_FLAGS})
