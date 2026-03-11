add_library(QEvent STATIC
    Src/QKEventTypes.cpp
    Src/QKEventQueue.cpp
    Src/QKEventListener.cpp
    Src/QKEventManager.cpp
    Src/QKMsgBus.cpp
    Src/QKServiceRegistry.cpp
    Src/QKRuntimeRegistries.cpp
)

target_include_directories(QEvent PUBLIC Include)
target_link_libraries(QEvent PUBLIC QCommon)
target_compile_options(QEvent PRIVATE ${KERNEL_COMPILE_FLAGS})
