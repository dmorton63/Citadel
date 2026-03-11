add_library(QNetwork STATIC
    Src/QNetStack.cpp
    Src/QNetEthernet.cpp
    Src/QNetIP.cpp
    Src/QNetTCP.cpp
    Src/QNetUDP.cpp
    Src/QNetDNS.cpp
    Src/QNetDHCP.cpp
    Src/QNetSocket.cpp
)

target_include_directories(QNetwork PUBLIC Include)
target_link_libraries(QNetwork PUBLIC QCommon QKMemory)
target_compile_options(QNetwork PRIVATE ${KERNEL_COMPILE_FLAGS})
