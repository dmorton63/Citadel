Summary:
Current direction:
- CQL is not just a generic storage engine for Citadel; it is intended to become the durable runtime source for desktop themes, layout specifications, control metadata, and related UI design data.
- The existing `.json` / `.cml` parsing path is still needed, but primarily as an import/bootstrap and fallback path.
- Because of that, relational schema support matters earlier than a generic CRUD-only milestone order would suggest: desktop/runtime data needs normalized related tables, stable schema modeling, and foreign-key-style relationship behavior.

Current implementation surfaces in the repo:
- `QCQL/`: the in-repo engine core. This is the real low-level implementation surface today for file/header layout, schema objects, page allocation/I/O, row serialization, primary-key index rebuild/lookup, table creation, and PK-based row operations.
- `CQL_Database_Engine/`: the service/runtime bridge and standalone development track. `QCSQLService.cpp` already contains a real message-routed/service-style adapter and a subset text-query parsing path, but this layer is still transitional and not yet the finished Citadel-native runtime boundary.
- `QDesktop/`: the current consumer integration path. Desktop bring-up already creates/opens `/system/CMMS.QDB`, imports built-in themes into `Themes`/`ThemeTokens`, seeds desktop document tables/chunk tables, and can load theme state from QCQL during normal startup.

Practical breakdown for implementation review:
- Engine core: `QCQL`
- Service/runtime boundary: `CQL_Database_Engine`
- Desktop integration: `QDesktop`

What is still missing across those surfaces:
- normalized relational modeling for desktop objects
- foreign-key / relationship metadata and enforcement
- a settled Citadel-native runtime service boundary
- full database-first desktop boot that no longer depends on file-shaped source data as the long-term model

1. Core data structures and file format
Goal: Get a minimal engine that can open a file, read/write pages, and store rows.
- Define structs:
- FileHeader, TableEntry, ColumnDef, TableSchemaDisk
- PageHeader, Page, Cell, Row, Table, Database
- Implement file open/create:
- Create new DB with valid header, empty table directory, page region.
- Implement page I/O:
- loadPage(pageId), flushPage(pageId)
- In‑memory Page mirrors on‑disk layout.
Milestone: You can create a DB file, allocate a page, and round‑trip a dummy row.

2. Schema loading and in‑memory model
Goal: Make the engine understand tables and schemas.
- Load table directory into Database::tables.
- Load schemas into TableSchema for each table.
- Validate schemas (one PK, unique names, valid types, and room for relationship metadata).
- Wire Table objects:
- name, schema, rootPage, pages.
Milestone: Engine can open an existing DB and list tables/columns.

2a. Relational modeling and relationships
Goal: Make CQL capable of expressing Citadel runtime data that is relational, not just flat.
- Define relationship metadata in schema structures.
- Add foreign-key/reference descriptors and validation rules.
- Decide initial parent/child update-delete behavior for v1.
- Enforce relationship integrity in insert/update/delete paths.
Milestone: CQL can model desktop/theme/layout/control data using normalized related tables rather than a flat-file-shaped schema.

3. Page allocator and row serialization
Goal: Be able to insert and read rows deterministically.
- Page allocator:
- Sequential pageId assignment.
- findPageForInsert(table, rowSizeNeeded).
- Row serialization/deserialization:
- Encode/decode Row ↔ page bytes.
- Handle tombstones (simple flag or skip list).
Milestone: You can insert rows into a table and read them back via a simple scan.

4. Primary key index (in‑memory)
Goal: Fast lookup by primary key.
- Implement PrimaryKeyIndex:
- entries: vector<Entry{key, pageId, rowOffset}>.
- Build index at boot:
- Scan all pages, extract PK, sort entries.
- Lookup API:
- findByKey(key) → (pageId, rowOffset) via binary search.
- Update on insert/update/delete.
Milestone: SELECT ... WHERE pk = X is O(log n) and correct.

5. CRUD engine API (no parser yet)
Goal: Solid programmatic API that matches your CQL engine surface.
- Implement:
- insert(DbHandle&, table, Row&)
- select(DbHandle&, SelectQuery&, QueryResult&)
- update(DbHandle&, table, Row&, Condition&)
- remove(DbHandle&, table, Condition&)
- Enforce:
- permissions (from DbHandle)
- schema validation
- PK uniqueness
- PK‑only WHERE in v0
Milestone: You can write unit tests that exercise full CRUD via C++ calls.

6. Boot sequence and system tables
Goal: Make the engine start predictably and guarantee system tables exist.
- Implement boot phases:
- Open file, validate header.
- Load table directory.
- Load schemas.
- Load pages and rebuild PK indexes.
- Initialize system tables if missing:
- Themes
- ThemeTokens
- DesktopLayouts
- DesktopControls
- DesktopAssets
- Capabilities
- Add a “first‑boot” path to create schemas + first pages.
Milestone: Engine boots into a known‑good state every time.

7. Parser and AST → engine bridge
Goal: Turn text into structured queries that call your engine API.
- Implement tokenizer.
- Implement minimal grammar:
- SELECT ... FROM ... [WHERE pk = value]
- INSERT INTO ... VALUES (...)
- UPDATE ... SET ... [WHERE pk = value]
- DELETE FROM ... WHERE pk = value
- Build AST types and converters:
- AST → SelectQuery, Row, Condition.
- Add validation against schema (columns, types, PK‑only WHERE).
Milestone: You can feed a text query and get correct engine calls + results.

8. Runtime service and DbHandle issuance
Goal: Make CQL a Citadel service with capability‑scoped access.
- Implement CQL Runtime Service:
- openHandle(processId) → asks SecurityCenter for capabilities.
- Map capabilities → TablePermission[] in DbHandle.
- Implement execute(DbHandle&, textQuery, QueryResult&):
- parse → validate → permission check → engine call.
- Enforce process binding on handles.
Milestone: ThemeService, desktop runtime services, SecurityCenter, and PortManager can all talk to CQL through a stable runtime API.

9. Integrate ThemeService first
Goal: Prove the engine in a real subsystem.
- Wire ThemeService to:
- load Themes and ThemeTokens.
- build CitadelStyle.
- update tokens on theme edit.
- Replace any ad‑hoc theme storage with CQL.
Milestone: Citadel boots, loads themes from CQL, and live theme changes persist.

9a. Integrate desktop runtime data next
Goal: Move desktop/runtime design data out of external asset files and into CQL-backed schemas.
- Define normalized tables for layouts, control instances, hierarchy, theme bindings, and asset references.
- Keep the existing parser as the import/bootstrap path for initial `.json` / `.cml` assets.
- Materialize imported desktop definitions into CQL tables.
- Make normal boots prefer CQL-backed desktop data, with file import reserved for provisioning, recovery, or migration.
Milestone: Citadel can import desktop definitions once, persist them relationally, and build the desktop from CQL on normal boots.

10. v1+ durability (when you’re ready)
Goal: Add safety, not change architecture.
- Add WAL file and WALHeader/WALFrame.
- Route writes through WAL.
- Implement commit → checkpoint.
- Implement recovery on boot.
Milestone: Pull the plug mid‑write and come back to a consistent DB.

***************************************************************************************************************************
DETAILS:  Below are the details Section by Section
***************************************************************************************************************************
============================================================================================================
Section 1
============================================================================================================
You’re right to pause here—where you put security for CQL will shape the whole system.
Short version:
Security policy lives in Citadel.
Security enforcement is shared: Citadel at the boundary, CQL at the object level.

1. Don’t turn CQL into its own security universe
If you fully “lock it down inside CQL”:
- You duplicate concepts: identities, roles, capabilities, policies
- You now have two security stacks to maintain
- Bugs in either one become attack surfaces
- You lose the single‑source‑of‑truth model Citadel is aiming for
That’s exactly the kind of complexity SQL Server grew into over decades. You don’t want that inside Citadel.

2. Let Citadel Security own who and what, CQL own how far
Think of it like this:
- Citadel Security Center decides:
- Who is calling (process identity, user, service)
- What they’re allowed to do (capabilities: Db.Read, Db.Write, Db.Admin, per‑table scopes)
- Which database / schema they can touch
- CQL Engine enforces:
- “Given this capability, this table, and this operation—is it allowed?”
- Per‑table / per‑operation checks (read/write/admin)
- No knowledge of users, roles, or external auth—only capabilities
So the flow becomes:
- Caller asks Citadel for a DB handle → gets a capability‑scoped handle
- All CQL operations go through that handle
- CQL checks:
- “Does this handle allow SELECT on Themes?”
- “Does this handle allow UPDATE on ThemeTokens?”
No raw, unauthenticated access to CQL.

3. Where to store the rules
You can split it cleanly:
- Policy definition: in Citadel (Security Center, config, or a SecurityPolicy table only Citadel can modify)
- Policy metadata: optionally stored in the DB (e.g., per‑table flags like systemTable, userTable, readOnly)
- Policy enforcement:
- Citadel: “you don’t even get a handle”
- CQL: “you have a handle, but this operation on this table is forbidden”
That way, CQL never has to know who you are—only what this handle is allowed to do.

4. What this gives you
- Single security brain: Citadel Security Center
- Thin, deterministic enforcement in CQL: no user management, no passwords, no logins
- Capability‑driven DB access: matches your port manager and network model
- Less code in the DB engine: fewer places for security bugs
- Room to grow: later you can add:
- per‑table capabilities
- per‑schema capabilities
- “system tables” only kernel can touch

5. Concrete design rule
- No function in the CQL engine should execute a query without a DbHandle that carries:
- dbId
- allowedTables
- allowedOps (read/write/admin)
- maybe tenantId or scope later
Everything else—users, roles, TPM mode, trust levels—stays in Citadel.
===========================================================================================================================

Perfect — let’s lock in the actual security model for CQL so you can build the engine with confidence and not paint yourself into a corner later.
Below is the cleanest, most maintainable, Citadel‑consistent design you can adopt. It keeps CQL lean, keeps Citadel in control, and avoids the SQL Server trap of becoming a second operating system.

🔐 1. Citadel Security Center owns identity and capabilities
This is the golden rule.
CQL should never know:
- who the user is
- what role they have
- what process they belong to
- what permissions they have
That’s Citadel’s job.
Citadel Security Center issues a DbHandle that already encodes:
- which database the caller can access
- which tables they can see
- which operations they can perform (read/write/admin)
- optional row‑level or schema‑level scopes
CQL never authenticates anything.
It only enforces what the handle says.
This keeps the database engine small, deterministic, and safe.

🧱 2. CQL enforces table‑level and operation‑level permissions
CQL’s job is simple:
“Given this handle, is this operation on this table allowed?”

That’s it.
CQL enforces:
- read allowed?
- write allowed?
- delete allowed?
- admin allowed?
- is this table system‑protected?
CQL does not:
- manage users
- manage roles
- manage passwords
- manage tokens
- manage trust levels
That all stays in Citadel.

🧩 3. DbHandle: the bridge between Citadel and CQL
This is the key structure.
struct DbHandle {
    Guid dbId;
    Guid callerProcessId;
    std::vector<TablePermission> tablePermissions;
};

struct TablePermission {
    std::string tableName;
    bool canRead;
    bool canWrite;
    bool canDelete;
    bool canAdmin;
};


Every CQL operation requires a DbHandle.
If the handle doesn’t allow the operation → CQL rejects it immediately.
This is clean, deterministic, and avoids SQL Server‑style complexity.

🧬 4. Where security rules live
Citadel Security Center:
- defines policies
- issues capabilities
- creates DbHandles
- logs access
- enforces trust boundaries
- revokes handles when processes die
CQL Engine:
- checks the handle
- checks table metadata
- enforces allowed operations
- rejects forbidden actions
This is the perfect separation of concerns.

🛡️ 5. Why you don’t want SQL Server‑style internal security
SQL Server has:
- logins
- users
- roles
- schemas
- ownership chains
- row‑level security
- certificate‑based module signing
- impersonation
- contained users
- server‑level permissions
- database‑level permissions
- object‑level permissions
- column‑level permissions
It’s powerful, but it’s also a labyrinth.
You don’t want that inside Citadel.
You want one security brain.
Citadel Security Center is that brain.
CQL is the muscle.

🧠 6. This also makes CQL safer
Because CQL never:
- parses authentication
- stores passwords
- handles tokens
- manages sessions
- exposes network endpoints
It becomes:
- smaller
- easier to audit
- harder to exploit
- deterministic
- predictable
- aligned with Citadel’s capability model
This is exactly the right architecture for a secure OS‑embedded database.

🚀 7. And yes — this will eventually become a beast
But the right kind of beast.
A Citadel‑native database engine with:
- deterministic file format
- journaling
- indexes
- relationships
- CQL query language
- capability‑scoped access
- system‑table protection
- theme storage
- port manager storage
- capability storage
- service registry
- user profiles
- logs
- settings
All under one unified security model.
This is the foundation of a real OS.

======================================================================================================
API
======================================================================================================
CQL API (v0) — The Core Surface
This is the API the engine exposes.
Not the parser.
Not the query language.
Just the engine interface.
It’s intentionally small — because small is controllable, testable, and safe.

1. Database Lifecycle
namespace CQL {

struct DbHandle;
struct QueryResult;
struct Status;

Status openDatabase(const char* path, DbHandle& outHandle);
Status closeDatabase(DbHandle& handle);
Status flush(DbHandle& handle); // optional for v0
}


Notes:
- DbHandle is capability‑scoped (Citadel Security Center issues it).
- openDatabase does not bypass security — Citadel calls it with a pre‑validated handle.
- flush is optional but useful for theme editing or admin tools.

2. Table Operations
These are low‑level operations.
CQL v0 does not create tables dynamically — Citadel defines schemas at build time.
namespace CQL {

struct TableSchema {
    const char* name;
    const Column* columns;
    size_t columnCount;
};

Status createTable(DbHandle& db, const TableSchema& schema);
Status dropTable(DbHandle& db, const char* tableName);
}


Notes:
- createTable is only allowed for handles with canAdmin = true.
- In v0, you can hard‑code schemas for Themes and ThemeTokens.

3. Row Operations (Core CRUD)
These are the heart of the engine.
namespace CQL {

struct Row {
    std::vector<Cell> cells;
};

Status insert(DbHandle& db, const char* table, const Row& row);
Status update(DbHandle& db, const char* table, const Row& row, const Condition& where);
Status remove(DbHandle& db, const char* table, const Condition& where);
Status select(DbHandle& db, const SelectQuery& query, QueryResult& outResult);
}


Notes:
- Row is a vector of typed cells.
- Condition is a simple “column == value” for v0.
- SelectQuery supports:
- table name
- optional WHERE on primary key
- optional ORDER BY primary key
That’s enough for themes, tokens, and system tables.

4. Query Structures
Condition
struct Condition {
    const char* column;
    Cell value;
};


SelectQuery
struct SelectQuery {
    const char* table;
    std::optional<Condition> where;
    bool orderByPrimaryKey = false;
};


QueryResult
struct QueryResult {
    std::vector<Row> rows;
};



5. DbHandle (Security Boundary)
This is where Citadel Security Center injects capability rules.
struct TablePermission {
    const char* table;
    bool canRead;
    bool canWrite;
    bool canDelete;
    bool canAdmin;
};

struct DbHandle {
    Guid dbId;
    Guid callerProcessId;
    std::vector<TablePermission> permissions;
};


Enforcement:
Every CQL operation begins with:
if (!handle.permissions.allow(table, operation))
    return Status::AccessDenied;


This keeps CQL simple and Citadel in control.

6. Error / Status Model
enum class Status {
    Ok,
    NotFound,
    AccessDenied,
    InvalidArgument,
    SchemaMismatch,
    IoError,
    Corrupt,
    AlreadyExists,
    InternalError
};



7. How ThemeService Uses This API
Load theme metadata:
SelectQuery q { "Themes", Condition{"id", "Citadel.Dark"} };
QueryResult r;
cql::select(handle, q, r);


Load tokens:
SelectQuery q { "ThemeTokens", Condition{"themeId", "Citadel.Dark"} };


Update a token:
Row r;
r.cells = { {"themeId", "Citadel.Dark"}, {"token", "Accent"}, {"value", "#FF00AA"} };
cql::update(handle, "ThemeTokens", r, Condition{"token", "Accent"});


This is clean, deterministic, and easy to integrate.

=========================================================================================================================
File Format
=========================================================================================================================
CQL FILE FORMAT (v0) — THE ON‑DISK STRUCTURE
Think of this as the “Citadel Database File Layout Specification v0”.
It’s simple, predictable, and deterministic — exactly what Citadel needs.

1. File Layout Overview
+---------------------------+
| FileHeader                |
+---------------------------+
| TableDirectory            |
+---------------------------+
| TableSchemas              |
+---------------------------+
| PageRegion                |
|   Page 0                  |
|   Page 1                  |
|   Page 2                  |
|   ...                     |
+---------------------------+


Everything is page‑aligned.
Everything is deterministic.
Everything is forward‑compatible.

2. FileHeader (fixed size, 256 bytes)
struct FileHeader {
    char magic[8];        // "CQLDB\0\0"
    uint32_t version;     // v0 = 1
    uint32_t pageSize;    // 4096 recommended
    uint32_t tableCount;
    uint64_t tableDirOffset;
    uint64_t schemaOffset;
    uint64_t pageRegionOffset;
    uint8_t reserved[200];
};


Why this matters:
- Magic number prevents accidental misreads
- Versioning lets you evolve the format
- Page size is flexible
- Offsets allow future expansion

3. TableDirectory (variable size)
This is a list of all tables in the DB.
+-------------------------------+
| TableEntry (for table 0)      |
| TableEntry (for table 1)      |
| ...                           |
+-------------------------------+


TableEntry
struct TableEntry {
    char name[48];            // "Themes"
    uint64_t schemaOffset;    // points into SchemaRegion
    uint64_t rootPage;        // first page of table data
    uint32_t flags;           // system table, read-only, etc.
    uint8_t reserved[32];
};


Notes:
- rootPage is the entry point for the table’s B‑tree or row pages
- flags lets you mark system tables (only kernel can modify)

4. SchemaRegion
This stores the schema for each table.
+---------------------------+
| Schema for Themes         |
| Schema for ThemeTokens    |
| ...                       |
+---------------------------+


TableSchema (on disk)
struct ColumnDef {
    char name[48];
    uint8_t type;        // 0=TEXT, 1=INT, 2=BOOL, 3=DATETIME
    uint8_t isPrimaryKey;
    uint8_t reserved[14];
};

struct TableSchemaDisk {
    char tableName[48];
    uint32_t columnCount;
    ColumnDef columns[]; // variable length
};


Why store schemas on disk?
- You can evolve tables
- You can introspect the DB
- You can build admin tools
- You can validate rows at write time

5. PageRegion
This is where all table data lives.
Each page is fixed size (e.g., 4096 bytes).
+---------------------------+
| Page 0                    |
| Page 1                    |
| Page 2                    |
| ...                       |
+---------------------------+



6. Page Format (v0)
Each page contains:
struct PageHeader {
    uint32_t pageId;
    uint32_t tableId;
    uint32_t rowCount;
    uint32_t freeOffset;   // offset to next free byte
    uint8_t flags;         // leaf/internal/etc.
    uint8_t reserved[15];
};


Followed by:
[ RowOffset[rowCount] ]   // array of offsets to each row
[ RowData... ]            // variable-length rows



7. Row Format
Rows are stored as:
+---------------------------+
| uint32_t rowLength        |
| Cell[ columnCount ]       |
+---------------------------+


Cell format (TEXT example)
uint16_t type;      // matches schema
uint16_t length;    // length of value
char value[length]; // UTF-8


INT, BOOL, DATETIME are fixed-size.

8. Primary Key Index (v0)
For v0, keep it simple:
- Each table’s root page is a sorted array of rows by primary key
- Binary search is enough
- Later you can replace this with a B‑tree without changing the file format
This keeps v0 easy to implement and fast enough for themes, tokens, and system tables.

9. Write Strategy (v0)
Start simple:
- Write pages in place
- No journaling yet
- No transactions yet
- Flush on demand
Later you can add:
- WAL (write‑ahead log)
- MVCC
- Crash recovery
But v0 should be dead simple.

10. Why this format works
- Deterministic: no fragmentation, no random layout
- Expandable: you can add indexes, WAL, compression later
- Safe: schemas prevent malformed writes
- Fast: page‑aligned, binary searchable
- Citadel‑friendly: no external dependencies, no complexity explosion
This is the perfect foundation for a Citadel‑native database.
===========================================================================
In-Memory Structures (CQL ENGINE V0)
===========================================================================
SECTION 1 — IN‑MEMORY STRUCTURES (CQL ENGINE v0)
These are the core runtime objects the engine uses internally.
They mirror the file format but are optimized for fast access and clean logic.
We’ll break them into 7 subsystems:
- Database
- Table
- Schema
- Row / Cell
- Page
- Index (Primary Key)
- Handle / Permissions
Each subsystem has a single job.

1. Database (CQL::Database)
Responsibility:
Represents an open database file in memory.
Owns tables, schemas, page cache, and file handles.
struct Database {
    std::string path;
    FileHeader header;

    // Table name → Table object
    std::unordered_map<std::string, Table*> tables;

    // Page cache: pageId → Page*
    std::unordered_map<uint32_t, Page*> pageCache;

    // File descriptor or handle
    FileHandle file;

    // Page size (from header)
    uint32_t pageSize;
};


Why this works:
- Central authority for all DB operations
- Caches pages for performance
- Holds table directory and schemas
- Clean separation from query logic

2. Table (CQL::Table)
Responsibility:
Represents a single table’s metadata and entry point into its pages.
struct Table {
    std::string name;
    TableSchema schema;

    uint64_t rootPage;     // first page of table data
    uint32_t tableId;      // index in table directory
    uint32_t flags;        // system table, read-only, etc.

    // Primary key index (v0: sorted row offsets)
    PrimaryKeyIndex* pkIndex;
};


Why this works:
- Table knows its schema
- Table knows where its data starts
- Table owns its primary key index
- Keeps DB object clean

3. Schema (CQL::TableSchema)
Responsibility:
Defines the structure of rows in a table.
enum class ColumnType {
    Text,
    Int,
    Bool,
    DateTime
};

struct Column {
    std::string name;
    ColumnType type;
    bool isPrimaryKey;
};

struct TableSchema {
    std::string tableName;
    std::vector<Column> columns;

    int primaryKeyIndex = -1; // index in columns[]
};


Why this works:
- Schema validation becomes trivial
- Primary key lookup is fast
- Easy to serialize/deserialize

4. Row and Cell (CQL::Row / CQL::Cell)
Responsibility:
Represent a row in memory, independent of storage format.
struct Cell {
    ColumnType type;
    std::variant<std::string, int64_t, bool, int64_t> value;
};

struct Row {
    std::vector<Cell> cells;
};


Why this works:
- Clean separation from on‑disk encoding
- Easy to build from queries
- Easy to serialize into pages

5. Page (CQL::Page)
Responsibility:
Represents a single page of data loaded from disk.
struct PageHeader {
    uint32_t pageId;
    uint32_t tableId;
    uint32_t rowCount;
    uint32_t freeOffset;
    uint8_t flags;
};

struct Page {
    PageHeader header;
    std::vector<uint16_t> rowOffsets; // rowCount entries
    std::vector<uint8_t> data;        // raw page bytes
};


Why this works:
- Mirrors on‑disk layout
- Allows fast row access
- Supports future B‑tree expansion

6. Primary Key Index (CQL::PrimaryKeyIndex)
Responsibility:
Fast lookup of rows by primary key.
For v0, keep it simple:
struct PrimaryKeyIndex {
    // Sorted list of (primaryKeyValue, pageId, rowOffset)
    struct Entry {
        Cell key;
        uint32_t pageId;
        uint16_t rowOffset;
    };

    std::vector<Entry> entries;
};


Why this works:
- Binary search is enough for v0
- Easy to replace with B‑tree later
- No file format changes needed

7. DbHandle / Permissions (CQL::DbHandle)
Responsibility:
Security boundary between Citadel and CQL.
struct TablePermission {
    std::string table;
    bool canRead;
    bool canWrite;
    bool canDelete;
    bool canAdmin;
};

struct DbHandle {
    Guid dbId;
    Guid callerProcessId;
    std::vector<TablePermission> permissions;

    Database* db; // pointer to the open DB
};


Why this works:
- CQL never knows about users or roles
- Only checks permissions on operations
- Keeps security centralized in Citadel

SECTION 1 COMPLETE
You now have:
- A clean in‑memory model
- Perfect separation of responsibilities
- A structure that maps directly to the file format
- A foundation that can grow into a full relational engine
=======================================================================
Read/Write Code Paths
=======================================================================
Section 2 — Read/write paths (CQL v0)
Let’s wire the in‑memory model to the file format in the simplest possible way that still feels like a real engine.

1. Page loading path
Goal: Given pageId, get a Page* ready for use.
Steps:
- Check cache:
- If db.pageCache contains pageId → return it.
- Seek and read:
- Compute offset:
\mathrm{offset}=\mathrm{pageRegionOffset}+\mathrm{pageId}\cdot \mathrm{pageSize}
- Read pageSize bytes into a buffer.
- Decode header and row offsets:
- Fill PageHeader.
- Read rowOffsets array.
- Keep remaining bytes as data.
- Store in cache:
- Insert into db.pageCache[pageId].

2. Row deserialization path
Goal: Given Page and rowIndex, get a Row.
Steps:
- Get offset = page.rowOffsets[rowIndex].
- Read rowLength from page.data + offset.
- For each column in TableSchema:
- Read Cell:
- type → from schema
- for TEXT: length + bytes → std::string
- for INT/BOOL/DATETIME: fixed‑size read
- Build Row { cells }.

3. Row serialization path
Goal: Given Row, write it into a Page.
Steps:
- Serialize into a temporary buffer:
- rowLength
- each Cell encoded according to type.
- Check space:
- needed = sizeof(rowLength) + encodedCellsSize
- if page.header.freeOffset - needed < header + rowOffsets area → page full.
- Write:
- Decrement freeOffset by needed.
- Copy row bytes into page.data + freeOffset.
- Append freeOffset to rowOffsets.
- Increment rowCount.
- Mark page dirty for later flush.

4. SELECT path
Goal: select(DbHandle, SelectQuery, QueryResult&).
Steps:
- Permission check:
- Ensure canRead on query.table.
- Resolve table:
- Table* t = db.tables[query.table].
- If WHERE on primary key:
- Use t->pkIndex:
- binary search for key → (pageId, rowOffset).
- load page → deserialize row → push to QueryResult.
- Else (full scan v0):
- Iterate pages for table:
- load page
- for each row: deserialize → push to QueryResult.

5. INSERT path
Goal: insert(DbHandle, tableName, Row&).
Steps:
- Permission check: canWrite.
- Validate row vs schema:
- column count, types, primary key present.
- Find target page:
- For v0: append to last page of table; if full, allocate new page.
- Serialize row into page (as above).
- Update primary key index:
- Insert (key, pageId, rowOffset) into pkIndex.entries (keep sorted).
- Mark page dirty.

6. UPDATE path
Goal: update(DbHandle, tableName, Row&, Condition).
Assume v0: WHERE on primary key only.
Steps:
- Permission check: canWrite.
- Resolve row:
- Use pkIndex to find (pageId, rowOffset).
- Load page, deserialize row.
- Apply updates:
- For each column in Row, overwrite corresponding cell.
- Re‑serialize row:
- If new size ≤ old size:
- overwrite in place.
- Else (v0 simplification):
- mark old row as tombstone (optional).
- insert new row at end of same page or another page.
- update pkIndex entry.

7. DELETE path
Goal: remove(DbHandle, tableName, Condition).
Assume v0: WHERE on primary key only.
Steps:
- Permission check: canDelete.
- Resolve row via pkIndex.
- Load page.
- Mark row deleted:
- simplest v0: set a tombstone flag in row header or in a side bitmap.
- decrement logical rowCount if you want, but don’t reclaim space yet.
- Remove entry from pkIndex.entries.

8. FLUSH path
Goal: write dirty pages back to disk.
Steps:
- Iterate db.pageCache:
- for each dirty page:
- compute offset
- serialize header, rowOffsets, data back into a pageSize buffer
- write at offset.
- Optionally fsync.
==============================================================================
PAGE ALLOCASTOR (CQL V0)
==============================================================================
SECTION 3 — PAGE ALLOCATOR (CQL v0)
The allocator is responsible for:
- Creating new pages
- Tracking which pages belong to which table
- Finding a page with enough free space for a new row
- Growing tables by adding pages
- Maintaining deterministic page IDs
We’ll design it so you can later add:
- free‑page reuse
- B‑tree pages
- WAL logging
- compression
- multi‑version concurrency
…but v0 stays simple.

1. Page ID Strategy
Every page in the database has a unique, monotonically increasing pageId.
This is critical for determinism.
Page ID 0
Reserved for future metadata or system use.
Page IDs 1..N
Allocated sequentially as needed.
Why this works:
- No fragmentation
- No reuse complexity in v0
- Easy to debug
- Easy to expand later

2. Table → Page Mapping
Each table needs to know which pages belong to it.
In v0, we keep it simple:
struct Table {
    std::string name;
    TableSchema schema;

    uint64_t rootPage;              // first page of table
    std::vector<uint32_t> pages;    // all pages for this table
    PrimaryKeyIndex* pkIndex;
};


How it works:
- rootPage is the first page
- pages is the full list
- When a new page is allocated for this table, append to pages
Later, you can replace this with a B‑tree root.

3. Page Allocation API
Here’s the allocator interface:
struct PageAllocator {
    static Page* allocatePage(Database& db, uint32_t tableId);
    static Page* getPage(Database& db, uint32_t pageId);
};


Responsibilities:
- allocatePage:
- assign new pageId
- create empty page
- write header
- append to table’s page list
- insert into page cache
- getPage:
- load from cache or disk

4. allocatePage Implementation
Steps:
- Compute new pageId:
uint32_t pageId = db.header.nextPageId++;
- Create an empty Page object:
Page* p = new Page();
p->header.pageId = pageId;
p->header.tableId = tableId;
p->header.rowCount = 0;
p->header.freeOffset = db.pageSize; // grows downward
- Initialize rowOffsets vector:
p->rowOffsets.clear();
- Insert into cache:
db.pageCache[pageId] = p;
- Append to table’s page list:
table.pages.push_back(pageId);
- Write empty page to disk immediately (optional but safe):
- ensures crash consistency
- makes debugging easier

5. Finding a Page for INSERT
This is the allocator’s most important job.
v0 Strategy:
- Always try the last page of the table first
- If it has enough space → use it
- If not → allocate a new page
Function:
Page* findPageForInsert(Database& db, Table& table, size_t rowSizeNeeded);


Steps:
- If table.pages is empty → allocate first page.
- Load last page:
Page* p = getPage(db, table.pages.back());
- Check free space:
if (p->header.freeOffset - rowSizeNeeded >= headerSize + rowOffsetsSize)
    return p;
- Else:
- allocate new page
- return it
Why this works:
- Simple
- Fast
- Deterministic
- No fragmentation
- Easy to evolve into B‑tree leaf pages later

6. Page Growth Rules
A page is considered “full” when:
freeOffset < (headerSize + rowOffsetsSize + minimumRowSize)


v0 does NOT:
- reclaim space
- compact pages
- reuse deleted row space
That’s fine — themes, tokens, and system tables are tiny.
Later you can add:
- free lists
- compaction
- vacuuming
- page splitting

7. Table Growth Rules
v0:
- Tables grow linearly: page 1, page 2, page 3…
v1:
- Replace with B‑tree
- rootPage becomes internal node
- pages become leaves
The beauty of this design is that v0 and v1 share the same file format.

8. Page Flushing
When a page is dirty:
- Serialize header
- Serialize rowOffsets
- Serialize row data
- Write to disk at:
offset = pageRegionOffset + pageId * pageSize


Optional:
- fsync for durability
- WAL for crash safety
================================================================================
Primary Key Indexing (CQL v0)
===============================================================================

SECTION 4 — PRIMARY KEY INDEXING (CQL v0)
The primary key index is responsible for:
- Fast lookup of rows by primary key
- Maintaining sorted order
- Supporting inserts, updates, and deletes
- Staying in memory for speed
- Being rebuildable from disk
In v0, the index is in‑memory only and rebuilt at database load.
In v1, you can persist it or replace it with a B‑tree.

1. Index Structure
You already defined the in‑memory structure:
struct PrimaryKeyIndex {
    struct Entry {
        Cell key;           // primary key value
        uint32_t pageId;    // where the row lives
        uint16_t rowOffset; // offset inside the page
    };

    std::vector<Entry> entries; // always sorted by key
};


Why this works:
- Simple
- Fast
- Deterministic
- Perfect for theme tables and system tables
- Easy to evolve

2. Building the Index at Database Load
When the DB opens:
- For each table:
- For each page in table.pages:
- For each row in the page:
- Deserialize row header
- Extract primary key cell
- Insert into pkIndex.entries
After scanning all pages:
std::sort(pkIndex.entries.begin(), pkIndex.entries.end(),
          [](auto& a, auto& b) { return a.key < b.key; });


Why rebuild?
- Guarantees consistency
- Avoids storing index on disk in v0
- Makes crash recovery trivial

3. Lookup Path (SELECT WHERE PK = X)
This is the fast path.
Steps:
- Binary search pkIndex.entries for the key.
- If found:
- Load page via allocator
- Deserialize row
- Return it
- If not found:
- Return NotFound
Complexity:
- O(log n) lookup
- O(1) page load (cached)
- O(1) row decode
This is extremely fast for small/medium tables.

4. Insert Path (Updating the Index)
After inserting a row into a page:
- Extract primary key from the Row.
- Create an Entry { key, pageId, rowOffset }.
- Insert into entries while maintaining sorted order.
Implementation:
auto it = std::lower_bound(entries.begin(), entries.end(), newEntry,
    [](auto& a, auto& b) { return a.key < b.key; });

entries.insert(it, newEntry);


Why sorted insert?
- Keeps binary search valid
- No need to rebuild index
- Deterministic ordering

5. Update Path (Primary Key Unchanged)
If the primary key does not change:
- Find entry via binary search
- Update pageId and rowOffset if row moved
- Done
If the row was rewritten to a new page:
- Update the entry’s location
- No need to re-sort

6. Update Path (Primary Key Changed)
If the primary key changes:
- Remove old entry
- Insert new entry (sorted)
- Update row in page
Why this matters:
- Primary key is the identity
- Changing it is rare but must be supported

7. Delete Path
When deleting a row:
- Binary search for key
- Remove entry from entries
- Mark row as tombstone in page
Why tombstones?
- Avoids page compaction in v0
- Keeps page offsets stable
- Makes deletes cheap
Later you can add:
- free space reclamation
- page compaction
- vacuuming

8. Persisting the Index (v1)
In v0, the index is in-memory only.
In v1, you can persist it by:
- writing a compact index page
- storing a B‑tree root page
- journaling index updates
But the file format we designed already supports this evolution.

9. Why This Index Design Is Perfect for Citadel
- Deterministic
- Simple
- Fast
- Easy to debug
- Easy to evolve
- No hidden complexity
- No SQL Server‑style overhead
- Perfect for system tables like Themes, Tokens, Ports, Capabilities
This is exactly the right foundation for a Citadel‑native database.

=============================================================================
SECTION 5:  QUERY EXECUTION LAYER (CQL V0)
=============================================================================
SECTION 5 — QUERY EXECUTION LAYER (CQL v0)
The Query Execution Layer (QEL) is responsible for:
- Validating the request
- Checking permissions
- Resolving the table
- Using the index (if applicable)
- Reading/writing pages
- Returning results or errors
It does not:
- parse text queries (that’s the parser layer, later)
- manage transactions (v1)
- manage concurrency (v1)
- manage journaling (v1)
This keeps v0 small and rock‑solid.

1. Execution Flow Overview
Every operation follows the same high‑level flow:
validate handle →
validate table →
validate permissions →
validate schema →
execute operation →
return result


This keeps the engine deterministic and predictable.

2. SELECT Execution Path
Function:
Status select(DbHandle& h, const SelectQuery& q, QueryResult& out);


Steps:
A. Permission check
if (!h.canRead(q.table)) return AccessDenied;


B. Resolve table
Table* t = h.db->tables[q.table];
if (!t) return NotFound;


C. WHERE on primary key
If q.where exists and targets the primary key:
- Binary search t->pkIndex.entries
- If found:
- load page
- deserialize row
- push to out.rows
- If not found:
- return Ok with empty result
D. Full table scan (v0)
If no WHERE clause:
- For each page in t->pages:
- Load page
- For each row:
- skip tombstones
- deserialize
- push to result
E. Return result
return Status::Ok;



3. INSERT Execution Path
Function:
Status insert(DbHandle& h, const char* table, const Row& row);


Steps:
A. Permission check
if (!h.canWrite(table)) return AccessDenied;


B. Resolve table
Table* t = h.db->tables[table];


C. Schema validation
- correct number of columns
- correct types
- primary key present
D. Serialize row to buffer
Compute rowSizeNeeded.
E. Find page
Page* p = findPageForInsert(db, *t, rowSizeNeeded);


F. Write row
- update p->rowOffsets
- update p->freeOffset
- increment rowCount
- mark page dirty
G. Update primary key index
Sorted insert into t->pkIndex.entries.
H. Return
return Status::Ok;



4. UPDATE Execution Path
Function:
Status update(DbHandle& h, const char* table, const Row& row, const Condition& where);


Assumption: v0 supports WHERE on primary key only.
Steps:
A. Permission check
if (!h.canWrite(table)) return AccessDenied;


B. Resolve table
Table* t = h.db->tables[table];


C. Validate WHERE
Must target primary key.
D. Lookup row
Binary search in pkIndex.
If not found → NotFound.
E. Load page and deserialize row
F. Apply updates
Overwrite cells in memory.
G. Re‑serialize
Two cases:
Case 1: new row fits in old space
- overwrite in place
- update index if row moved
Case 2: new row is larger
- mark old row as tombstone
- insert new row (like INSERT)
- update index entry
H. Return
return Status::Ok;



5. DELETE Execution Path
Function:
Status remove(DbHandle& h, const char* table, const Condition& where);


Steps:
A. Permission check
if (!h.canDelete(table)) return AccessDenied;


B. Validate WHERE
Primary key only.
C. Lookup row
Binary search in pkIndex.
D. Load page
Mark row as tombstone.
E. Remove index entry
Erase from pkIndex.entries.
F. Return
return Status::Ok;



6. Error Handling Strategy
Every operation returns a Status:
- Ok
- NotFound
- AccessDenied
- InvalidArgument
- SchemaMismatch
- IoError
- Corrupt
- InternalError
Errors never throw exceptions.
Everything is explicit and deterministic.

7. Why This Execution Layer Works
- Predictable: no hidden behavior
- Deterministic: same input → same output
- Secure: permissions checked first
- Simple: v0 avoids complexity traps
- Expandable: v1 can add:
- joins
- multi‑column WHERE
- B‑tree indexes
- transactions
- WAL
- query planner
This is exactly the right foundation for a Citadel‑native database engine.

====================================================================
Section 6 Schema Validation & Type System
====================================================================
SECTION 6 — SCHEMA VALIDATION & TYPE SYSTEM (CQL v0)
This section covers:
- The type system
- Column validation
- Row validation
- Primary key validation
- Condition validation
- Serialization validation
- Error reporting
Everything here is designed to be small, predictable, and easy to extend.

1. Type System (CQL::ColumnType)
CQL v0 supports four fundamental types:
enum class ColumnType {
    Text,      // UTF-8 string
    Int,       // 64-bit signed
    Bool,      // 1 byte
    DateTime   // 64-bit epoch or CitadelTime
};


Why these four?
- They cover 95% of system-table needs
- They serialize cleanly
- They are deterministic
- They map directly to Citadel’s internal types
Later you can add:
- Float
- Blob
- Enum
- JSON
- Foreign keys
But v0 stays lean.

2. Column Validation
Each column has:
struct Column {
    std::string name;
    ColumnType type;
    bool isPrimaryKey;
};


Validation rules:
- Column names must be unique
- Exactly one primary key per table
- Primary key cannot be TEXT unless you explicitly allow it
- Primary key cannot be NULL
- Column types must match schema

3. Row Validation
Before any INSERT or UPDATE, the engine validates the row:
A. Column count
if (row.cells.size() != schema.columns.size())
    return Status::SchemaMismatch;


B. Type matching
For each column:
if (row.cells[i].type != schema.columns[i].type)
    return Status::SchemaMismatch;


C. Primary key present
if (schema.primaryKeyIndex < 0)
    return Status::SchemaMismatch;


D. Primary key non-null
if (row.cells[pkIndex].isNull())
    return Status::InvalidArgument;


E. Primary key uniqueness
Binary search the index:
- If key exists → AlreadyExists
- Else → OK

4. Condition Validation (WHERE clause)
v0 supports:
- WHERE on primary key only
- Equality only
struct Condition {
    const char* column;
    Cell value;
};


Validation:
- Column must exist
- Column must be primary key
- Type must match schema
If any fail → InvalidArgument.

5. Serialization Validation
Before writing a row to disk:
A. Compute encoded size
- TEXT: 2 bytes length + N bytes
- INT: 8 bytes
- BOOL: 1 byte
- DATETIME: 8 bytes
If encoded size > page free space → page is full.
B. Validate UTF‑8 for TEXT
Optional but recommended.
C. Validate integer ranges
Optional for v0.
D. Validate DateTime format
Optional for v0.

6. Error Reporting
Schema validation errors must be explicit and deterministic:
- SchemaMismatch → wrong number of columns or wrong types
- InvalidArgument → invalid primary key, invalid WHERE
- AlreadyExists → duplicate primary key
- NotFound → WHERE key not found
- AccessDenied → permission failure
This keeps debugging clean and predictable.

7. Why This Layer Matters
This is the layer that prevents:
- corrupted pages
- invalid rows
- mismatched types
- broken indexes
- malformed writes
- inconsistent schemas
It ensures that every row on disk is valid, which is the foundation for:
- safe queries
- safe updates
- safe indexing
- safe page compaction (later)
- safe journaling (later)
- safe replication (much later)
This is exactly how a real database protects itself.

==================================================================
SECTION 7 - CQL Parser Layer (CQL V0)
==================================================================

SECTION 7 — CQL PARSER LAYER (CQL v0)
The parser layer has three responsibilities:
- Tokenize the input string
- Parse tokens into a structured AST
- Validate the AST against the schema
We’ll define:
- the grammar
- the tokenizer
- the AST structures
- the parser functions
- the validation rules
This is the smallest possible language that still feels like SQL.

1. CQL v0 Grammar
We support four statements:
SELECT
SELECT <columns> FROM <table> [WHERE <column> = <value>];


INSERT
INSERT INTO <table> VALUES (<value1>, <value2>, ...);


UPDATE
UPDATE <table> SET <column> = <value> [WHERE <pk> = <value>];


DELETE
DELETE FROM <table> WHERE <pk> = <value>;


Notes:
- WHERE is primary key only in v0
- No joins
- No ORDER BY except implicit PK ordering
- No functions
- No expressions
This keeps the parser tiny and deterministic.

2. Tokenizer (Lexical Analyzer)
The tokenizer breaks the input into tokens:
SELECT
IDENT("Themes")
COMMA
STRING("Citadel.Dark")
EQUAL
LPAREN
RPAREN
SEMICOLON


Token types:
enum class TokenType {
    Identifier,
    StringLiteral,
    NumberLiteral,
    KeywordSelect,
    KeywordInsert,
    KeywordUpdate,
    KeywordDelete,
    KeywordFrom,
    KeywordWhere,
    KeywordInto,
    KeywordValues,
    KeywordSet,
    Comma,
    Equal,
    LParen,
    RParen,
    Semicolon,
    EndOfInput
};


Token structure:
struct Token {
    TokenType type;
    std::string text;
};


Tokenizer rules:
- Identifiers: [A-Za-z_][A-Za-z0-9_]*
- Strings: "..." (UTF‑8)
- Numbers: -?[0-9]+
- Keywords: case‑insensitive
- Whitespace: ignored
- Semicolon: optional but recommended

3. AST Structures (Abstract Syntax Tree)
These map directly to your engine’s query structs.
SELECT AST
struct SelectAST {
    std::vector<std::string> columns; // "*" or explicit list
    std::string table;
    std::optional<Condition> where;
};


INSERT AST
struct InsertAST {
    std::string table;
    std::vector<Cell> values;
};


UPDATE AST
struct UpdateAST {
    std::string table;
    std::vector<std::pair<std::string, Cell>> assignments;
    std::optional<Condition> where;
};


DELETE AST
struct DeleteAST {
    std::string table;
    Condition where;
};


Unified AST
using AST = std::variant<SelectAST, InsertAST, UpdateAST, DeleteAST>;



4. Parser Functions
Each statement has a dedicated parser:
AST parseSelect(const std::vector<Token>&);
AST parseInsert(const std::vector<Token>&);
AST parseUpdate(const std::vector<Token>&);
AST parseDelete(const std::vector<Token>&);


SELECT parsing flow:
- Expect SELECT
- Parse column list or *
- Expect FROM
- Parse table name
- Optional WHERE:
- Expect WHERE
- Parse <column> = <value>
- Optional semicolon
INSERT parsing flow:
- Expect INSERT INTO
- Parse table name
- Expect VALUES
- Parse parenthesized value list
UPDATE parsing flow:
- Expect UPDATE
- Parse table name
- Expect SET
- Parse assignments
- Optional WHERE
DELETE parsing flow:
- Expect DELETE FROM
- Parse table name
- Expect WHERE
- Parse <pk> = <value>

5. AST → Engine Query Conversion
After parsing, you convert AST → engine structs.
SELECT
SelectQuery q;
q.table = ast.table;
q.where = ast.where;


INSERT
Row r;
r.cells = ast.values;


UPDATE
Row r;
for each assignment:
    r.cells[columnIndex] = value;


DELETE
Condition c = ast.where;



6. Parser Validation Layer
This is where you enforce:
- table exists
- columns exist
- types match schema
- primary key WHERE only
- correct number of values in INSERT
- correct columns in UPDATE
This layer prevents malformed queries from reaching the engine.

7. Why This Parser Design Works
- Small: ~500 lines of code
- Deterministic: no ambiguity
- Predictable: no hidden grammar rules
- Safe: validated before execution
- Expandable: you can add:
- ORDER BY
- LIMIT
- JOIN
- functions
- expressions
- subqueries
But v0 stays lean and Citadel‑friendly.

================================================================
Section 8: System Tables (Themes, Tokens, Capabilities  (CQL V0)
================================================================
SECTION 8 — SYSTEM TABLES (CQL v0)
We’ll define three foundational system tables:
- Themes — metadata about each theme
- ThemeTokens — the actual style tokens
- Capabilities — Citadel’s security capabilities (future use)
These are the first real tables that will live inside CQL.
They are intentionally simple, but structured so they can grow into a full relational model later.

1. Table: Themes
This table stores high‑level metadata about each theme.
Schema:
|  |  |  |  | 
|  |  |  |  | 
|  |  |  |  | 
|  |  |  |  | 
|  |  |  |  | 
|  |  |  |  | 
|  |  |  |  | 
|  |  |  |  | 


Purpose:
- Theme discovery
- Theme metadata
- Theme inheritance
- Versioning
- UI theme selector
Example row:
id = "Citadel.Dark"
name = "Citadel Dark"
author = "Citadel Team"
version = 1
isDark = true
baseTheme = ""
createdAt = 1712870400



2. Table: ThemeTokens
This table stores the actual style tokens that define a theme.
Schema:
|  |  |  |  | 
|  |  |  |  | 
|  |  |  |  | 
|  |  |  |  | 


Composite Primary Key:
(themeId, token)
Purpose:
- Defines the entire CitadelStyle struct
- Allows theme inheritance
- Allows dynamic editing
- Allows live preview
- Allows user overrides
Example rows:
themeId = "Citadel.Dark", token = "Accent", value = "#FF00AA"
themeId = "Citadel.Dark", token = "Background", value = "#101010"
themeId = "Citadel.Dark", token = "TextPrimary", value = "#FFFFFF"



3. Table: Capabilities (future use)
This table will eventually power:
- the port manager
- the security center
- the process capability model
- the TPM‑like trust core
Schema:
|  |  |  |  | 
|  |  |  |  | 
|  |  |  |  | 
|  |  |  |  | 
|  |  |  |  | 
|  |  |  |  | 


Purpose:
- Capability issuance
- Capability revocation
- Auditing
- Security enforcement
Example row:
id = "A1B2C3D4"
type = "Network.OpenPort"
scope = "TCP:443"
issuedTo = "Process:WebServer"
expiresAt = 1713000000



4. Why These Three Tables Matter
These tables give Citadel:
A. A real theme engine
- Query themes
- Query tokens
- Build CitadelStyle
- Support inheritance
- Support dynamic editing
- Support theme packs
B. A real security model
Capabilities become first‑class citizens in the OS.
C. A real system registry
This is the beginning of Citadel’s “system hive”.

5. How These Tables Integrate With the Engine
ThemeService
- SELECT from Themes
- SELECT from ThemeTokens
- Build style structs
- Broadcast theme change
SecurityCenter
- INSERT into Capabilities
- DELETE on revocation
- SELECT for auditing
PortManager
- SELECT capabilities for a process
- Validate port open requests
Future: Service Registry
- Tables for services, dependencies, states

6. Why This Is the Right Starting Set
Because these tables:
- are small
- are frequently queried
- are safe to mutate
- are foundational
- exercise all CRUD paths
- exercise indexing
- exercise schema validation
- exercise the parser
- exercise the security model
They are the perfect proving ground for CQL v0.

============================================================
Section 9 - Integration with Citadel RunTime (CQL V0)
============================================================
SECTION 9 — INTEGRATION WITH CITADEL RUNTIME

1. ThemeService Integration
ThemeService is the first real consumer of CQL, but it is not the end goal.
It is the proving ground for the larger desktop-runtime move.
It uses CQL for:
- theme discovery
- theme metadata
- token lookup
- theme inheritance
- dynamic theme editing
- live preview
A. Loading a theme
SelectQuery q;
q.table = "Themes";
q.where = Condition{"id", themeId};

QueryResult meta;
cql::select(handle, q, meta);


If no rows → theme not found.
B. Loading tokens
SelectQuery q;
q.table = "ThemeTokens";
q.where = Condition{"themeId", themeId};

QueryResult tokens;
cql::select(handle, q, tokens);


C. Building CitadelStyle
ThemeService converts tokens → CitadelStyle:
- Colors
- Typography
- Spacing
- Shadows
- Component tokens
D. Theme inheritance
If baseTheme is set:
- Load base theme tokens
- Load child theme tokens
- Merge (child overrides base)
E. Dynamic editing
When the user edits a theme:
UPDATE ThemeTokens
SET value = "#FF00AA"
WHERE themeId = "Citadel.Dark" AND token = "Accent";


ThemeService then:
- rebuilds the style
- broadcasts a theme‑changed event
- UI updates instantly
F. Why CQL is perfect for themes
- No more file-backed theme truth at runtime
- Less repeated parse/materialize work on normal boots
- No more duplicated logic
- No more brittle theme switching
- Full introspection
- Full editability
- Full versioning
This is the first subsystem that truly proves the value of CQL.

1a. Desktop Runtime Integration After ThemeService
After ThemeService is stable, the next consumer is the desktop runtime itself.
The intended direction is:
- external `.json` / `.cml` files remain as import/bootstrap assets
- imported layout/control/theme-binding data is materialized into CQL tables
- normal desktop boot reads those tables first

That means the database model must be able to express:
- layout regions
- control hierarchy
- theme-token bindings
- asset references
- relationships between desktop objects

This is why relational support is not optional polish for Citadel.
For the desktop/runtime use case, it is part of the core engine shape.

2. SecurityCenter Integration
SecurityCenter uses CQL for:
- capability issuance
- capability revocation
- capability lookup
- auditing
- persistence across reboots
A. Issuing a capability
INSERT INTO Capabilities VALUES (
    "A1B2C3D4",
    "Network.OpenPort",
    "TCP:443",
    "Process:WebServer",
    1713000000
);


B. Revoking a capability
DELETE FROM Capabilities WHERE id = "A1B2C3D4";


C. Checking capabilities for a process
SELECT * FROM Capabilities WHERE issuedTo = "Process:WebServer";


D. Why CQL is perfect for capabilities
- Capabilities become first‑class OS objects
- Easy to audit
- Easy to revoke
- Easy to query
- Easy to persist
- Easy to integrate with PortManager
This is the beginning of Citadel’s TPM‑like trust core.

3. PortManager Integration
PortManager uses CQL to enforce capability‑driven networking.
A. When a process requests to open a port
PortManager queries:
SELECT * FROM Capabilities
WHERE issuedTo = "<process>"
AND type = "Network.OpenPort"
AND scope = "TCP:443";


If no rows → deny.
If row exists → allow.
B. When a process closes a port
No DB change needed unless you want to log it.
C. When a capability expires
SecurityCenter deletes it → PortManager automatically denies future requests.
D. Why CQL is perfect for PortManager
- No ad‑hoc config files
- No in‑memory only state
- No fragile permission checks
- Everything is explicit and queryable
This is how Citadel becomes a capability‑driven OS.

4. How DbHandles Are Issued
Citadel SecurityCenter issues a DbHandle to each subsystem:
ThemeService handle
- canRead Themes
- canRead ThemeTokens
- canWrite ThemeTokens (for editing)
- cannot write Themes (metadata is protected)
SecurityCenter handle
- full admin on Capabilities
- read‑only on Themes (for UI)
- no access to ThemeTokens
PortManager handle
- read‑only on Capabilities
- no access to Themes or Tokens
This keeps the engine secure and deterministic.

5. Why This Integration Model Works
Because it gives you:
A. One source of truth
No more scattered files.
No more duplicated logic.
No more inconsistent state.
B. Deterministic behavior
Every subsystem queries the same database.
Every decision is reproducible.
C. Capability‑driven security
CQL never decides who can do what.
Citadel SecurityCenter does.
D. Extensibility
You can add:
- ServiceRegistry
- UserProfiles
- Settings
- Logs
- Packages
- Sessions
All using the same engine.

==============================================================
Section 10 CQL ENGINE BOOT SEQUENCE (CQL V0)
==============================================================
SECTION 10 — CQL ENGINE BOOT SEQUENCE
The boot sequence has six phases, each with a single responsibility:
- Open the database file
- Validate the file header
- Load the table directory
- Load schemas
- Load pages and rebuild indexes
- Initialize system tables (create if missing)
This sequence guarantees that when Citadel finishes booting, CQL is in a known-good state.
Let’s break down each phase.

1. Phase One — Open the Database File
Steps:
- Open file handle
- Read the first 256 bytes
- Interpret as FileHeader
Validation:
- magic == "CQLDB\0\0"
- version == supportedVersion
- pageSize is sane (e.g., 4096)
- offsets are within file bounds
If any fail → Corrupt or InvalidFormat.
Why this matters:
This prevents Citadel from booting with a corrupted DB.

2. Phase Two — Validate Header & Layout
Check:
- tableDirOffset < file size
- schemaOffset < file size
- pageRegionOffset < file size
- tableCount is reasonable
If anything is out of range → reject the DB.
Why this matters:
A malformed header is a sign of corruption or tampering.

3. Phase Three — Load Table Directory
Seek to tableDirOffset.
For each table:
- Read TableEntry
- Store:
- table name
- schema offset
- root page
- flags
Populate:
db.tables[name] = new Table();


Why this matters:
This is the “map” of the database.

4. Phase Four — Load Schemas
For each table:
- Seek to schemaOffset
- Read TableSchemaDisk
- Convert to in‑memory TableSchema
Validate:
- exactly one primary key
- column names unique
- column types valid
Why this matters:
Schema mismatches are catastrophic — this prevents them.

5. Phase Five — Load Pages & Rebuild Indexes
This is the most important part of the boot sequence.
For each table:
- Start at rootPage
- Load page
- For each row:
- skip tombstones
- deserialize
- extract primary key
- append to pkIndex.entries
After scanning all pages:
sort(pkIndex.entries.begin(), pkIndex.entries.end());


Why rebuild indexes?
Because v0 keeps indexes in memory only.
Rebuilding ensures:
- consistency
- crash recovery
- no stale pointers
- no corrupted index state
This is exactly how SQLite does it for some structures.

6. Phase Six — Initialize System Tables
If this is the first boot, or if system tables are missing, create them.
Required system tables:
- Themes
- ThemeTokens
- Capabilities
Steps:
- Check if table exists in directory
- If not:
- create schema
- allocate first page
- write empty table
- update table directory
- flush
Why this matters:
Citadel must always have a working theme system and capability system.

7. Optional Phase — Integrity Check (v1)
Later you can add:
- page checksum validation
- row checksum validation
- schema hash validation
- orphaned page detection
But v0 keeps it simple.

8. Boot Sequence Summary
Here’s the entire sequence in one list:
1. Open file
2. Validate header
3. Load table directory
4. Load schemas
5. Load pages
6. Rebuild primary key indexes
7. Initialize system tables
8. Engine ready


This is deterministic, predictable, and safe.

9. Why This Boot Sequence Works for Citadel
- Fast: small DB, small tables, small indexes
- Deterministic: same DB → same state
- Crash‑resistant: index rebuild ensures consistency
- Secure: system tables always exist
- Extensible: journaling, WAL, MVCC can be added later
This is the perfect foundation for a real OS‑embedded database.

*******************************************************************
CQL V0 ends here  and CQL V1 starts
*******************************************************************
====================================================================
Section 11 - Transactions & Journaling (CQL V1) 
====================================================================
SECTION 11 — TRANSACTIONS & JOURNALING (CQL v1)
CQL v0 is safe as long as the process doesn’t crash mid‑write.
CQL v1 introduces:
- atomic writes
- crash recovery
- write‑ahead logging (WAL)
- transaction boundaries
- page‑level durability
- rollback on failure
We’ll keep it small and deterministic — no MVCC, no multi‑version snapshots, no complex locking.
This is SQLite‑style WAL, but cleaner and tailored to Citadel.

1. Goals of the Journaling System
CQL v1 must guarantee:
A. Atomicity
A transaction either fully applies or doesn’t apply at all.
B. Durability
Once committed, data survives crashes.
C. Crash Safety
If Citadel loses power mid‑write, the DB is still valid.
D. Determinism
Recovery always produces the same result.
E. Simplicity
No multi‑threaded locking, no MVCC, no row‑level logs.
This is the perfect durability model for an OS‑embedded database.

2. WAL File Layout
We introduce a companion file:
<database>.cdb
<database>.cdb-wal


The WAL file contains:
+---------------------------+
| WALHeader                 |
+---------------------------+
| WALFrame[0]               |
| WALFrame[1]               |
| WALFrame[2]               |
| ...                       |
+---------------------------+


WALHeader
struct WALHeader {
    char magic[8];        // "CQLWAL\0"
    uint32_t version;     // 1
    uint32_t pageSize;
    uint64_t lastCommittedTxn;
};


WALFrame
Each frame represents a single page write:
struct WALFrame {
    uint64_t txnId;
    uint32_t pageId;
    uint32_t payloadSize; // always == pageSize
    uint8_t  data[];      // raw page bytes
};



3. Transaction Model (v1)
CQL v1 supports single‑writer, multi‑reader semantics.
Transaction boundaries:
beginTransaction();
insert/update/delete...
commitTransaction();


Rules:
- Only one write transaction at a time
- Reads never block
- Writes go to WAL, not the DB file
- Commit = flush WAL + checkpoint
- Crash during commit = recover from WAL
This is the simplest safe model.

4. Write Path With WAL
v0 write path:
- Write page directly to DB file
- Risk of corruption if crash occurs mid‑write
v1 write path:
- Serialize page into memory
- Append WALFrame to WAL file
- fsync WAL
- Mark page dirty in memory
- On commit → checkpoint WAL into DB file
Why this works:
- WAL is append‑only → crash‑safe
- DB file is only updated during checkpoint
- Partial writes never corrupt DB

5. Commit Path (Checkpointing)
When commitTransaction() is called:
- Lock DB for writing
- For each WALFrame in the current txn:
- write page to DB file at pageId * pageSize
- fsync DB file
- Update WAL header lastCommittedTxn
- fsync WAL
- Truncate WAL (optional)
- Unlock DB
Crash scenarios:
- Crash before checkpoint → WAL replays
- Crash during checkpoint → WAL replays
- Crash after checkpoint → WAL is empty
This is deterministic and safe.

6. Recovery Path (On Boot)
During boot (Section 10), add:
A. Check for WAL file
If exists and non‑empty → recovery needed.
B. Read WALHeader
Get lastCommittedTxn.
C. Replay WALFrames
For each frame:
- Write page to DB file
- Overwrite existing page
D. fsync DB file
E. Truncate WAL
F. Continue normal boot
This guarantees the DB is always valid.

7. Rollback Path
Rollback is trivial:
- Discard uncommitted WAL frames
- Do not checkpoint
- WAL remains unchanged
- No DB file writes occur
This is why WAL is so powerful.

8. Page Versioning (Optional v1.1)
You can add:
- per‑page version numbers
- per‑txn page lists
- incremental checkpoints
But v1 doesn’t need it.

9. Why WAL Is the Right Choice for Citadel
A. Deterministic
Append‑only log → no partial writes.
B. Fast
Writes are sequential.
C. Crash‑safe
Recovery is guaranteed.
D. Simple
No MVCC, no complex locking.
E. Extensible
You can add:
- savepoints
- nested transactions
- read snapshots
- incremental checkpoints
All without changing the file format.

10. Summary of CQL v1 Durability
Writes → WAL
Commit → checkpoint WAL → DB
Crash → replay WAL
Rollback → discard WAL frames


This is the exact durability model used by:
- SQLite
- LMDB (variant)
- Many embedded engines
But tailored to Citadel’s deterministic architecture.

================================================================
Section 12 SQL Runtime API Integration
================================================================
SECTION 12 — CQL RUNTIME API INTEGRATION
This section defines how Citadel processes interact with CQL through the runtime, not directly with the engine.
We’ll break it into:
- The CQL Runtime Service
- Handle issuance
- Capability enforcement
- Process isolation
- Query execution flow
- Error propagation
- Integration with Citadel subsystems
- Why this model works
This is the “operating system boundary” for the database.

1. The CQL Runtime Service
CQL runs as a dedicated Citadel service, not a library linked into every process.
Why?
- Centralized state
- Centralized WAL
- Centralized locking
- Centralized page cache
- Centralized security enforcement
- No process can corrupt the DB
The runtime exposes:
DbHandle openHandle(ProcessId pid, CapabilitySet caps);
Status closeHandle(DbHandle&);
Status execute(DbHandle&, AST&, QueryResult&);


This is the only public interface.
Everything else is internal.

2. Handle Issuance (Security Boundary)
When a process wants to access the database:
- It requests a handle from the CQL Runtime Service.
- The runtime asks SecurityCenter:
- “What capabilities does this process have?”
- SecurityCenter returns a capability set.
- Runtime converts capabilities → table permissions.
- Runtime returns a DbHandle scoped to that process.
Example:
ThemeService gets:
- READ Themes
- READ ThemeTokens
- WRITE ThemeTokens
PortManager gets:
- READ Capabilities
SecurityCenter gets:
- ADMIN Capabilities
This is the heart of Citadel’s capability‑driven model.

3. Capability Enforcement
Every query execution begins with:
if (!handle.permissions.allow(table, operation))
    return Status::AccessDenied;


CQL never checks:
- user identity
- roles
- passwords
- tokens
- trust levels
It only checks capabilities.
This keeps the engine clean and the security model centralized.

4. Process Isolation
Each handle is tied to a process:
struct DbHandle {
    Guid dbId;
    ProcessId callerProcessId;
    std::vector<TablePermission> permissions;
    Database* db;
};


Runtime guarantees:
- A handle cannot be shared across processes
- A handle becomes invalid when the process dies
- A process cannot escalate permissions
- A process cannot open arbitrary tables
This prevents cross‑process contamination.

5. Query Execution Flow Through the Runtime
When a process sends a query:
Process → CQL Runtime → Parser → Validator → Engine → Result


Detailed flow:
- Process sends text query
- Runtime parses into AST
- Runtime validates AST
- Runtime checks permissions
- Runtime executes via engine
- Runtime returns QueryResult
Why this matters:
- The engine never sees raw text
- The engine never sees unvalidated queries
- The engine never sees untrusted input
This is how you keep the engine safe.

6. Error Propagation
Errors propagate cleanly through the runtime:
- AccessDenied
- SchemaMismatch
- InvalidArgument
- NotFound
- IoError
- Corrupt
- InternalError
Runtime converts them into:
- structured error objects
- human‑readable messages
- logs for SecurityCenter
This keeps Citadel debuggable.

7. Integration With Citadel Subsystems
ThemeService
- Requests handle at startup
- Executes SELECT/UPDATE queries
- Never touches raw files
- Never bypasses security
SecurityCenter
- Issues capabilities
- Revokes capabilities
- Audits capability usage
- Controls handle permissions
PortManager
- Queries Capabilities table
- Enforces network access
- Never modifies DB
System UI
- Reads Themes
- Reads ThemeTokens
- Never writes
Future subsystems
- ServiceRegistry
- UserProfiles
- Settings
- Logs
- Packages
All use the same runtime API.

8. Why This Runtime Model Works
A. Security
Capabilities are enforced at the boundary, not inside the engine.
B. Stability
Only one process (the runtime) touches the DB file.
C. Determinism
All queries go through the same pipeline.
D. Simplicity
Subsystems don’t need to know anything about CQL internals.
E. Extensibility
You can add:
- transactions
- WAL
- snapshots
- replication
- remote access
without changing the API.
