add_library(QWindowing STATIC
    Src/QWWindow.cpp
    Src/QWWindowManager.cpp
    Src/QWFramebuffer.cpp
    Src/QWFramebufferBackend.cpp
    Src/QWCompositor.cpp
    Src/QWFramebufferPresentBackend.cpp
    Src/QWVmwareSVGAPresentBackend.cpp
    Src/QWMessageBus.cpp
    Src/QWRenderer.cpp
    Src/QWStyleRenderer.cpp
    Src/QWStyleTypes.cpp
    Src/QWSurfaceBackend.cpp
    Src/QWStyleSystem.cpp
)

target_include_directories(QWindowing PUBLIC Include)
target_link_libraries(QWindowing PUBLIC
    QCommon
    QGfx
    QGraphics
    QDrivers
    QEvent
    QKMemory
    QWControls
    QWInterfaces)
target_compile_options(QWindowing PRIVATE ${KERNEL_COMPILE_FLAGS})
