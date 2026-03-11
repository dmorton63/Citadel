add_library(QCMath INTERFACE)

target_include_directories(QCMath INTERFACE Include)
target_link_libraries(QCMath INTERFACE QCCore)
target_compile_options(QCMath INTERFACE ${KERNEL_COMPILE_FLAGS})
