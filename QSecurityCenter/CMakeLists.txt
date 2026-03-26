add_library(QSecurityCenter STATIC
    Src/QSCSecurityCenter.cpp
)

target_include_directories(QSecurityCenter PUBLIC Include)

target_link_libraries(QSecurityCenter PUBLIC QCCore QQuantum)

target_compile_options(QSecurityCenter PRIVATE ${KERNEL_COMPILE_FLAGS})
