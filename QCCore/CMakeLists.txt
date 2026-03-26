add_library(QCCore STATIC
    Src/QCString.cpp
    Src/QCBuiltins.cpp
    Src/QCLogger.cpp
    Src/QCxxAbi.cpp
    Src/QCMemUtil.c
    Src/QCSha256.cpp
    Src/QCCanonicalArgs.cpp
)

target_include_directories(QCCore PUBLIC Include)
target_compile_options(QCCore PRIVATE ${KERNEL_COMPILE_FLAGS})
