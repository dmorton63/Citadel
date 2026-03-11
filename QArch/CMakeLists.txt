add_library(QArch STATIC
    Src/QArchGDT.cpp
    Src/QArchIDT.cpp
    Src/QArchPCI.cpp
    Src/QArchCPU.cpp
    Src/QArchRelax.cpp
)

target_include_directories(QArch PUBLIC Include)
target_link_libraries(QArch PUBLIC QCommon)
target_compile_options(QArch PRIVATE ${KERNEL_COMPILE_FLAGS})
