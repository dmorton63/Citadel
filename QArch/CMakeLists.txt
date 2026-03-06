add_library(QArch STATIC
    src/QArchGDT.cpp
    src/QArchIDT.cpp
    src/QArchPCI.cpp
    src/QArchCPU.cpp
    src/QArchRelax.cpp
)

target_include_directories(QArch PUBLIC include)
target_link_libraries(QArch PUBLIC QCommon)
target_compile_options(QArch PRIVATE ${KERNEL_COMPILE_FLAGS})
