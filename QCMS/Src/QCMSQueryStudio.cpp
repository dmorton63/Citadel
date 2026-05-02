#include "QCMSQueryStudio.h"

#include "QCString.h"
#include "QCTypes.h"
#include "QWWindow.h"
#include "CQL_Database_Engine/QCSQLServiceProtocol.h"

namespace QCMS
{
    namespace
    {
        using namespace QCQL::Svc;

        struct LocalQueryAdapter
        {
            static bool streqIgnoreCase(const char *a, const char *b)
            {
                if (!a || !b)
                    return false;
                while (*a && *b)
                {
                    char ca = *a;
                    char cb = *b;
                    if (ca >= 'A' && ca <= 'Z')
                        ca = static_cast<char>(ca - 'A' + 'a');
                    if (cb >= 'A' && cb <= 'Z')
                        cb = static_cast<char>(cb - 'A' + 'a');
                    if (ca != cb)
                        return false;
                    ++a;
                    ++b;
                }
                return *a == '\0' && *b == '\0';
            }

            static bool isSpace(char ch)
            {
                return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
            }

            static const char *skipSpaces(const char *text)
            {
                while (text && isSpace(*text))
                    ++text;
                return text;
            }

            static bool startsWithIgnoreCase(const char *text, const char *prefix)
            {
                if (!text || !prefix)
                    return false;
                while (*prefix)
                {
                    if (*text == '\0')
                        return false;
                    char a = *text;
                    char b = *prefix;
                    if (a >= 'A' && a <= 'Z')
                        a = static_cast<char>(a - 'A' + 'a');
                    if (b >= 'A' && b <= 'Z')
                        b = static_cast<char>(b - 'A' + 'a');
                    if (a != b)
                        return false;
                    ++text;
                    ++prefix;
                }
                return true;
            }

            static bool readIdentifier(const char *&p, char *out, QC::usize outCap)
            {
                if (!out || outCap == 0)
                    return false;
                QC::String::memset(out, 0, outCap);
                p = p ? skipSpaces(p) : nullptr;
                if (!p || *p == '\0')
                    return false;

                const char first = *p;
                if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_'))
                    return false;

                QC::usize i = 0;
                while (*p)
                {
                    const char ch = *p;
                    const bool ident = (ch >= 'A' && ch <= 'Z') ||
                                       (ch >= 'a' && ch <= 'z') ||
                                       (ch >= '0' && ch <= '9') ||
                                       ch == '_';
                    if (!ident || i + 1 >= outCap)
                        break;
                    out[i++] = *p++;
                }
                out[i] = '\0';
                return i > 0;
            }

            static void setError(ExecuteSQLResponse &resp, const char *message)
            {
                resp.success = false;
                resp.rowsAffected = 0;
                resp.resultLength = 0;
                QC::String::strncpy(resp.errorMessage, message ? message : "Error", MaxErrorLength - 1);
                resp.errorMessage[MaxErrorLength - 1] = '\0';
                resp.result[0] = '\0';
            }

            static void setSuccess(ExecuteSQLResponse &resp, const char *text, QC::u32 rowsAffected)
            {
                resp.success = true;
                resp.rowsAffected = rowsAffected;
                resp.errorMessage[0] = '\0';
                QC::String::strncpy(resp.result, text ? text : "", MaxResultLength - 1);
                resp.result[MaxResultLength - 1] = '\0';
                resp.resultLength = static_cast<QC::u32>(QC::String::strlen(resp.result));
            }

            static bool appendText(char *dst, QC::usize cap, QC::usize &len, const char *src)
            {
                if (!dst || !src)
                    return false;
                for (QC::usize i = 0; src[i] != '\0'; ++i)
                {
                    if (len + 1 >= cap)
                        return false;
                    dst[len++] = src[i];
                }
                dst[len] = '\0';
                return true;
            }

            static bool appendChar(char *dst, QC::usize cap, QC::usize &len, char ch)
            {
                if (!dst || len + 1 >= cap)
                    return false;
                dst[len++] = ch;
                dst[len] = '\0';
                return true;
            }

            static bool appendU32(char *dst, QC::usize cap, QC::usize &len, QC::u32 value)
            {
                char tmp[16] = {};
                QC::usize digits = 0;
                if (value == 0)
                    return appendChar(dst, cap, len, '0');
                while (value > 0 && digits < sizeof(tmp))
                {
                    tmp[digits++] = static_cast<char>('0' + (value % 10));
                    value /= 10;
                }
                while (digits > 0)
                {
                    if (!appendChar(dst, cap, len, tmp[--digits]))
                        return false;
                }
                return true;
            }

            static void appendCellText(char *dst, QC::usize cap, QC::usize &len, const QCQL::Cell &cell)
            {
                if (cell.type == QCQL::ColumnType::Text)
                {
                    for (QC::usize i = 0; i < cell.bytes.size() && len + 1 < cap; ++i)
                    {
                        char ch = static_cast<char>(cell.bytes[i]);
                        if (ch == '\0')
                            continue;
                        if (ch < 32 || ch > 126)
                            ch = ' ';
                        dst[len++] = ch;
                    }
                    dst[len] = '\0';
                    return;
                }

                if (cell.type == QCQL::ColumnType::Bool)
                {
                    (void)appendText(dst, cap, len, (!cell.bytes.empty() && cell.bytes[0] != 0) ? "true" : "false");
                    return;
                }

                QC::u32 value = 0;
                const QC::usize maxBytes = cell.bytes.size() < 4 ? cell.bytes.size() : static_cast<QC::usize>(4);
                for (QC::usize i = 0; i < maxBytes; ++i)
                    value |= (static_cast<QC::u32>(cell.bytes[i]) << (8 * i));
                (void)appendU32(dst, cap, len, value);
            }

            static bool consumeKeyword(const char *&p, const char *keyword)
            {
                p = p ? skipSpaces(p) : nullptr;
                if (!p || !keyword)
                    return false;
                const char *cursor = p;
                while (*keyword)
                {
                    if (*cursor == '\0')
                        return false;
                    char a = *cursor;
                    char b = *keyword;
                    if (a >= 'A' && a <= 'Z')
                        a = static_cast<char>(a - 'A' + 'a');
                    if (b >= 'A' && b <= 'Z')
                        b = static_cast<char>(b - 'A' + 'a');
                    if (a != b)
                        return false;
                    ++cursor;
                    ++keyword;
                }
                if (*cursor != '\0' && !isSpace(*cursor) && *cursor != '(' && *cursor != ')' && *cursor != ',')
                    return false;
                p = cursor;
                return true;
            }

            static bool consumeChar(const char *&p, char token)
            {
                p = p ? skipSpaces(p) : nullptr;
                if (!p || *p != token)
                    return false;
                ++p;
                return true;
            }

            static bool parseColumnType(const char *&p, QCQL::ColumnType &outType)
            {
                char typeName[32];
                if (!readIdentifier(p, typeName, sizeof(typeName)))
                    return false;

                if (streqIgnoreCase(typeName, "text") || streqIgnoreCase(typeName, "varchar") || streqIgnoreCase(typeName, "char") || streqIgnoreCase(typeName, "string"))
                    outType = QCQL::ColumnType::Text;
                else if (streqIgnoreCase(typeName, "int") || streqIgnoreCase(typeName, "integer") || streqIgnoreCase(typeName, "tinyint") || streqIgnoreCase(typeName, "smallint") || streqIgnoreCase(typeName, "bigint"))
                    outType = QCQL::ColumnType::Int;
                else if (streqIgnoreCase(typeName, "bool") || streqIgnoreCase(typeName, "boolean"))
                    outType = QCQL::ColumnType::Bool;
                else if (streqIgnoreCase(typeName, "datetime") || streqIgnoreCase(typeName, "timestamp"))
                    outType = QCQL::ColumnType::DateTime;
                else
                    return false;

                p = p ? skipSpaces(p) : nullptr;
                if (p && *p == '(')
                {
                    ++p;
                    while (*p && *p != ')')
                        ++p;
                    if (*p != ')')
                        return false;
                    ++p;
                }

                return true;
            }

            static bool parseCreateTable(const char *definition, QCQL::TableSchema &outSchema)
            {
                const char *p = definition ? skipSpaces(definition) : nullptr;
                if (!p)
                    return false;

                outSchema = QCQL::TableSchema{};
                if (!readIdentifier(p, outSchema.tableName, sizeof(outSchema.tableName)))
                    return false;
                if (!consumeChar(p, '('))
                    return false;

                QC::u32 pkCount = 0;
                while (true)
                {
                    QCQL::Column column{};
                    if (!readIdentifier(p, column.name, sizeof(column.name)))
                        return false;
                    if (!parseColumnType(p, column.type))
                        return false;

                    const char *pkCursor = p;
                    if (consumeKeyword(pkCursor, "PRIMARY"))
                    {
                        if (!consumeKeyword(pkCursor, "KEY"))
                            return false;
                        column.isPrimaryKey = true;
                        p = pkCursor;
                        outSchema.primaryKeyIndex = static_cast<QC::u32>(outSchema.columns.size());
                        ++pkCount;
                    }

                    outSchema.columns.push_back(static_cast<QCQL::Column &&>(column));
                    if (outSchema.columns.size() > QCQL::kMaxColumnsPerTable)
                        return false;

                    p = p ? skipSpaces(p) : nullptr;
                    if (!p)
                        return false;
                    if (*p == ',')
                    {
                        ++p;
                        continue;
                    }
                    if (*p == ')')
                    {
                        ++p;
                        break;
                    }
                    return false;
                }

                p = p ? skipSpaces(p) : nullptr;
                return p && *p == '\0' && !outSchema.columns.empty() && pkCount == 1;
            }

            static void execShowTables(QCQL::Database *database, ExecuteSQLResponse &resp)
            {
                char buffer[MaxResultLength] = {};
                QC::usize len = 0;
                for (QC::usize i = 0; i < database->tables.size(); ++i)
                {
                    const QCQL::Table &table = database->tables[i];
                    if (i > 0)
                        (void)appendChar(buffer, sizeof(buffer), len, '\n');
                    (void)appendU32(buffer, sizeof(buffer), len, static_cast<QC::u32>(i + 1));
                    (void)appendText(buffer, sizeof(buffer), len, ") ");
                    (void)appendText(buffer, sizeof(buffer), len, table.name);
                    (void)appendText(buffer, sizeof(buffer), len, " columns=");
                    (void)appendU32(buffer, sizeof(buffer), len, static_cast<QC::u32>(table.schema.columns.size()));
                }
                if (database->tables.empty())
                    (void)appendText(buffer, sizeof(buffer), len, "No tables found");
                setSuccess(resp, buffer, static_cast<QC::u32>(database->tables.size()));
            }

            static const QCQL::Table *findTable(QCQL::Database *database, const char *name)
            {
                if (!database || !name)
                    return nullptr;
                for (QC::usize i = 0; i < database->tables.size(); ++i)
                {
                    if (streqIgnoreCase(database->tables[i].name, name))
                        return &database->tables[i];
                }
                return nullptr;
            }

            static void execDescribe(QCQL::Database *database, const char *name, ExecuteSQLResponse &resp)
            {
                const QCQL::Table *table = findTable(database, name);
                if (!table)
                {
                    setError(resp, "Table not found");
                    return;
                }

                char buffer[MaxResultLength] = {};
                QC::usize len = 0;
                for (QC::usize i = 0; i < table->schema.columns.size(); ++i)
                {
                    const QCQL::Column &col = table->schema.columns[i];
                    if (i > 0)
                        (void)appendChar(buffer, sizeof(buffer), len, '\n');
                    (void)appendU32(buffer, sizeof(buffer), len, static_cast<QC::u32>(i + 1));
                    (void)appendText(buffer, sizeof(buffer), len, ") ");
                    (void)appendText(buffer, sizeof(buffer), len, col.name);
                    (void)appendText(buffer, sizeof(buffer), len, " type=");
                    switch (col.type)
                    {
                    case QCQL::ColumnType::Text: (void)appendText(buffer, sizeof(buffer), len, "text"); break;
                    case QCQL::ColumnType::Int: (void)appendText(buffer, sizeof(buffer), len, "int"); break;
                    case QCQL::ColumnType::Bool: (void)appendText(buffer, sizeof(buffer), len, "bool"); break;
                    case QCQL::ColumnType::DateTime: (void)appendText(buffer, sizeof(buffer), len, "datetime"); break;
                    }
                    if (col.isPrimaryKey)
                        (void)appendText(buffer, sizeof(buffer), len, " pk=1");
                }
                setSuccess(resp, buffer, static_cast<QC::u32>(table->schema.columns.size()));
            }

            static bool parseLimitSuffix(const char *text, QC::u32 &outLimit)
            {
                const char *p = text ? skipSpaces(text) : nullptr;
                if (!p || *p == '\0')
                    return true;
                if (!consumeKeyword(p, "LIMIT"))
                    return false;
                p = skipSpaces(p);
                if (!p || *p < '0' || *p > '9')
                    return false;
                QC::u32 value = 0;
                while (*p >= '0' && *p <= '9')
                {
                    value = (value * 10u) + static_cast<QC::u32>(*p - '0');
                    ++p;
                }
                p = skipSpaces(p);
                if (*p != '\0')
                    return false;
                outLimit = value;
                return true;
            }

            static void execSelect(QCQL::Database *database, const char *text, ExecuteSQLResponse &resp)
            {
                const char *p = text ? text : "";
                if (!consumeKeyword(p, "SELECT") || !consumeChar(p, '*') || !consumeKeyword(p, "FROM"))
                {
                    setError(resp, "Only SELECT * FROM <table> [LIMIT N] is supported in Query Studio MVP");
                    return;
                }

                char tableName[64];
                if (!readIdentifier(p, tableName, sizeof(tableName)))
                {
                    setError(resp, "Missing table name");
                    return;
                }

                QC::u32 limit = 25;
                if (!parseLimitSuffix(p, limit))
                {
                    setError(resp, "Bad LIMIT clause");
                    return;
                }

                const QCQL::Table *table = findTable(database, tableName);
                if (!table)
                {
                    setError(resp, "Table not found");
                    return;
                }

                char buffer[MaxResultLength] = {};
                QC::usize len = 0;
                QC::u32 emitted = 0;
                for (QC::usize pageIndex = 0; pageIndex < table->pages.size() && emitted < limit; ++pageIndex)
                {
                    QCQL::Page page{};
                    if (QCQL::Engine::instance().loadPage(*database, table->pages[pageIndex], page) != QCQL::Status::Success)
                        continue;

                    for (QC::usize rowIndex = 0; rowIndex < page.rowOffsets.size() && emitted < limit; ++rowIndex)
                    {
                        QCQL::Row row{};
                        if (QCQL::Engine::instance().readRow(*database, page.header.pageId, page.rowOffsets[rowIndex], row) != QCQL::Status::Success)
                            continue;
                        if (row.tombstone)
                            continue;

                        if (emitted > 0)
                            (void)appendChar(buffer, sizeof(buffer), len, '\n');
                        for (QC::usize c = 0; c < row.cells.size(); ++c)
                        {
                            if (c > 0)
                                (void)appendText(buffer, sizeof(buffer), len, " | ");
                            appendCellText(buffer, sizeof(buffer), len, row.cells[c]);
                        }
                        ++emitted;
                    }
                }

                if (emitted == 0)
                    (void)appendText(buffer, sizeof(buffer), len, "No rows found");
                setSuccess(resp, buffer, emitted);
            }

            static void execCreateTable(QCQL::Database *database, const char *definition, ExecuteSQLResponse &resp)
            {
                QCQL::TableSchema schema{};
                if (!parseCreateTable(definition, schema))
                {
                    setError(resp, "Bad CREATE TABLE syntax");
                    return;
                }

                const QCQL::Status status = QCQL::Engine::instance().createTable(*database, schema);
                if (status != QCQL::Status::Success)
                {
                    switch (status)
                    {
                    case QCQL::Status::AlreadyExists: setError(resp, "Table already exists"); break;
                    case QCQL::Status::InvalidParam: setError(resp, "Invalid schema"); break;
                    default: setError(resp, "Create table failed"); break;
                    }
                    return;
                }

                char buffer[256] = {};
                QC::usize len = 0;
                (void)appendText(buffer, sizeof(buffer), len, "Created table ");
                (void)appendText(buffer, sizeof(buffer), len, schema.tableName);
                (void)appendText(buffer, sizeof(buffer), len, " columns=");
                (void)appendU32(buffer, sizeof(buffer), len, static_cast<QC::u32>(schema.columns.size()));
                setSuccess(resp, buffer, 0);
            }

            static void execute(QCQL::Database *database, const ExecuteSQLRequest &req, ExecuteSQLResponse &resp)
            {
                resp.type = MessageType::Response;
                resp.success = false;
                resp.resultLength = 0;
                resp.rowsAffected = 0;
                resp.result[0] = '\0';
                resp.errorMessage[0] = '\0';

                if (!database)
                {
                    setError(resp, "No database bound");
                    return;
                }

                const char *query = skipSpaces(req.query);
                if (!query || *query == '\0')
                {
                    setError(resp, "Query is empty");
                    return;
                }

                if (streqIgnoreCase(query, "SHOW TABLES") || streqIgnoreCase(query, "DUMP TABLES_LOADED"))
                {
                    execShowTables(database, resp);
                    return;
                }

                if (startsWithIgnoreCase(query, "DESCRIBE "))
                {
                    execDescribe(database, skipSpaces(query + 9), resp);
                    return;
                }

                if (startsWithIgnoreCase(query, "SHOW COLUMNS FROM "))
                {
                    execDescribe(database, skipSpaces(query + 18), resp);
                    return;
                }

                if (startsWithIgnoreCase(query, "SELECT "))
                {
                    execSelect(database, query, resp);
                    return;
                }

                if (startsWithIgnoreCase(query, "CREATE TABLE "))
                {
                    execCreateTable(database, query + 13, resp);
                    return;
                }

                setError(resp, "Supported queries: SHOW TABLES, DESCRIBE, SHOW COLUMNS, SELECT * FROM, CREATE TABLE");
            }
        };
    }

    void QueryStudio::build(QW::Window *window, QW::Controls::Panel *parent, QCQL::Database *database, const QW::Rect &bounds)
    {
        if (!window || !parent)
            return;

        m_database = database;
        m_panel = new QW::Controls::Panel(window, bounds);
        m_panel->setBorderStyle(QW::Controls::BorderStyle::None);
        m_panel->setPadding(12);
        m_panel->setVisible(false);
        parent->addChild(m_panel);

        m_title = new QW::Controls::Label(window, "CSSMS Query Studio", {10, 8, bounds.width - 20, 22});
        m_panel->addChild(m_title);

        m_hint = new QW::Controls::Label(window, "Protocol shape: QCSQL ExecuteSQL. Transport: local QCQL adapter until service registration lands.", {10, 30, bounds.width - 20, 18});
        m_panel->addChild(m_hint);

        m_queryBox = new QW::Controls::TextBox(window, {10, 56, bounds.width - 220, 24});
        m_queryBox->setPlaceholder("SHOW TABLES | DESCRIBE Themes | SELECT * FROM Themes LIMIT 10 | CREATE TABLE demo(id int PRIMARY KEY, name text)");
        m_queryBox->setTextSubmitHandler(&QueryStudio::onQuerySubmitted, this);
        m_panel->addChild(m_queryBox);

        m_executeButton = new QW::Controls::Button(window, "Execute", {static_cast<QC::i32>(bounds.width - 198), 56, 88, 24});
        m_executeButton->setContentMode(QW::ButtonContentMode::Text);
        m_executeButton->setRole(QW::ButtonRole::Accent);
        m_executeButton->setClickHandler(&QueryStudio::onExecuteClicked, this);
        m_panel->addChild(m_executeButton);

        m_clearButton = new QW::Controls::Button(window, "Clear", {static_cast<QC::i32>(bounds.width - 104), 56, 88, 24});
        m_clearButton->setContentMode(QW::ButtonContentMode::Text);
        m_clearButton->setClickHandler(&QueryStudio::onClearClicked, this);
        m_panel->addChild(m_clearButton);

        m_results = new QW::Controls::ListView(window, {10, 90, bounds.width - 20, bounds.height - 136});
        m_results->setSelectionMode(QW::Controls::SelectionMode::Single);
        m_results->setShowHeader(true);
        m_results->addColumn("Result", bounds.width - 40, QW::Controls::TextAlign::Left);
        m_panel->addChild(m_results);

        m_status = new QW::Controls::Label(window, "Ready", {10, static_cast<QC::i32>(bounds.height - 34), bounds.width - 20, 20});
        m_panel->addChild(m_status);

        renderIntro();
    }

    void QueryStudio::destroy()
    {
        m_database = nullptr;
        m_panel = nullptr;
        m_title = nullptr;
        m_hint = nullptr;
        m_queryBox = nullptr;
        m_executeButton = nullptr;
        m_clearButton = nullptr;
        m_results = nullptr;
        m_status = nullptr;
    }

    void QueryStudio::setVisible(bool visible)
    {
        if (m_panel)
            m_panel->setVisible(visible);
    }

    void QueryStudio::renderIntro()
    {
        if (!m_results || !m_status)
            return;
        m_results->clearItems();
        m_results->addItem("Query Studio MVP", nullptr);
        m_results->addItem("This panel uses QCSQL ExecuteSQL request/response semantics.", nullptr);
        m_results->addItem("Current transport is a local adapter over QCQL::Database.", nullptr);
        m_results->addItem("When QCSQL is registered, the UI should switch transports without redesign.", nullptr);
        m_status->setText(m_database ? "Ready: local protocol adapter bound to current QCQL database" : "No database bound");
    }

    void QueryStudio::executeCurrentQuery()
    {
        if (!m_queryBox || !m_results || !m_status)
            return;

        ExecuteSQLRequest req{};
        ExecuteSQLResponse resp{};
        req.handle = 0;
        QC::String::strncpy(req.query, m_queryBox->text() ? m_queryBox->text() : "", MaxQueryLength - 1);
        req.query[MaxQueryLength - 1] = '\0';
        req.queryLength = static_cast<QC::u32>(QC::String::strlen(req.query));

        LocalQueryAdapter::execute(m_database, req, resp);

        m_results->clearItems();
        if (resp.success)
        {
            const char *cursor = resp.result;
            while (cursor && *cursor)
            {
                char line[256] = {};
                QC::usize len = 0;
                while (*cursor && *cursor != '\n' && len + 1 < sizeof(line))
                    line[len++] = *cursor++;
                line[len] = '\0';
                m_results->addItem(line[0] ? line : " ", nullptr);
                if (*cursor == '\n')
                    ++cursor;
            }
            if (resp.result[0] == '\0')
                m_results->addItem("(success)", nullptr);

            char status[160] = {};
            QC::usize statusLen = 0;
            LocalQueryAdapter::appendText(status, sizeof(status), statusLen, "Success rows=");
            LocalQueryAdapter::appendU32(status, sizeof(status), statusLen, resp.rowsAffected);
            LocalQueryAdapter::appendText(status, sizeof(status), statusLen, " mode=QCSQL-protocol/local-adapter");
            m_status->setText(status);
        }
        else
        {
            m_results->addItem(resp.errorMessage[0] ? resp.errorMessage : "Query failed", nullptr);
            m_status->setText("Error");
        }

        if (m_panel)
            m_panel->invalidate();
    }

    void QueryStudio::onExecuteClicked(QW::Controls::Button *, void *userData)
    {
        auto *self = static_cast<QueryStudio *>(userData);
        if (self)
            self->executeCurrentQuery();
    }

    void QueryStudio::onClearClicked(QW::Controls::Button *, void *userData)
    {
        auto *self = static_cast<QueryStudio *>(userData);
        if (!self)
            return;
        if (self->m_queryBox)
            self->m_queryBox->setText("");
        self->renderIntro();
    }

    void QueryStudio::onQuerySubmitted(QW::Controls::TextBox *, void *userData)
    {
        auto *self = static_cast<QueryStudio *>(userData);
        if (self)
            self->executeCurrentQuery();
    }

} // namespace QCMS