add_library(QCSerialization STATIC
    Src/QCJson.cpp
    Src/QCJsonOverlay.cpp
)

target_include_directories(QCSerialization PUBLIC Include)
target_link_libraries(QCSerialization PUBLIC QCCore)
target_compile_options(QCSerialization PRIVATE ${KERNEL_COMPILE_FLAGS})
