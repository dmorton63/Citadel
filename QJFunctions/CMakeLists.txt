add_library(QJFunctions STATIC
    Src/QJFunction.cpp
    Src/QJFunctionRegistry.cpp
    Src/QJFExecutor.cpp
    Src/QJFJitAllocator.cpp
)

target_include_directories(QJFunctions PUBLIC Include)
target_link_libraries(QJFunctions PUBLIC QCCore QCMath QCSerialization QFileSystem)
target_compile_options(QJFunctions PRIVATE ${KERNEL_COMPILE_FLAGS})
