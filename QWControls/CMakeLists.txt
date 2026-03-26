add_library(QWControls STATIC
    Src/Base/ControlBase.cpp
    Src/Containers/Container.cpp
    Src/Containers/Frame.cpp
    Src/Containers/Panel.cpp
    Src/Containers/ScrollPanel.cpp
    Src/Leaf/Button.cpp
    Src/Leaf/IconButton.cpp
    Src/Leaf/Label.cpp
    Src/Leaf/ImageView.cpp
    Src/Leaf/ScrollBar.cpp
    Src/Leaf/TextBox.cpp
    Src/Composite/ComboBox.cpp
    Src/Composite/ListView.cpp
)

target_compile_options(QWControls PRIVATE ${KERNEL_COMPILE_FLAGS})
target_include_directories(QWControls PUBLIC Include)
target_link_libraries(QWControls PUBLIC QCommon QGraphics QWindowing QEvent QKMemory QWInterfaces)
