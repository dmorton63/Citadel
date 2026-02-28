add_library(QJFunctions STATIC
    src/QJFunction.cpp
    src/QJFunctionRegistry.cpp
)

target_include_directories(QJFunctions PUBLIC include)
target_link_libraries(QJFunctions PUBLIC QCCore QCMath QCSerialization QFileSystem)
target_compile_options(QJFunctions PRIVATE ${KERNEL_COMPILE_FLAGS})
