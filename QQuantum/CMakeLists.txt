add_library(QQuantum
	Src/QQExecutor.cpp
	Src/QQScheduler.cpp
)

target_include_directories(QQuantum PUBLIC Include)

target_link_libraries(QQuantum PUBLIC QCCore QKernel)

target_include_directories(QQuantum PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/../QCCore/Include
)
