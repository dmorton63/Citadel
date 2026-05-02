add_library(QCMS STATIC
    Src/QCMSNavBar.cpp
    Src/QCMSDbBrowser.cpp
    Src/QCMSQueryStudio.cpp
    Src/QCMSApp.cpp
)

target_include_directories(QCMS PUBLIC
    Include
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/QKernel/Include)
target_link_libraries(QCMS PUBLIC QCommon QFileSystem QCQL QWindowing QWControls)
target_compile_options(QCMS PRIVATE ${KERNEL_COMPILE_FLAGS})
