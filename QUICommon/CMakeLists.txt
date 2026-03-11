add_library(QUICommon STATIC
    Src/QCUIStyle.cpp
)

target_include_directories(QUICommon PUBLIC Include)
target_link_libraries(QUICommon PUBLIC QCCore)
target_compile_options(QUICommon PRIVATE ${KERNEL_COMPILE_FLAGS})
