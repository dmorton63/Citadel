add_library(QFileSystem STATIC
    Src/QFSVFS.cpp
    Src/QFSFile.cpp
    Src/QFSDirectory.cpp
    Src/QFSFAT32.cpp
    Src/QFSFAT16.cpp
    Src/QFSFATProbe.cpp
    Src/QFSRoleTier.cpp
    Src/QFSStat.cpp
    Src/QFSCache.cpp
    Src/QFSPath.cpp
    Src/QFSVolumeManager.cpp
)

target_include_directories(QFileSystem PUBLIC Include)
target_link_libraries(QFileSystem PUBLIC QCommon QKMemory)
target_compile_options(QFileSystem PRIVATE ${KERNEL_COMPILE_FLAGS})
