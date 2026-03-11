add_library(QDesktop STATIC
    Src/QDAccent.cpp
    Src/QDDesktop.cpp
    Src/QDTheme.cpp
    Src/QDTerminal.cpp
    Src/QDShutdownDialog.cpp
    Src/QDSetupWizard.cpp
    Src/QDLoginDialog.cpp
    Src/QDCommandProcessor.cpp

)

target_include_directories(QDesktop PUBLIC
    Include
    ${CMAKE_SOURCE_DIR}/QKernel/Include)
target_link_libraries(QDesktop PUBLIC QCommon QKernel QGraphics QWindowing QWControls QFileSystem)
target_compile_options(QDesktop PRIVATE ${KERNEL_COMPILE_FLAGS})
