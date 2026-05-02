add_library(QGfx STATIC
    Src/QGfxBatch.cpp
    Src/QGfxContext.cpp
    Src/QGfxVmwareSVGADriver.cpp
)

target_include_directories(QGfx PUBLIC Include)
target_link_libraries(QGfx PUBLIC
    QCommon
    QDrivers)
target_compile_options(QGfx PRIVATE ${KERNEL_COMPILE_FLAGS})
