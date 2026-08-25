# QC3D - Software 3D rendering primitives/pipeline

add_library(QC3D STATIC
	Src/QC3DContext.cpp
	Src/QC3DMesh.cpp
	Src/QCClipping.cpp
	Src/QCDepthBuffer.cpp
	Src/QCPixelShader.cpp
	Src/CQRasterizer.cpp
	Src/QCVertexShader.cpp
)

target_include_directories(QC3D PUBLIC Include)
target_link_libraries(QC3D PUBLIC QCommon QGraphics)
target_compile_options(QC3D PRIVATE ${KERNEL_COMPILE_FLAGS})