add_library(CQLDatabaseEngine STATIC
    Database.cpp
    DiagnosticDumper.cpp
    FileManager.cpp
    PageManager.cpp
    QueryExecutor.cpp
    RowSerializer.cpp
    SQLParser.cpp
    Table.cpp
    WhereClauseEvaluator.cpp
    src/Storage/BTree.cpp
)

target_include_directories(CQLDatabaseEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Storage
)

add_library(QCSQLService STATIC
    QCSQLService.cpp
)

target_include_directories(QCSQLService PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(QCSQLService PUBLIC
    $<$<BOOL:${CITADEL_QCSQL_USE_QCQL_ENGINE}>:CITADEL_QCSQL_USE_QCQL_ENGINE=1>
    $<$<BOOL:${CITADEL_QCSQL_USE_CQL_ENGINE}>:CITADEL_QCSQL_USE_CQL_ENGINE=1>
)

target_link_libraries(QCSQLService PUBLIC
    CQLDatabaseEngine
)

add_executable(QCSQLServiceTestHost
    QCSQLServiceTest.cpp
)

target_include_directories(QCSQLServiceTestHost PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QCSQLServiceTestHost PRIVATE
    QCSQLService
)