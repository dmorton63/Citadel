# CQL Database Engine - TODO List

## 1. Core Data Structures and File Format
**Goal:** Get a minimal engine that can open a file, read/write pages, and store rows.

- [ ] Define structs:
  - [ ] FileHeader
  - [ ] TableEntry
  - [ ] ColumnDef
  - [ ] TableSchemaDisk
  - [ ] PageHeader
  - [ ] Page
  - [ ] Cell
  - [ ] Row
  - [ ] Table
  - [ ] Database
- [ ] Implement file open/create:
  - [ ] Create new DB with valid header
  - [ ] Create empty table directory
  - [ ] Create page region
- [ ] Implement page I/O:
  - [ ] `loadPage(pageId)`
  - [ ] `flushPage(pageId)`
  - [ ] In-memory Page mirrors on-disk layout
- [ ] **Milestone:** Create a DB file, allocate a page, and round-trip a dummy row

---

## 2. Schema Loading and In-Memory Model
**Goal:** Make the engine understand tables and schemas.

- [ ] Load table directory into `Database::tables`
- [ ] Load schemas into `TableSchema` for each table
- [ ] Validate schemas:
  - [ ] One PK per table
  - [ ] Unique column names
  - [ ] Valid types
- [ ] Wire Table objects:
  - [ ] name
  - [ ] schema
  - [ ] rootPage
  - [ ] pages
- [ ] **Milestone:** Engine can open an existing DB and list tables/columns

---

## 3. Page Allocator and Row Serialization
**Goal:** Be able to insert and read rows deterministically.

- [ ] Implement page allocator:
  - [ ] Sequential pageId assignment
  - [ ] `findPageForInsert(table, rowSizeNeeded)`
- [ ] Implement row serialization/deserialization:
  - [ ] Encode Row ↔ page bytes
  - [ ] Handle tombstones (simple flag or skip list)
- [ ] **Milestone:** Insert rows into a table and read them back via a simple scan

---

## 4. Primary Key Index (In-Memory)
**Goal:** Fast lookup by primary key.

- [ ] Implement `PrimaryKeyIndex`:
  - [ ] entries: `vector<Entry{key, pageId, rowOffset}>`
  - [ ] Build index at boot:
    - [ ] Scan all pages
    - [ ] Extract PK
    - [ ] Sort entries
- [ ] Implement lookup API:
  - [ ] `findByKey(key)` → (pageId, rowOffset) via binary search
- [ ] Update index on insert/update/delete
- [ ] **Milestone:** `SELECT ... WHERE pk = X` is O(log n) and correct

---

## 5. CRUD Engine API (No Parser Yet)
**Goal:** Solid programmatic API that matches your CQL engine surface.

- [ ] Implement core operations:
  - [ ] `insert(DbHandle&, table, Row&)`
  - [ ] `select(DbHandle&, SelectQuery&, QueryResult&)`
  - [ ] `update(DbHandle&, table, Row&, Condition&)`
  - [ ] `remove(DbHandle&, table, Condition&)`
- [ ] Enforce:
  - [ ] Permissions (from DbHandle)
  - [ ] Schema validation
  - [ ] PK uniqueness
  - [ ] PK-only WHERE in v0
- [ ] **Milestone:** Write unit tests that exercise full CRUD via C++ calls

---

## 6. Boot Sequence and System Tables
**Goal:** Make the engine start predictably and guarantee system tables exist.

- [ ] Implement boot phases:
  - [ ] Open file, validate header
  - [ ] Load table directory
  - [ ] Load schemas
  - [ ] Load pages and rebuild PK indexes
- [ ] Initialize system tables if missing:
  - [ ] Themes
  - [ ] ThemeTokens
  - [ ] Capabilities
- [ ] Add a "first-boot" path to create schemas + first pages
- [ ] **Milestone:** Engine boots into a known-good state every time

---

## 7. Parser and AST → Engine Bridge
**Goal:** Turn text into structured queries that call your engine API.

- [ ] Implement tokenizer
- [ ] Implement minimal grammar:
  - [ ] `SELECT ... FROM ... [WHERE pk = value]`
  - [ ] `INSERT INTO ... VALUES (...)`
  - [ ] `UPDATE ... SET ... [WHERE pk = value]`
  - [ ] `DELETE FROM ... WHERE pk = value`
- [ ] Build AST types and converters:
  - [ ] AST → SelectQuery
  - [ ] AST → Row
  - [ ] AST → Condition
- [ ] Add validation against schema:
  - [ ] Columns exist
  - [ ] Types match
  - [ ] PK-only WHERE
- [ ] **Milestone:** Feed a text query and get correct engine calls + results

---

## 8. Runtime Service and DbHandle Issuance
**Goal:** Make CQL a Citadel service with capability-scoped access.

- [ ] Implement CQL Runtime Service:
  - [ ] `openHandle(processId)` → asks SecurityCenter for capabilities
  - [ ] Map capabilities → `TablePermission[]` in DbHandle
  - [ ] `execute(DbHandle&, textQuery, QueryResult&)`:
    - [ ] Parse
    - [ ] Validate
    - [ ] Permission check
    - [ ] Engine call
- [ ] Enforce process binding on handles
- [ ] **Milestone:** ThemeService, SecurityCenter, PortManager can all talk to CQL through a stable runtime API

---

## 9. Integrate ThemeService First
**Goal:** Prove the engine in a real subsystem.

- [ ] Wire ThemeService to:
  - [ ] Load Themes and ThemeTokens
  - [ ] Build CitadelStyle
  - [ ] Update tokens on theme edit
- [ ] Replace any ad-hoc theme storage with CQL
- [ ] **Milestone:** Citadel boots, loads themes from CQL, and live theme changes persist

---

## 10. v1+ Durability (When Ready)
**Goal:** Add safety, not change architecture.

- [ ] Add WAL file and WALHeader/WALFrame
- [ ] Route writes through WAL
- [ ] Implement commit → checkpoint
- [ ] Implement recovery on boot
- [ ] **Milestone:** Pull the plug mid-write and come back to a consistent DB

---

## Security Model Implementation

### Citadel Security Center Responsibilities
- [ ] Define policies
- [ ] Issue capabilities
- [ ] Create DbHandles
- [ ] Log access
- [ ] Enforce trust boundaries
- [ ] Revoke handles when processes die

### CQL Engine Responsibilities
- [ ] Check the handle
- [ ] Check table metadata
- [ ] Enforce allowed operations
- [ ] Reject forbidden actions

### DbHandle Structure
- [ ] Implement `DbHandle` struct:
  - [ ] `Guid dbId`
  - [ ] `Guid callerProcessId`
  - [ ] `std::vector<TablePermission> tablePermissions`
- [ ] Implement `TablePermission` struct:
  - [ ] `std::string tableName`
  - [ ] `bool canRead`
  - [ ] `bool canWrite`
  - [ ] `bool canDelete`
  - [ ] `bool canAdmin`

---

## API Implementation

### Database Lifecycle
- [ ] `Status openDatabase(const char* path, DbHandle& outHandle)`
- [ ] `Status closeDatabase(DbHandle& handle)`
- [ ] `Status flush(DbHandle& handle)` (optional for v0)

### Table Operations
- [ ] `Status createTable(DbHandle& db, const TableSchema& schema)`
- [ ] `Status dropTable(DbHandle& db, const char* tableName)`

### Core CRUD Operations
- [ ] `Status insert(DbHandle& db, const char* table, const Row& row)`
- [ ] `Status update(DbHandle& db, const char* table, const Row& row, const Condition& where)`
- [ ] `Status remove(DbHandle& db, const char* table, const Condition& where)`
- [ ] `Status select(DbHandle& db, const SelectQuery& query, QueryResult& outResult)`

### Supporting Structures
- [ ] Implement `Row` struct
- [ ] Implement `Cell` struct
- [ ] Implement `Condition` struct
- [ ] Implement `SelectQuery` struct
- [ ] Implement `QueryResult` struct
- [ ] Implement `Status` enum

---

## File Format Implementation

### FileHeader (256 bytes)
- [ ] `char magic[8]` - "CQLDB\0\0"
- [ ] `uint32_t version` - v0 = 1
- [ ] `uint32_t pageSize` - 4096 recommended
- [ ] `uint32_t tableCount`
- [ ] `uint64_t tableDirOffset`
- [ ] `uint64_t schemaOffset`
- [ ] `uint64_t pageRegionOffset`
- [ ] `uint8_t reserved[200]`

### TableDirectory
- [ ] Implement `TableEntry` struct:
  - [ ] `char name[48]`
  - [ ] `uint64_t schemaOffset`
  - [ ] `uint64_t rootPage`
  - [ ] `uint32_t flags`
  - [ ] `uint8_t reserved[32]`

### SchemaRegion
- [ ] Implement `ColumnDef` struct:
  - [ ] `char name[48]`
  - [ ] `uint8_t type` (0=TEXT, 1=INT, 2=BOOL, 3=DATETIME)
  - [ ] `uint8_t isPrimaryKey`
  - [ ] `uint8_t reserved[14]`
- [ ] Implement `TableSchemaDisk` struct:
  - [ ] `char tableName[48]`
  - [ ] `uint32_t columnCount`
  - [ ] `ColumnDef columns[]` (variable length)

### Page Format
- [ ] Implement `PageHeader` struct:
  - [ ] `uint32_t pageId`
  - [ ] `uint32_t tableId`
  - [ ] `uint32_t rowCount`
  - [ ] `uint32_t freeOffset`
  - [ ] `uint8_t flags`
  - [ ] `uint8_t reserved[15]`

### Row Format
- [ ] Implement row storage:
  - [ ] `uint32_t rowLength`
  - [ ] `Cell[columnCount]`
- [ ] Implement Cell format for each type:
  - [ ] TEXT: `uint16_t type`, `uint16_t length`, `char value[length]`
  - [ ] INT: fixed-size
  - [ ] BOOL: fixed-size
  - [ ] DATETIME: fixed-size

---

## In-Memory Structures

### Database
- [ ] Implement `Database` struct:
  - [ ] `std::string path`
  - [ ] `FileHeader header`
  - [ ] `std::unordered_map<std::string, Table*> tables`
  - [ ] `std::unordered_map<uint32_t, Page*> pageCache`
  - [ ] `FileHandle file`
  - [ ] `uint32_t pageSize`

### Table
- [ ] Implement `Table` struct:
  - [ ] `std::string name`
  - [ ] `TableSchema schema`
  - [ ] `uint64_t rootPage`
  - [ ] `uint32_t tableId`
  - [ ] `uint32_t flags`
  - [ ] `PrimaryKeyIndex* pkIndex`

### Schema
- [ ] Implement `ColumnType` enum (Text, Int, Bool, DateTime)
- [ ] Implement `Column` struct
- [ ] Implement `TableSchema` struct with primaryKeyIndex

### Row and Cell
- [ ] Implement `Cell` struct with variant type
- [ ] Implement `Row` struct with vector of cells

### Page
- [ ] Implement `Page` struct:
  - [ ] `PageHeader header`
  - [ ] `std::vector<uint16_t> rowOffsets`
  - [ ] `std::vector<uint8_t> data`

### Primary Key Index
- [ ] Implement `PrimaryKeyIndex` struct:
  - [ ] `Entry` struct (key, pageId, rowOffset)
  - [ ] `std::vector<Entry> entries`

---

## Read/Write Code Paths

### Page Loading Path
- [ ] Check cache for pageId
- [ ] Seek and read from disk
- [ ] Decode header and row offsets
- [ ] Store in cache

### Row Deserialization Path
- [ ] Get row offset from page
- [ ] Read rowLength
- [ ] For each column, read Cell based on type
- [ ] Build Row object

### Row Serialization Path
- [ ] Serialize row into temporary buffer
- [ ] Check available space in page
- [ ] Write row bytes to page
- [ ] Update row offsets array
- [ ] Mark page dirty

### SELECT Path
- [ ] Permission check (canRead)
- [ ] Resolve table
- [ ] If WHERE on primary key: use pkIndex
- [ ] Else: full scan
- [ ] Load pages and deserialize rows
- [ ] Return QueryResult

### INSERT Path
- [ ] Permission check (canWrite)
- [ ] Validate row vs schema
- [ ] Find target page
- [ ] Serialize row into page
- [ ] Update primary key index
- [ ] Mark page dirty

### UPDATE Path
- [ ] Permission check (canWrite)
- [ ] Resolve row via pkIndex
- [ ] Load page, deserialize row
- [ ] Apply updates
- [ ] Re-serialize row
- [ ] Update pkIndex if needed

### DELETE Path
- [ ] Permission check (canDelete)
- [ ] Resolve row via pkIndex
- [ ] Load page
- [ ] Mark row deleted (tombstone)
- [ ] Remove entry from pkIndex

### FLUSH Path
- [ ] Iterate dirty pages in cache
- [ ] Serialize header, rowOffsets, data
- [ ] Write at correct offset
- [ ] Optional fsync

---

## Page Allocator

### Page ID Strategy
- [ ] Reserve page ID 0 for metadata
- [ ] Allocate page IDs 1..N sequentially

### Table → Page Mapping
- [ ] Add pages vector to Table struct
- [ ] Track rootPage and all pages per table

### Page Allocation API
- [ ] Implement `PageAllocator::allocatePage(Database& db, uint32_t tableId)`
- [ ] Implement `PageAllocator::getPage(Database& db, uint32_t pageId)`

---

## Testing & Validation

- [ ] Unit tests for CRUD operations
- [ ] Unit tests for schema validation
- [ ] Unit tests for primary key index
- [ ] Unit tests for page allocator
- [ ] Unit tests for serialization/deserialization
- [ ] Integration tests with ThemeService
- [ ] Performance tests for binary search on PK
- [ ] Security tests for permission enforcement

---

## Documentation

- [ ] Document file format specification
- [ ] Document API usage
- [ ] Document security model
- [ ] Document boot sequence
- [ ] Document migration path from ad-hoc storage to CQL
- [ ] Document performance characteristics

---

## Notes

- Keep v0 simple: no journaling, no transactions, no B-trees initially
- Security policy lives in Citadel, enforcement is shared
- CQL never authenticates - only enforces what DbHandle allows
- All operations require a DbHandle with appropriate permissions
- Page size recommendation: 4096 bytes
- Primary key index: binary search is sufficient for v0
- Write strategy: in-place writes, flush on demand
