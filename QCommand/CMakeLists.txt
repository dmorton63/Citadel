add_library(QCommand STATIC
    Src/QCCommandRegistry.cpp
)

target_include_directories(QCommand PUBLIC Include)
target_link_libraries(QCommand PUBLIC QCCore)
target_compile_options(QCommand PRIVATE ${KERNEL_COMPILE_FLAGS})
