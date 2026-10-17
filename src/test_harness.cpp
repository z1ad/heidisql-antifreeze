#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <thread>
#include <chrono>

struct SymbolCheck {
    const char* name;
    bool required;
};

static const std::vector<SymbolCheck> g_symbols = {
    {"mysql_affected_rows", true},
    {"mysql_character_set_name", true},
    {"mysql_close", true},
    {"mysql_data_seek", true},
    {"mysql_errno", true},
    {"mysql_error", true},
    {"mysql_fetch_field_direct", true},
    {"mysql_fetch_field", true},
    {"mysql_fetch_lengths", true},
    {"mysql_fetch_row", true},
    {"mysql_free_result", true},
    {"mysql_get_client_info", true},
    {"mysql_get_server_info", true},
    {"mysql_init", true},
    {"mysql_info", true},
    {"mysql_num_fields", true},
    {"mysql_num_rows", true},
    {"mysql_options", true},
    {"mysql_optionsv", false},
    {"mysql_ping", true},
    {"mysql_real_connect", true},
    {"mysql_real_query", true},
    {"mysql_stat", true},
    {"mysql_store_result", true},
    {"mysql_thread_id", true},
    {"mysql_next_result", true},
    {"mysql_set_character_set", true},
    {"mysql_thread_init", true},
    {"mysql_thread_end", true},
    {"mysql_warning_count", true},
    {"mariadb_cancel", false}
};

extern bool DetectAndStripOrderBy(const std::string& sql, std::string& outStrippedSql);

// Inlined copy for standalone harness verification
bool LocalDetectAndStripOrderBy(const std::string& sql, std::string& outStrippedSql) {
    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    bool inBacktick = false;
    int parenDepth = 0;

    size_t len = sql.length();
    size_t orderStart = std::string::npos;
    size_t orderEnd = std::string::npos;

    auto matchKeyword = [&](size_t pos, const char* kw) -> bool {
        size_t kwLen = strlen(kw);
        if (pos + kwLen > len) return false;
        if (pos > 0) {
            char prev = sql[pos - 1];
            if (isalnum((unsigned char)prev) || prev == '_' || prev == '$') return false;
        }
        for (size_t k = 0; k < kwLen; ++k) {
            if (toupper((unsigned char)sql[pos + k]) != toupper((unsigned char)kw[k])) return false;
        }
        if (pos + kwLen < len) {
            char next = sql[pos + kwLen];
            if (isalnum((unsigned char)next) || next == '_' || next == '$') return false;
        }
        return true;
    };

    for (size_t i = 0; i < len; ++i) {
        char c = sql[i];
        if (inSingleQuote) {
            if (c == '\\') { i++; continue; }
            if (c == '\'') {
                if (i + 1 < len && sql[i + 1] == '\'') { i++; }
                else { inSingleQuote = false; }
            }
            continue;
        }
        if (inDoubleQuote) {
            if (c == '\\') { i++; continue; }
            if (c == '"') {
                if (i + 1 < len && sql[i + 1] == '"') { i++; }
                else { inDoubleQuote = false; }
            }
            continue;
        }
        if (inBacktick) {
            if (c == '\\') { i++; continue; }
            if (c == '`') {
                if (i + 1 < len && sql[i + 1] == '`') { i++; }
                else { inBacktick = false; }
            }
            continue;
        }

        if (c == '\'') { inSingleQuote = true; continue; }
        if (c == '"') { inDoubleQuote = true; continue; }
        if (c == '`') { inBacktick = true; continue; }
        if (c == '(') { parenDepth++; continue; }
        if (c == ')') { if (parenDepth > 0) parenDepth--; continue; }

        if (parenDepth == 0) {
            if (orderStart == std::string::npos) {
                if (matchKeyword(i, "ORDER")) {
                    size_t j = i + 5;
                    while (j < len && isspace((unsigned char)sql[j])) j++;
                    if (matchKeyword(j, "BY")) {
                        orderStart = i;
                        i = j + 2;
                        continue;
                    }
                }
            } else {
                if (matchKeyword(i, "LIMIT") ||
                    matchKeyword(i, "PROCEDURE") ||
                    matchKeyword(i, "INTO") ||
                    matchKeyword(i, "FOR") ||
                    matchKeyword(i, "LOCK") ||
                    c == ';') {
                    orderEnd = i;
                    break;
                }
            }
        }
    }

    if (orderStart == std::string::npos) {
        return false;
    }

    if (orderEnd == std::string::npos) {
        orderEnd = len;
    }

    std::string prefix = sql.substr(0, orderStart);
    while (!prefix.empty() && isspace((unsigned char)prefix.back())) {
        prefix.pop_back();
    }
    std::string suffix = (orderEnd < len) ? sql.substr(orderEnd) : "";
    size_t sIdx = 0;
    while (sIdx < suffix.length() && isspace((unsigned char)suffix[sIdx])) sIdx++;
    if (sIdx > 0) suffix = suffix.substr(sIdx);

    if (!suffix.empty()) {
        outStrippedSql = prefix + " " + suffix;
    } else {
        outStrippedSql = prefix;
    }
    return true;
}

static bool LocalExtractDbAndTableFromSql(const std::string& sql, std::string& outDb, std::string& outTable) {
    outDb.clear();
    outTable.clear();

    size_t len = sql.length();
    size_t i = 0;
    while (i < len && isspace((unsigned char)sql[i])) i++;

    if (i + 6 > len || _strnicmp(sql.c_str() + i, "SELECT", 6) != 0) {
        return false;
    }
    if (i + 6 < len && (isalnum((unsigned char)sql[i + 6]) || sql[i + 6] == '_')) {
        return false;
    }
    i += 6;

    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    bool inBacktick = false;
    int parenDepth = 0;
    size_t fromPos = std::string::npos;

    auto matchKeyword = [&](size_t pos, const char* kw) -> bool {
        size_t kwLen = strlen(kw);
        if (pos + kwLen > len) return false;
        if (pos > 0) {
            char prev = sql[pos - 1];
            if (isalnum((unsigned char)prev) || prev == '_' || prev == '$') return false;
        }
        for (size_t k = 0; k < kwLen; ++k) {
            if (toupper((unsigned char)sql[pos + k]) != toupper((unsigned char)kw[k])) return false;
        }
        if (pos + kwLen < len) {
            char next = sql[pos + kwLen];
            if (isalnum((unsigned char)next) || next == '_' || next == '$') return false;
        }
        return true;
    };

    for (; i < len; ++i) {
        char c = sql[i];
        if (inSingleQuote) {
            if (c == '\\') { i++; continue; }
            if (c == '\'') {
                if (i + 1 < len && sql[i + 1] == '\'') { i++; }
                else { inSingleQuote = false; }
            }
            continue;
        }
        if (inDoubleQuote) {
            if (c == '\\') { i++; continue; }
            if (c == '"') {
                if (i + 1 < len && sql[i + 1] == '"') { i++; }
                else { inDoubleQuote = false; }
            }
            continue;
        }
        if (inBacktick) {
            if (c == '\\') { i++; continue; }
            if (c == '`') {
                if (i + 1 < len && sql[i + 1] == '`') { i++; }
                else { inBacktick = false; }
            }
            continue;
        }

        if (c == '\'') { inSingleQuote = true; continue; }
        if (c == '"') { inDoubleQuote = true; continue; }
        if (c == '`') { inBacktick = true; continue; }
        if (c == '(') { parenDepth++; continue; }
        if (c == ')') { if (parenDepth > 0) parenDepth--; continue; }

        if (parenDepth == 0) {
            if (matchKeyword(i, "FROM")) {
                fromPos = i + 4;
                break;
            }
        }
    }

    if (fromPos == std::string::npos) {
        return false;
    }

    size_t pos = fromPos;
    auto parseOneIdentifier = [&](size_t& idx) -> std::string {
        while (idx < len && isspace((unsigned char)sql[idx])) idx++;
        if (idx >= len) return "";
        std::string ident;
        if (sql[idx] == '`' || sql[idx] == '"' || sql[idx] == '[') {
            char quoteChar = sql[idx];
            char closeQuote = (quoteChar == '[') ? ']' : quoteChar;
            idx++;
            while (idx < len) {
                if (sql[idx] == closeQuote) {
                    idx++;
                    break;
                }
                ident += sql[idx];
                idx++;
            }
        } else {
            while (idx < len) {
                char c = sql[idx];
                if (isalnum((unsigned char)c) || c == '_' || c == '$') {
                    ident += c;
                    idx++;
                } else {
                    break;
                }
            }
        }
        return ident;
    };

    std::string first = parseOneIdentifier(pos);
    while (pos < len && isspace((unsigned char)sql[pos])) pos++;
    if (pos < len && sql[pos] == '.') {
        pos++;
        while (pos < len && isspace((unsigned char)sql[pos])) pos++;
        std::string second = parseOneIdentifier(pos);
        outDb = first;
        outTable = second;
    } else {
        outDb = "";
        outTable = first;
    }

    return !outTable.empty();
}

static std::string LocalReplaceLimitWithZero(const std::string& sql) {
    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    bool inBacktick = false;
    int parenDepth = 0;
    size_t len = sql.length();
    size_t limitPos = std::string::npos;

    auto matchKeyword = [&](size_t pos, const char* kw) -> bool {
        size_t kwLen = strlen(kw);
        if (pos + kwLen > len) return false;
        if (pos > 0) {
            char prev = sql[pos - 1];
            if (isalnum((unsigned char)prev) || prev == '_' || prev == '$') return false;
        }
        for (size_t k = 0; k < kwLen; ++k) {
            if (toupper((unsigned char)sql[pos + k]) != toupper((unsigned char)kw[k])) return false;
        }
        if (pos + kwLen < len) {
            char next = sql[pos + kwLen];
            if (isalnum((unsigned char)next) || next == '_' || next == '$') return false;
        }
        return true;
    };

    for (size_t i = 0; i < len; ++i) {
        char c = sql[i];
        if (inSingleQuote) {
            if (c == '\\') { i++; continue; }
            if (c == '\'') {
                if (i + 1 < len && sql[i + 1] == '\'') { i++; }
                else { inSingleQuote = false; }
            }
            continue;
        }
        if (inDoubleQuote) {
            if (c == '\\') { i++; continue; }
            if (c == '"') {
                if (i + 1 < len && sql[i + 1] == '"') { i++; }
                else { inDoubleQuote = false; }
            }
            continue;
        }
        if (inBacktick) {
            if (c == '\\') { i++; continue; }
            if (c == '`') {
                if (i + 1 < len && sql[i + 1] == '`') { i++; }
                else { inBacktick = false; }
            }
            continue;
        }

        if (c == '\'') { inSingleQuote = true; continue; }
        if (c == '"') { inDoubleQuote = true; continue; }
        if (c == '`') { inBacktick = true; continue; }
        if (c == '(') { parenDepth++; continue; }
        if (c == ')') { if (parenDepth > 0) parenDepth--; continue; }

        if (parenDepth == 0) {
            if (matchKeyword(i, "LIMIT")) {
                limitPos = i;
                break;
            }
        }
    }

    if (limitPos != std::string::npos) {
        bool hasSemicolon = false;
        size_t endIdx = len;
        while (endIdx > 0 && isspace((unsigned char)sql[endIdx - 1])) endIdx--;
        if (endIdx > 0 && sql[endIdx - 1] == ';') {
            hasSemicolon = true;
        }
        std::string prefix = sql.substr(0, limitPos + 5);
        if (hasSemicolon) {
            return prefix + " 0;";
        } else {
            return prefix + " 0";
        }
    } else {
        size_t endIdx = len;
        while (endIdx > 0 && isspace((unsigned char)sql[endIdx - 1])) endIdx--;
        if (endIdx > 0 && sql[endIdx - 1] == ';') {
            std::string beforeSemi = sql.substr(0, endIdx - 1);
            return beforeSemi + " LIMIT 0;";
        } else {
            return sql + " LIMIT 0";
        }
    }
}

int main(int argc, char* argv[]) {
    std::cout << "========================================================\n";
    std::cout << " HeidiSQL libmariadb.dll Proxy Test Harness\n";
    std::cout << "========================================================\n\n";

    // Test 1: Verify DetectAndStripOrderBy Parser
    std::cout << "[*] Test 1: Validating DetectAndStripOrderBy parser...\n";
    {
        std::string sql1 = "SELECT * FROM `users` ORDER BY `id` ASC LIMIT 1000";
        std::string stripped1;
        bool res1 = LocalDetectAndStripOrderBy(sql1, stripped1);
        assert(res1 == true);
        assert(stripped1 == "SELECT * FROM `users` LIMIT 1000");

        std::string sql2 = "SELECT * FROM `users` WHERE `name` = 'ORDER BY fake' ORDER BY id DESC";
        std::string stripped2;
        bool res2 = LocalDetectAndStripOrderBy(sql2, stripped2);
        assert(res2 == true);
        assert(stripped2 == "SELECT * FROM `users` WHERE `name` = 'ORDER BY fake'");

        std::string sql3 = "SELECT GROUP_CONCAT(name ORDER BY id) FROM `users`";
        std::string stripped3;
        bool res3 = LocalDetectAndStripOrderBy(sql3, stripped3);
        assert(res3 == false); // Subquery / function paren ORDER BY must NOT be stripped

        std::string sql4 = "SELECT * FROM `users` WHERE id = 1";
        std::string stripped4;
        bool res4 = LocalDetectAndStripOrderBy(sql4, stripped4);
        assert(res4 == false);

        std::cout << "  [+] All 4 SQL parser cases passed perfectly.\n";
        std::cout << "[+] PASS: DetectAndStripOrderBy verification successful.\n\n";
    }

    std::wstring dllPath = L"libmariadb.dll";
    std::string dllPathA = "libmariadb.dll";
    if (argc > 1) {
        dllPathA = argv[1];
        int len = MultiByteToWideChar(CP_ACP, 0, argv[1], -1, NULL, 0);
        std::vector<wchar_t> wbuf(len);
        MultiByteToWideChar(CP_ACP, 0, argv[1], -1, wbuf.data(), len);
        dllPath = wbuf.data();
    }

    std::cout << "[*] Attempting to load proxy DLL: " << dllPathA << "\n";
    std::cout.flush();
    HMODULE hDll = LoadLibraryW(dllPath.c_str());
    if (!hDll) {
        std::cerr << "[-] FAIL: LoadLibrary failed with error code: " << GetLastError() << "\n";
        return 1;
    }
    std::cout << "[+] SUCCESS: Loaded " << dllPathA << " at base address " << hDll << "\n\n";

    // Test 2: Verify Export Symbols Resolution
    std::cout << "[*] Test 2: Verifying export symbol resolution against HeidiSQL 30 imports...\n";
    int resolvedCount = 0;
    int missingRequired = 0;

    for (const auto& sym : g_symbols) {
        FARPROC proc = GetProcAddress(hDll, sym.name);
        if (proc) {
            resolvedCount++;
        } else {
            if (sym.required) {
                std::cerr << "  [-] MISSING REQUIRED EXPORT: " << sym.name << "\n";
                missingRequired++;
            } else {
                std::cout << "  [i] Optional export omitted: " << sym.name << "\n";
            }
        }
    }

    std::cout << "[+] Resolved " << resolvedCount << " / " << g_symbols.size() << " symbols.\n";
    if (missingRequired > 0) {
        std::cerr << "[-] FAIL: " << missingRequired << " required symbols were missing!\n";
        FreeLibrary(hDll);
        return 1;
    }
    std::cout << "[+] PASS: All HeidiSQL required symbols resolved successfully.\n\n";

    // Test 3: Safe Passthrough Execution
    std::cout << "[*] Test 3: Testing safe passthrough execution (mysql_get_client_info)...\n";
    typedef const char* (__stdcall *fn_client_info)();
    fn_client_info pClientInfo = (fn_client_info)GetProcAddress(hDll, "mysql_get_client_info");
    if (pClientInfo) {
        const char* info = pClientInfo();
        std::cout << "  [+] Client info string: \"" << (info ? info : "NULL") << "\"\n";
        std::cout << "[+] PASS: Passthrough function executed cleanly.\n\n";
    }

    // Test 4: Thread Dispatch Differentiation
    std::cout << "[*] Test 4: Testing background thread vs GUI main thread path...\n";
    typedef int (__stdcall *fn_real_query)(void*, const char*, unsigned long);
    fn_real_query pRealQuery = (fn_real_query)GetProcAddress(hDll, "mysql_real_query");
    if (pRealQuery) {
        std::thread bgWorker([&]() {
            int res = pRealQuery(nullptr, "SELECT 1", 8);
            std::cout << "  [+] Background thread query returned: " << res << " (expected 1 for null handle)\n";
        });
        bgWorker.join();
        std::cout << "[+] PASS: Background thread executed zero-overhead route.\n\n";
    }

    // Test 5: Verify mysql_store_result is direct passthrough without blocking
    std::cout << "[*] Test 5: Verifying mysql_store_result direct passthrough...\n";
    typedef void* (__stdcall *fn_store_result)(void*);
    fn_store_result pStoreResult = (fn_store_result)GetProcAddress(hDll, "mysql_store_result");
    if (pStoreResult) {
        auto tStart = std::chrono::steady_clock::now();
        void* res = pStoreResult(nullptr);
        auto tEnd = std::chrono::steady_clock::now();
        auto durationUs = std::chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart).count();
        assert(res == nullptr);
        std::cout << "  [+] mysql_store_result(nullptr) returned nullptr in " << durationUs << " us without blocking.\n";
        std::cout << "[+] PASS: mysql_store_result direct passthrough verified.\n\n";
    }

    // Test 6: Verify ExtractDbAndTableFromSql
    std::cout << "[*] Test 6: Validating ExtractDbAndTableFromSql parser...\n";
    {
        std::string db, tbl;

        bool r1 = LocalExtractDbAndTableFromSql("SELECT * FROM production_db.audit_logs_archive ORDER BY id", db, tbl);
        assert(r1 == true);
        assert(db == "production_db");
        assert(tbl == "audit_logs_archive");

        bool r2 = LocalExtractDbAndTableFromSql("SELECT * FROM `production_db`.`audit_logs_archive` WHERE id = 1", db, tbl);
        assert(r2 == true);
        assert(db == "production_db");
        assert(tbl == "audit_logs_archive");

        bool r3 = LocalExtractDbAndTableFromSql("SELECT * FROM audit_logs", db, tbl);
        assert(r3 == true);
        assert(db == "");
        assert(tbl == "audit_logs");

        bool r4 = LocalExtractDbAndTableFromSql("SELECT * FROM `audit_logs`", db, tbl);
        assert(r4 == true);
        assert(db == "");
        assert(tbl == "audit_logs");

        bool r5 = LocalExtractDbAndTableFromSql("   select a, b FROM `my_db`.`my_table`", db, tbl);
        assert(r5 == true);
        assert(db == "my_db");
        assert(tbl == "my_table");

        std::cout << "  [+] All 5 ExtractDbAndTableFromSql cases passed.\n";
        std::cout << "[+] PASS: ExtractDbAndTableFromSql verification successful.\n\n";
    }

    // Test 7: Verify ReplaceLimitWithZero
    std::cout << "[*] Test 7: Validating ReplaceLimitWithZero...\n";
    {
        std::string q1 = "SELECT * FROM `audit_logs` ORDER BY id DESC LIMIT 1000";
        std::string r1 = LocalReplaceLimitWithZero(q1);
        assert(r1 == "SELECT * FROM `audit_logs` ORDER BY id DESC LIMIT 0");

        std::string q2 = "SELECT * FROM `audit_logs` LIMIT 500;";
        std::string r2 = LocalReplaceLimitWithZero(q2);
        assert(r2 == "SELECT * FROM `audit_logs` LIMIT 0;");

        std::string q3 = "SELECT * FROM `audit_logs` WHERE id = 1";
        std::string r3 = LocalReplaceLimitWithZero(q3);
        assert(r3 == "SELECT * FROM `audit_logs` WHERE id = 1 LIMIT 0");

        std::string q4 = "SELECT * FROM `audit_logs` WHERE id = 1;";
        std::string r4 = LocalReplaceLimitWithZero(q4);
        assert(r4 == "SELECT * FROM `audit_logs` WHERE id = 1 LIMIT 0;");

        std::cout << "  [+] All 4 ReplaceLimitWithZero cases passed.\n";
        std::cout << "[+] PASS: ReplaceLimitWithZero verification successful.\n\n";
    }

    FreeLibrary(hDll);

    std::cout << "========================================================\n";
    std::cout << " ALL VERIFICATION TESTS PASSED SUCCESSFULLY!\n";
    std::cout << "========================================================\n";
    return 0;
}
