# QGraphics - Core graphics abstractions

add_library(QGraphics STATIC
	src/QG/PainterSurface.cpp
	src/QG/Image.cpp
	${CMAKE_SOURCE_DIR}/shared/third_party/miniz_tinfl.c
)

# miniz (tinfl) is third-party code intended for hosted environments.
# Build it in a freestanding-friendly configuration for the kernel.
set_source_files_properties(${CMAKE_SOURCE_DIR}/shared/third_party/miniz_tinfl.c
	PROPERTIES
		COMPILE_DEFINITIONS "MINIZ_NO_MALLOC;MINIZ_NO_STDIO;MINIZ_NO_TIME;NDEBUG")

target_include_directories(QGraphics PUBLIC include)
target_include_directories(QGraphics PRIVATE ${CMAKE_SOURCE_DIR}/shared/third_party)
target_link_libraries(QGraphics PUBLIC QCommon)
target_compile_options(QGraphics PRIVATE ${KERNEL_COMPILE_FLAGS})
