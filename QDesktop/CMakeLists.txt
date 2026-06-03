add_library(QDesktop STATIC
    Src/QDAccent.cpp
    Src/QDBrowser.cpp
    Src/QDCuiMLViewer.cpp
    Src/QDDesktop.cpp
    Src/QDDesktopDocument.cpp
    Src/QDDesktopDocumentIO.cpp
    Src/QDDesktopDocumentValidation.cpp
    Src/QDHtml.cpp
    Src/QDTheme.cpp
    Src/QDThemeAssets.cpp
    Src/QDThemeService.cpp
    Src/QDTerminal.cpp
    Src/QDShutdownDialog.cpp
    Src/QDSetupWizard.cpp
    Src/QDLoginDialog.cpp
    Src/QDCommandProcessor.cpp
    Src/QDThemeImporter.cpp

)

target_include_directories(QDesktop PUBLIC
    Include
    ${CMAKE_SOURCE_DIR}/QCMS/Include
    ${CMAKE_SOURCE_DIR}/QKernel/Include)
target_link_libraries(QDesktop PUBLIC QCommon QKernel QGraphics QWindowing QWControls QFileSystem QNetwork QCQL QCMS)
target_compile_options(QDesktop PRIVATE ${KERNEL_COMPILE_FLAGS})
