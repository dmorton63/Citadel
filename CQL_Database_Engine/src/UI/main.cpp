#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <commdlg.h>     // For file dialogs
#include <sstream>       // For comment toggle feature
#include <map>           // For result buffers
#include <algorithm>     // For std::min
#include "Database.h"


// DirectX 11 data
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// CQL Database
static CQL::Database g_database;

// Query result structure for collapsible display
struct QueryResult {
    std::string query;      // The SQL query text
    std::string result;     // The output/result
    int queryNumber;        // Query #1, #2, etc.
    bool isError;           // True if result contains "Error:"
};

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Main code
int main(int, char**)
{
    // Create application window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, 
                       GetModuleHandle(nullptr), nullptr, nullptr, 
                       nullptr, nullptr, L"CQL Database Engine", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"CQL Database Engine", 
                                 WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, 
                                 nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    //io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Main loop
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Enable docking
        //ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());

        // Your CQL Database UI goes here
        {
            ImGui::SetNextWindowSize(ImVec2(1000, 750), ImGuiCond_FirstUseEver);
            ImGui::Begin("CQL Database Engine", nullptr, ImGuiWindowFlags_MenuBar);
            
            // Menu bar
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("New Database")) {
                        // New database dialog
                        OPENFILENAMEA ofn;
                        char szFile[260] = { 0 };
                        ZeroMemory(&ofn, sizeof(ofn));
                        ofn.lStructSize = sizeof(ofn);
                        ofn.hwndOwner = hwnd;
                        ofn.lpstrFile = szFile;
                        ofn.nMaxFile = sizeof(szFile);
                        ofn.lpstrFilter = "CQL Database\0*.cql\0All Files\0*.*\0";
                        ofn.nFilterIndex = 1;
                        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
                        ofn.lpstrDefExt = "cql";
                        
                        if (GetSaveFileNameA(&ofn)) {
                            if (g_database.Create(szFile)) {
                                ImGui::OpenPopup("Success");
                            } else {
                                ImGui::OpenPopup("Error");
                            }
                        }
                    }
                    
                    if (ImGui::MenuItem("Open Database")) {
                        OPENFILENAMEA ofn;
                        char szFile[260] = { 0 };
                        ZeroMemory(&ofn, sizeof(ofn));
                        ofn.lStructSize = sizeof(ofn);
                        ofn.hwndOwner = hwnd;
                        ofn.lpstrFile = szFile;
                        ofn.nMaxFile = sizeof(szFile);
                        ofn.lpstrFilter = "CQL Database\0*.cql\0All Files\0*.*\0";
                        ofn.nFilterIndex = 1;
                        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                        
                        if (GetOpenFileNameA(&ofn)) {
                            if (g_database.Open(szFile)) {
                                ImGui::OpenPopup("Success");
                            } else {
                                ImGui::OpenPopup("Error");
                            }
                        }
                    }
                    
                    if (ImGui::MenuItem("Close Database", nullptr, false, g_database.IsOpen())) {
                        g_database.Close();
                    }
                    
                    ImGui::Separator();
                    
                    if (ImGui::MenuItem("Exit")) {
                        done = true;
                    }
                    
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            
            // Popups
            if (ImGui::BeginPopupModal("Success", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Operation completed successfully!");
                if (ImGui::Button("OK", ImVec2(120, 0)))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            
            if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Operation failed! Check console for details.");           
                //ImGui::Text("Database functionality coming soon!");
                if (ImGui::Button("OK", ImVec2(120, 0)))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            
            // Database Status
            ImGui::SeparatorText("Database Status");
            
            if (g_database.IsOpen()) {
                const auto& header = g_database.GetHeader();
                
                ImGui::Text("Status: Connected");
                ImGui::Text("Path: %s", g_database.GetPath().c_str());
                ImGui::Spacing();
                
                // File Header Info
                if (ImGui::CollapsingHeader("File Header")) {
                    ImGui::Indent();
                    ImGui::Text("Magic: %.5s", header.magic);
                    ImGui::Text("Version: %u", header.version);
                    ImGui::Text("Page Size: %u bytes", header.pageSize);
                    ImGui::Text("Table Count: %u", header.tableCount);
                    ImGui::Text("Table Directory Offset: 0x%llX", header.tableDirOffset);
                    ImGui::Text("Schema Offset: 0x%llX", header.schemaOffset);
                    ImGui::Text("Page Region Offset: 0x%llX", header.pageRegionOffset);
                    ImGui::Unindent();
                }

                // Schema Browser
                if (ImGui::CollapsingHeader("Schema Browser", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const auto& tables = g_database.GetTables();

                    if (tables.empty()) {
                        ImGui::Indent();
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No tables in database");
                        ImGui::Unindent();
                    } else {
                        for (const auto& table : tables) {
                            // Table node
                            std::string tableLabel = "Table: " + table->GetName();
                            if (ImGui::TreeNode(tableLabel.c_str())) {
                                ImGui::Text("Root Page: %llu", table->GetRootPage());
                                ImGui::Text("Schema Offset: 0x%llX", table->GetSchemaOffset());
                                ImGui::Spacing();

                                // Columns
                                ImGui::Text("Columns (%zu):", table->GetColumns().size());
                                ImGui::Spacing();

                                // Use modern BeginTable API (better than legacy Columns)
                                if (ImGui::BeginTable("columnTable", 4, 
                                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                                    ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit))
                                {
                                    // Setup columns
                                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                                    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                                    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                                    ImGui::TableSetupColumn("PK", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                                    ImGui::TableHeadersRow();

                                    // Column rows
                                    for (const auto& col : table->GetColumns()) {
                                        ImGui::TableNextRow();

                                        ImGui::TableNextColumn();
                                        ImGui::Text("%s", col.name.c_str());

                                        ImGui::TableNextColumn();
                                        std::string typeName = CQL::Table::GetTypeName(col.type, col.size);
                                        ImGui::Text("%s", typeName.c_str());

                                        ImGui::TableNextColumn();
                                        if (col.size > 0) {
                                            ImGui::Text("%u", col.size);
                                        } else {
                                            ImGui::Text("-");
                                        }

                                        ImGui::TableNextColumn();
                                        if (col.isPrimaryKey) {
                                            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "YES");
                                        } else {
                                            ImGui::Text("-");
                                        }
                                    }

                                    ImGui::EndTable();
                                }

                                ImGui::TreePop();
                            }
                        }
                    }
                }

            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Status: Not Connected");
                ImGui::Text("Use File > New Database or File > Open Database");
            }

            // Hex Viewer
            ImGui::SeparatorText("Hex Viewer");

            if (g_database.IsOpen()) {
                if (ImGui::CollapsingHeader("Hex Dump")) {
                    static int hexOffset = 0;
                    static int hexLength = 256;
                    static std::vector<uint8_t> hexData;
                    static bool needsRefresh = true;

                    ImGui::Indent();

                ImGui::Text("View raw bytes from database file:");
                ImGui::Spacing();

                // Quick navigation buttons
                if (ImGui::Button("File Header (0)", ImVec2(120, 0))) {
                    hexOffset = 0;
                    needsRefresh = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Table Dir (256)", ImVec2(130, 0))) {
                    hexOffset = 256;
                    needsRefresh = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Schema", ImVec2(120, 0))) {
                    hexOffset = static_cast<int>(g_database.GetHeader().schemaOffset);
                    needsRefresh = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Page Region", ImVec2(120, 0))) {
                    hexOffset = static_cast<int>(g_database.GetHeader().pageRegionOffset);
                    needsRefresh = true;
                }

                ImGui::Spacing();

                // Manual offset/length input
                if (ImGui::InputInt("Offset", &hexOffset)) {
                    if (hexOffset < 0) hexOffset = 0;
                    needsRefresh = true;
                }
                if (ImGui::InputInt("Length", &hexLength)) {
                    if (hexLength < 16) hexLength = 16;
                    if (hexLength > 4096) hexLength = 4096;
                    needsRefresh = true;
                }

                if (ImGui::Button("Refresh", ImVec2(120, 0))) {
                    needsRefresh = true;
                }

                ImGui::Spacing();

                // Read data if needed
                if (needsRefresh) {
                    hexData.resize(hexLength);
                    std::ifstream file(g_database.GetPath(), std::ios::binary);
                    if (file) {
                        file.seekg(hexOffset);
                        file.read(reinterpret_cast<char*>(hexData.data()), hexLength);
                        size_t actualRead = file.gcount();
                        hexData.resize(actualRead);
                    }
                    needsRefresh = false;
                }

                // Display hex dump
                ImGui::BeginChild("HexDump", ImVec2(0, 300), true, ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Use default font (monospace would be better)

                for (size_t i = 0; i < hexData.size(); i += 16) {
                    // Address
                    ImGui::Text("%08X: ", static_cast<unsigned int>(hexOffset + i));
                    ImGui::SameLine();

                    // Hex bytes
                    std::string hexLine;
                    std::string asciiLine;
                    for (size_t j = 0; j < 16; j++) {
                        if (i + j < hexData.size()) {
                            char buf[4];
                            snprintf(buf, sizeof(buf), "%02X ", hexData[i + j]);
                            hexLine += buf;

                            // ASCII representation
                            char c = hexData[i + j];
                            asciiLine += (c >= 32 && c <= 126) ? c : '.';
                        } else {
                            hexLine += "   ";
                            asciiLine += " ";
                        }

                        // Extra space after 8 bytes
                        if (j == 7) hexLine += " ";
                    }

                    ImGui::Text("%s  %s", hexLine.c_str(), asciiLine.c_str());
                }

                ImGui::PopFont();
                ImGui::EndChild();

                ImGui::Unindent();
                }  // End of CollapsingHeader("Hex Dump")
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), 
                                  "Open a database to view hex data");
            }

            // Query Interface
            ImGui::SeparatorText("Query Interface");

            if (g_database.IsOpen()) {
                static char queryBuffer[16384] = "";  // Increased from 4096 to 16KB for larger SQL files
                static std::vector<QueryResult> queryResults;  // Store each result separately
                static std::map<int, std::vector<char>> resultBuffers;  // Static map for result text buffers

                ImGui::Text("Enter CQL commands (Ctrl+/ to comment/uncomment):");

                // Simple approach: Check for Ctrl+/ after input
                ImGui::InputTextMultiline("##query", queryBuffer, sizeof(queryBuffer), 
                                         ImVec2(-1, 150), ImGuiInputTextFlags_AllowTabInput);

                // Handle Ctrl+/ for comment toggle
                if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Slash) && ImGui::GetIO().KeyCtrl) {
                    std::string text(queryBuffer);
                    std::stringstream ss(text);
                    std::string line;
                    std::string result;

                    // Process each line
                    while (std::getline(ss, line)) {
                        // Find first non-whitespace
                        size_t firstNonSpace = line.find_first_not_of(" \t");

                        if (firstNonSpace != std::string::npos) {
                            // Check if already commented
                            if (line.substr(firstNonSpace, 2) == "--") {
                                // Uncomment: Remove "-- " or "--"
                                if (firstNonSpace + 2 < line.length() && line[firstNonSpace + 2] == ' ') {
                                    line.erase(firstNonSpace, 3); // Remove "-- "
                                } else {
                                    line.erase(firstNonSpace, 2); // Remove "--"
                                }
                            } else {
                                // Comment: Insert "-- " at first non-space
                                line.insert(firstNonSpace, "-- ");
                            }
                        }

                        result += line + "\n";
                    }

                    // Remove trailing newline if original didn't have it
                    if (!text.empty() && text.back() != '\n' && !result.empty() && result.back() == '\n') {
                        result.pop_back();
                    }

                    // Update buffer
                    strncpy_s(queryBuffer, result.c_str(), sizeof(queryBuffer) - 1);
                    queryBuffer[sizeof(queryBuffer) - 1] = '\0';
                }

                if (ImGui::Button("Execute", ImVec2(120, 0))) {
                    if (strlen(queryBuffer) > 0) {
                        // Clear previous results and buffers
                        queryResults.clear();
                        resultBuffers.clear();  // IMPORTANT: Clear old result buffers to prevent overflow

                        // Split queries by semicolon and execute each
                        std::string allQueries(queryBuffer);
                        size_t pos = 0;
                        int queryNum = 1;

                        while (pos < allQueries.length()) {
                            // Find next semicolon
                            size_t semicolonPos = allQueries.find(';', pos);
                            if (semicolonPos == std::string::npos) {
                                semicolonPos = allQueries.length();
                            }

                            // Extract single query
                            std::string singleQuery = allQueries.substr(pos, semicolonPos - pos);

                            // Trim whitespace
                            size_t firstNonSpace = singleQuery.find_first_not_of(" \t\r\n");
                            if (firstNonSpace != std::string::npos) {
                                singleQuery = singleQuery.substr(firstNonSpace);
                                size_t lastNonSpace = singleQuery.find_last_not_of(" \t\r\n");
                                singleQuery = singleQuery.substr(0, lastNonSpace + 1);

                                // Execute non-empty query
                                if (!singleQuery.empty()) {
                                    QueryResult qr;
                                    qr.query = singleQuery;
                                    qr.result = g_database.ExecuteQuery(singleQuery);
                                    qr.queryNumber = queryNum;
                                    qr.isError = (qr.result.find("Error:") != std::string::npos);

                                    queryResults.push_back(qr);
                                    queryNum++;
                                }
                            }

                            // Move to next query
                            pos = semicolonPos + 1;
                        }
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("Clear", ImVec2(120, 0))) {
                    // Clear query input, results, AND result buffers
                    queryBuffer[0] = '\0';
                    queryResults.clear();
                    resultBuffers.clear();  // Clear buffers to prevent memory buildup
                }

                ImGui::SameLine();
                if (ImGui::Button("Load SQL File", ImVec2(120, 0))) {
                    // Open file dialog
                    OPENFILENAMEA ofn;
                    char szFile[260] = { 0 };
                    ZeroMemory(&ofn, sizeof(ofn));
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = sizeof(szFile);
                    ofn.lpstrFilter = "SQL Files (*.sql)\0*.sql\0All Files (*.*)\0*.*\0";
                    ofn.nFilterIndex = 1;
                    ofn.lpstrFileTitle = nullptr;
                    ofn.nMaxFileTitle = 0;
                    ofn.lpstrInitialDir = nullptr;
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                    if (GetOpenFileNameA(&ofn) == TRUE) {
                        // Read file contents
                        FILE* file = nullptr;
                        fopen_s(&file, szFile, "r");
                        if (file) {
                            size_t bytesRead = fread(queryBuffer, 1, sizeof(queryBuffer) - 1, file);
                            queryBuffer[bytesRead] = '\0';
                            fclose(file);

                            // Show file loaded message as first "result"
                            queryResults.clear();
                            QueryResult qr;
                            qr.query = "-- File Load --";
                            qr.result = std::string("Loaded file: ") + szFile + "\n(Click Execute to run queries)";
                            qr.queryNumber = 0;
                            qr.isError = false;
                            queryResults.push_back(qr);
                        } else {
                            queryResults.clear();
                            QueryResult qr;
                            qr.query = "-- File Load Error --";
                            qr.result = std::string("Error: Failed to load file: ") + szFile;
                            qr.queryNumber = 0;
                            qr.isError = true;
                            queryResults.push_back(qr);
                        }
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("Save Results", ImVec2(120, 0))) {
                    if (!queryResults.empty()) {
                        // Open save file dialog
                        OPENFILENAMEA ofn;
                        char szFile[260] = "query_results.txt";
                        ZeroMemory(&ofn, sizeof(ofn));
                        ofn.lStructSize = sizeof(ofn);
                        ofn.hwndOwner = hwnd;
                        ofn.lpstrFile = szFile;
                        ofn.nMaxFile = sizeof(szFile);
                        ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
                        ofn.nFilterIndex = 1;
                        ofn.lpstrDefExt = "txt";
                        ofn.Flags = OFN_OVERWRITEPROMPT;

                        if (GetSaveFileNameA(&ofn) == TRUE) {
                            // Write all results to file
                            FILE* file = nullptr;
                            fopen_s(&file, szFile, "w");
                            if (file) {
                                fprintf(file, "CQL Database Query Results\n");
                                fprintf(file, "Database: %s\n", g_database.GetPath().c_str());
                                fprintf(file, "Generated: %s\n", __DATE__ " " __TIME__);
                                fprintf(file, "========================================\n\n");

                                for (const auto& qr : queryResults) {
                                    fprintf(file, "Query #%d:\n", qr.queryNumber);
                                    fprintf(file, "%s\n\n", qr.query.c_str());
                                    fprintf(file, "Result:\n");
                                    fprintf(file, "%s\n", qr.result.c_str());
                                    fprintf(file, "----------------------------------------\n\n");
                                }

                                fclose(file);

                                // Show success message
                                QueryResult qr;
                                qr.query = "-- Save Results --";
                                qr.result = std::string("Results saved to: ") + szFile;
                                qr.queryNumber = 0;
                                qr.isError = false;
                                queryResults.insert(queryResults.begin(), qr);
                            }
                        }
                    }
                }

                ImGui::Spacing();

                // Display results in scrollable area with collapsible headers
                // Count errors and successes for summary
                int errorCount = 0;
                int successCount = 0;
                for (const auto& qr : queryResults) {
                    if (qr.isError) errorCount++;
                    else successCount++;
                }

                ImGui::Text("Results: %d success, %d errors", successCount, errorCount);

                // Collapse/Expand controls
                static bool collapseAllErrors = false;
                static bool expandAll = false;

                if (ImGui::Button("Collapse All Errors")) {
                    collapseAllErrors = true;
                    expandAll = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Expand All")) {
                    collapseAllErrors = false;
                    expandAll = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Collapse All")) {
                    collapseAllErrors = true;
                    expandAll = false;
                }

                // Calculate remaining vertical space for results
                float availableHeight = ImGui::GetContentRegionAvail().y - 50; // Leave space for FPS counter
                ImGui::BeginChild("##output", ImVec2(0, availableHeight), true, 
                                 ImGuiWindowFlags_HorizontalScrollbar);

                if (queryResults.empty()) {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), 
                                      "Ready to execute queries...");
                } else {
                    for (auto& qr : queryResults) {
                        // Count lines in result for summary
                        int lineCount = 1;
                        for (char c : qr.result) {
                            if (c == '\n') lineCount++;
                        }

                        // Build header text with color coding
                        std::string headerText = "Query #" + std::to_string(qr.queryNumber) + 
                                                " (" + std::to_string(lineCount) + " lines)";

                        // Color the header based on error/success
                        ImVec4 headerColor = qr.isError ? 
                            ImVec4(1.0f, 0.4f, 0.4f, 1.0f) :  // Red for errors
                            ImVec4(0.4f, 1.0f, 0.4f, 1.0f);   // Green for success

                        ImGui::PushStyleColor(ImGuiCol_Header, headerColor);
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 
                            ImVec4(headerColor.x * 1.2f, headerColor.y * 1.2f, headerColor.z * 1.2f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_HeaderActive, 
                            ImVec4(headerColor.x * 0.8f, headerColor.y * 0.8f, headerColor.z * 0.8f, 1.0f));

                        // Determine if this header should be open based on user controls
                        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;

                        // Collapse errors if requested
                        if (collapseAllErrors && qr.isError) {
                            flags = ImGuiTreeNodeFlags_None; // Collapsed
                        } else if (expandAll || (!collapseAllErrors && !qr.isError)) {
                            flags = ImGuiTreeNodeFlags_DefaultOpen; // Expanded
                        }

                        // Collapsible header with dynamic open/close state
                        if (ImGui::CollapsingHeader(headerText.c_str(), flags)) {
                            ImGui::PopStyleColor(3);

                            ImGui::Indent();

                            // Show the SQL query
                            ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "SQL:");
                            ImGui::SameLine();
                            ImGui::TextWrapped("%s", qr.query.c_str());

                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::Spacing();

                            // Show the result in a selectable, scrollable text area
                            // Calculate height based on line count (max 20 lines visible)
                            int visibleLines = (lineCount < 20) ? lineCount : 20;
                            float textHeight = visibleLines * ImGui::GetTextLineHeightWithSpacing();

                            // Create a unique buffer for this result (so it's selectable)
                            // resultBuffers is now static at scope level and cleared on Execute
                            if (resultBuffers[qr.queryNumber].empty() || 
                                resultBuffers[qr.queryNumber].size() != qr.result.size() + 1) {
                                resultBuffers[qr.queryNumber].resize(qr.result.size() + 1);
                                strcpy_s(resultBuffers[qr.queryNumber].data(), 
                                        resultBuffers[qr.queryNumber].size(), 
                                        qr.result.c_str());
                            }

                            // Read-only, selectable multiline text
                            ImGui::InputTextMultiline(
                                ("##result" + std::to_string(qr.queryNumber)).c_str(),
                                resultBuffers[qr.queryNumber].data(),
                                resultBuffers[qr.queryNumber].size(),
                                ImVec2(-1, textHeight),
                                ImGuiInputTextFlags_ReadOnly
                            );

                            ImGui::Unindent();
                            ImGui::Spacing();
                        } else {
                            ImGui::PopStyleColor(3);
                        }
                    }
                }

                ImGui::EndChild();
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), 
                                  "Open a database to execute queries");
            }

            ImGui::Separator();
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 
                        1000.0f / io.Framerate, io.Framerate);

            ImGui::End();
        }

        // Demo window for reference
        {
            /*static bool show_demo = true;
            if (show_demo)
                ImGui::ShowDemoWindow(&show_demo);*/
        }

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Update and Render additional Platform Windows
        //if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        //{
        //    ImGui::UpdatePlatformWindows();
        //    ImGui::RenderPlatformWindowsDefault();
        //}

        g_pSwapChain->Present(1, 0);
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions for DirectX setup
bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };

    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 
                                                 createDeviceFlags, featureLevelArray, 2, 
                                                 D3D11_SDK_VERSION, &sd, &g_pSwapChain, 
                                                 &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        if (g_pd3dDevice != nullptr)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
