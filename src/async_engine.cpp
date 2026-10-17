#pragma comment(lib, "advapi32.lib")
#include "async_engine.h"
#include "ui_overlay.h"
#include <thread>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <cctype>
#include <cwctype>
#include <cstring>
#include <cstdio>

DWORD g_dwMainThreadId = 0;
HMODULE g_hOrigDll = NULL;
fn_mariadb_cancel g_real_mariadb_cancel = nullptr;
fn_mysql_thread_id g_real_mysql_thread_id = nullptr;
fn_mysql_thread_init g_real_mysql_thread_init = nullptr;
fn_mysql_thread_end g_real_mysql_thread_end = nullptr;
fn_mysql_init g_real_mysql_init = nullptr;
fn_mysql_real_connect g_real_mysql_real_connect = nullptr;
fn_mysql_real_query g_real_mysql_real_query = nullptr;
fn_mysql_close g_real_mysql_close = nullptr;
fn_mysql_options g_real_mysql_options = nullptr;
fn_mysql_optionsv g_real_mysql_optionsv = nullptr;
fn_mysql_error g_real_mysql_error = nullptr;
fn_mysql_errno g_real_mysql_errno = nullptr;
fn_mysql_store_result g_real_mysql_store_result = nullptr;
fn_mysql_free_result g_real_mysql_free_result = nullptr;
fn_mysql_next_result g_real_mysql_next_result = nullptr;
fn_mysql_ping g_real_mysql_ping = nullptr;
fn_mysql_reset_connection g_real_mysql_reset_connection = nullptr;
fn_mysql_fetch_row g_real_mysql_fetch_row = nullptr;
fn_mysql_fetch_lengths g_real_mysql_fetch_lengths = nullptr;
fn_mysql_num_fields g_real_mysql_num_fields = nullptr;
fn_mysql_num_rows g_real_mysql_num_rows = nullptr;

struct SavedConnectionInfo {
    std::string host;
    std::string user;
    std::string password;
    std::string db;
    unsigned int port = 3306;
    std::string unixSocket;
    unsigned long clientFlag = 0;
    unsigned long threadId = 0;

    // Recorded connection options
    std::string tlsVersion;
    std::string sslKey;
    std::string sslCert;
    std::string sslCa;
    std::string sslCipher;
    std::string pluginDir;
    int sslMode = -1;
    int verifyServerCert = -1;
    int cleartext = -1;
};

static std::mutex g_connMutex;
static std::map<void*, SavedConnectionInfo> g_connections;

void LogDebug(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Send formatted message to system debug output
    OutputDebugStringA(buf);
}

void AsyncEngine_Init(HMODULE hOrigDll, DWORD mainThreadId) {
    g_hOrigDll = hOrigDll;
    g_dwMainThreadId = mainThreadId;
    if (hOrigDll) {
        g_real_mariadb_cancel = (fn_mariadb_cancel)GetProcAddress(hOrigDll, "mariadb_cancel");
        g_real_mysql_thread_id = (fn_mysql_thread_id)GetProcAddress(hOrigDll, "mysql_thread_id");
        g_real_mysql_thread_init = (fn_mysql_thread_init)GetProcAddress(hOrigDll, "mysql_thread_init");
        g_real_mysql_thread_end = (fn_mysql_thread_end)GetProcAddress(hOrigDll, "mysql_thread_end");
        g_real_mysql_init = (fn_mysql_init)GetProcAddress(hOrigDll, "mysql_init");
        g_real_mysql_real_connect = (fn_mysql_real_connect)GetProcAddress(hOrigDll, "mysql_real_connect");
        g_real_mysql_real_query = (fn_mysql_real_query)GetProcAddress(hOrigDll, "mysql_real_query");
        g_real_mysql_close = (fn_mysql_close)GetProcAddress(hOrigDll, "mysql_close");
        g_real_mysql_options = (fn_mysql_options)GetProcAddress(hOrigDll, "mysql_options");
        g_real_mysql_optionsv = (fn_mysql_optionsv)GetProcAddress(hOrigDll, "mysql_optionsv");
        g_real_mysql_error = (fn_mysql_error)GetProcAddress(hOrigDll, "mysql_error");
        g_real_mysql_errno = (fn_mysql_errno)GetProcAddress(hOrigDll, "mysql_errno");
        g_real_mysql_store_result = (fn_mysql_store_result)GetProcAddress(hOrigDll, "mysql_store_result");
        g_real_mysql_free_result = (fn_mysql_free_result)GetProcAddress(hOrigDll, "mysql_free_result");
        g_real_mysql_next_result = (fn_mysql_next_result)GetProcAddress(hOrigDll, "mysql_next_result");
        g_real_mysql_ping = (fn_mysql_ping)GetProcAddress(hOrigDll, "mysql_ping");
        g_real_mysql_reset_connection = (fn_mysql_reset_connection)GetProcAddress(hOrigDll, "mysql_reset_connection");
        g_real_mysql_fetch_row = (fn_mysql_fetch_row)GetProcAddress(hOrigDll, "mysql_fetch_row");
        g_real_mysql_fetch_lengths = (fn_mysql_fetch_lengths)GetProcAddress(hOrigDll, "mysql_fetch_lengths");
        g_real_mysql_num_fields = (fn_mysql_num_fields)GetProcAddress(hOrigDll, "mysql_num_fields");
        g_real_mysql_num_rows = (fn_mysql_num_rows)GetProcAddress(hOrigDll, "mysql_num_rows");
    }
}

void AsyncEngine_Shutdown() {
    std::lock_guard<std::mutex> lock(g_connMutex);
    g_connections.clear();
}

void AsyncEngine_RecordOption(void* handle, int option, const void* arg) {
    if (!handle || !arg) return;
    std::lock_guard<std::mutex> lock(g_connMutex);
    auto& info = g_connections[handle];

    switch (option) {
    case 21: // MYSQL_OPT_SSL_VERIFY_SERVER_CERT (MariaDB)
    case 32: // MYSQL_OPT_SSL_VERIFY_SERVER_CERT (MySQL)
        info.verifyServerCert = *(const unsigned char*)arg;
        break;
    case 25: // MYSQL_OPT_SSL_KEY (MariaDB)
    case 19: // MYSQL_OPT_SSL_KEY (MySQL 8)
        info.sslKey = (const char*)arg;
        break;
    case 26: // MYSQL_OPT_SSL_CERT (MariaDB)
    case 20: // MYSQL_OPT_SSL_CERT (MySQL 8)
        info.sslCert = (const char*)arg;
        break;
    case 27: // MYSQL_OPT_SSL_CA (MariaDB)
        info.sslCa = (const char*)arg;
        break;
    case 29: // MYSQL_OPT_SSL_CIPHER (MariaDB)
    case 23: // MYSQL_OPT_SSL_CIPHER (MySQL 8)
        info.sslCipher = (const char*)arg;
        break;
    case 22: // MYSQL_PLUGIN_DIR (MariaDB)
    case 16: // MYSQL_PLUGIN_DIR (MySQL 8)
        info.pluginDir = (const char*)arg;
        break;
    case 36: // MYSQL_ENABLE_CLEARTEXT_PLUGIN (MariaDB)
    case 30: // MYSQL_ENABLE_CLEARTEXT_PLUGIN (MySQL 8)
        info.cleartext = *(const unsigned char*)arg;
        break;
    case 41: // MYSQL_OPT_TLS_VERSION
    case 7005: // MARIADB_OPT_TLS_VERSION
    case 34: // MYSQL_OPT_TLS_VERSION (MySQL 8)
        info.tlsVersion = (const char*)arg;
        break;
    case 35: // MYSQL_OPT_SSL_MODE (MySQL 8)
        info.sslMode = *(const unsigned int*)arg;
        break;
    default:
        break;
    }
}

void AsyncEngine_RecordOptionV(void* handle, int option, const void* arg, const void* /*val*/) {
    AsyncEngine_RecordOption(handle, option, arg);
}

void AsyncEngine_SaveConnection(void* handle, const char* host, const char* user, const char* passwd, const char* db, unsigned int port, const char* unixSocket, unsigned long clientFlag, unsigned long threadId) {
    if (!handle) return;
    std::lock_guard<std::mutex> lock(g_connMutex);
    auto& info = g_connections[handle];
    info.host = host ? host : "";
    info.user = user ? user : "";
    info.password = passwd ? passwd : "";
    info.db = db ? db : "";
    info.port = port;
    info.unixSocket = unixSocket ? unixSocket : "";
    info.clientFlag = clientFlag;
    info.threadId = threadId;
}

void AsyncEngine_RemoveConnection(void* handle) {
    if (!handle) return;
    std::lock_guard<std::mutex> lock(g_connMutex);
    g_connections.erase(handle);
}

static void* CreateHelperConnection(const SavedConnectionInfo& info) {
    if (!g_real_mysql_init || !g_real_mysql_real_connect || !g_real_mysql_close) {
        return nullptr;
    }

    void* conn = g_real_mysql_init(NULL);
    if (!conn) return nullptr;

    unsigned int timeout = 3;
    if (g_real_mysql_options) {
        g_real_mysql_options(conn, 0 /* MYSQL_OPT_CONNECT_TIMEOUT */, &timeout);

        // Explicitly disable MYSQL_OPT_SSL_VERIFY_SERVER_CERT
        unsigned char verify = 0;
        g_real_mysql_options(conn, 21 /* MARIADB verify */, &verify);
        g_real_mysql_options(conn, 32 /* MYSQL verify */, &verify);

        if (!info.tlsVersion.empty()) {
            g_real_mysql_options(conn, 41, info.tlsVersion.c_str());
            g_real_mysql_options(conn, 7005, info.tlsVersion.c_str());
        }
        if (!info.sslKey.empty()) {
            g_real_mysql_options(conn, 25, info.sslKey.c_str());
            g_real_mysql_options(conn, 19, info.sslKey.c_str());
        }
        if (!info.sslCert.empty()) {
            g_real_mysql_options(conn, 26, info.sslCert.c_str());
            g_real_mysql_options(conn, 20, info.sslCert.c_str());
        }
        if (!info.sslCa.empty()) {
            g_real_mysql_options(conn, 27, info.sslCa.c_str());
            g_real_mysql_options(conn, 21, info.sslCa.c_str());
        }
        if (!info.sslCipher.empty()) {
            g_real_mysql_options(conn, 29, info.sslCipher.c_str());
            g_real_mysql_options(conn, 23, info.sslCipher.c_str());
        }
        if (!info.pluginDir.empty()) {
            g_real_mysql_options(conn, 22, info.pluginDir.c_str());
        }
        if (info.cleartext > 0) {
            unsigned char ct = 1;
            g_real_mysql_options(conn, 36, &ct);
        }
        if (info.sslMode > 0) {
            unsigned int mode = info.sslMode;
            g_real_mysql_options(conn, 35, &mode);
        }
    }

    void* connected = g_real_mysql_real_connect(
        conn,
        info.host.empty() ? NULL : info.host.c_str(),
        info.user.empty() ? NULL : info.user.c_str(),
        info.password.empty() ? NULL : info.password.c_str(),
        NULL,
        info.port,
        info.unixSocket.empty() ? NULL : info.unixSocket.c_str(),
        info.clientFlag
    );

    if (!connected) {
        const char* connErr = g_real_mysql_error ? g_real_mysql_error(conn) : "unknown error";
        LogDebug("[CreateHelperConnection] Connect failed to %s:%u: %s",
            info.host.c_str(), info.port, connErr);
        g_real_mysql_close(conn);
        return nullptr;
    }

    return connected;
}

void AsyncEngine_KillQuery(void* mysqlHandle, bool isFullCancel) {
    if (!mysqlHandle) return;

    unsigned long threadId = 0;
    if (g_real_mysql_thread_id) {
        threadId = g_real_mysql_thread_id(mysqlHandle);
    }

    SavedConnectionInfo info;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(g_connMutex);
        auto it = g_connections.find(mysqlHandle);
        if (it != g_connections.end()) {
            info = it->second;
            found = true;
        }
    }

    if (threadId == 0 && found) {
        threadId = info.threadId;
    }

    LogDebug("[AsyncEngine_KillQuery] Initiating kill for handle: %p, threadId: %lu, isFullCancel: %d",
        mysqlHandle, threadId, (int)isFullCancel);

    if (found && threadId > 0) {
        if (g_real_mysql_thread_init) {
            g_real_mysql_thread_init();
        }

        void* killer = CreateHelperConnection(info);
        if (killer) {
            LogDebug("[AsyncEngine_KillQuery] Helper connected to %s:%u. Attempting kill commands...",
                info.host.c_str(), info.port);

            char killCmd[128];
            bool killed = false;

            // Attempt 1: KILL QUERY <threadId>
            snprintf(killCmd, sizeof(killCmd), "KILL QUERY %lu", threadId);
            int res = g_real_mysql_real_query(killer, killCmd, (unsigned long)strlen(killCmd));
            const char* err = (res != 0 && g_real_mysql_error) ? g_real_mysql_error(killer) : "none";
            LogDebug("[AsyncEngine_KillQuery] Executed '%s' -> res: %d, err: %s", killCmd, res, err);
            if (res == 0) killed = true;

            // Attempt 2: CALL mysql.rds_kill_query(<threadId>)
            if (!killed) {
                snprintf(killCmd, sizeof(killCmd), "CALL mysql.rds_kill_query(%lu)", threadId);
                res = g_real_mysql_real_query(killer, killCmd, (unsigned long)strlen(killCmd));
                err = (res != 0 && g_real_mysql_error) ? g_real_mysql_error(killer) : "none";
                LogDebug("[AsyncEngine_KillQuery] Executed '%s' -> res: %d, err: %s", killCmd, res, err);
                if (res == 0) killed = true;
            }

            // Attempt 3: KILL <threadId>
            if (!killed) {
                snprintf(killCmd, sizeof(killCmd), "KILL %lu", threadId);
                res = g_real_mysql_real_query(killer, killCmd, (unsigned long)strlen(killCmd));
                err = (res != 0 && g_real_mysql_error) ? g_real_mysql_error(killer) : "none";
                LogDebug("[AsyncEngine_KillQuery] Executed '%s' -> res: %d, err: %s", killCmd, res, err);
                if (res == 0) killed = true;
            }

            // Attempt 4: CALL mysql.rds_kill(<threadId>)
            if (!killed) {
                snprintf(killCmd, sizeof(killCmd), "CALL mysql.rds_kill(%lu)", threadId);
                res = g_real_mysql_real_query(killer, killCmd, (unsigned long)strlen(killCmd));
                err = (res != 0 && g_real_mysql_error) ? g_real_mysql_error(killer) : "none";
                LogDebug("[AsyncEngine_KillQuery] Executed '%s' -> res: %d, err: %s", killCmd, res, err);
            }

            g_real_mysql_close(killer);
        }

        if (g_real_mysql_thread_end) {
            g_real_mysql_thread_end();
        }
    } else {
        LogDebug("[AsyncEngine_KillQuery] Cannot use helper connection (found=%d, threadId=%lu)",
            (int)found, threadId);
    }

    // Client socket preservation:
    // Never call mariadb_cancel() because that shuts down the TCP socket and breaks subsequent
    // HeidiSQL operations (such as Process List, database tree navigation, and refresh).
    // The server-side KILL commands above terminate the running query on MySQL,
    // sending ER_QUERY_INTERRUPTED while keeping the client connection alive and healthy.
    LogDebug("[AsyncEngine_KillQuery] Server kill complete. Preserving client socket for connection health.");
}

void AsyncEngine_KillSpecificThread(void* mysqlHandle, unsigned long targetThreadId) {
    if (!mysqlHandle || targetThreadId == 0) return;
    LogDebug("[AsyncEngine_KillSpecificThread] Killing target blocker thread ID: %lu", targetThreadId);

    SavedConnectionInfo info;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(g_connMutex);
        auto it = g_connections.find(mysqlHandle);
        if (it != g_connections.end()) {
            info = it->second;
            found = true;
        }
    }

    if (!found) {
        LogDebug("[AsyncEngine_KillSpecificThread] No connection info for handle: %p", mysqlHandle);
        return;
    }

    if (g_real_mysql_thread_init) {
        g_real_mysql_thread_init();
    }

    void* killer = CreateHelperConnection(info);
    if (killer) {
        char killCmd[128];
        bool killed = false;

        // 1. KILL <targetThreadId> (terminates blocker connection, rolling back transaction and immediately releasing metadata locks)
        snprintf(killCmd, sizeof(killCmd), "KILL %lu", targetThreadId);
        int res = g_real_mysql_real_query(killer, killCmd, (unsigned long)strlen(killCmd));
        const char* err = (res != 0 && g_real_mysql_error) ? g_real_mysql_error(killer) : "none";
        LogDebug("[AsyncEngine_KillSpecificThread] Executed '%s' -> res: %d, err: %s", killCmd, res, err);
        if (res == 0) killed = true;

        // 2. CALL mysql.rds_kill(<targetThreadId>)
        if (!killed) {
            snprintf(killCmd, sizeof(killCmd), "CALL mysql.rds_kill(%lu)", targetThreadId);
            res = g_real_mysql_real_query(killer, killCmd, (unsigned long)strlen(killCmd));
            err = (res != 0 && g_real_mysql_error) ? g_real_mysql_error(killer) : "none";
            LogDebug("[AsyncEngine_KillSpecificThread] Executed '%s' -> res: %d, err: %s", killCmd, res, err);
            if (res == 0) killed = true;
        }

        // 3. KILL QUERY <targetThreadId>
        if (!killed) {
            snprintf(killCmd, sizeof(killCmd), "KILL QUERY %lu", targetThreadId);
            res = g_real_mysql_real_query(killer, killCmd, (unsigned long)strlen(killCmd));
            err = (res != 0 && g_real_mysql_error) ? g_real_mysql_error(killer) : "none";
            LogDebug("[AsyncEngine_KillSpecificThread] Executed '%s' -> res: %d, err: %s", killCmd, res, err);
            if (res == 0) killed = true;
        }

        // 4. CALL mysql.rds_kill_query(<targetThreadId>)
        if (!killed) {
            snprintf(killCmd, sizeof(killCmd), "CALL mysql.rds_kill_query(%lu)", targetThreadId);
            res = g_real_mysql_real_query(killer, killCmd, (unsigned long)strlen(killCmd));
            err = (res != 0 && g_real_mysql_error) ? g_real_mysql_error(killer) : "none";
            LogDebug("[AsyncEngine_KillSpecificThread] Executed '%s' -> res: %d, err: %s", killCmd, res, err);
        }

        g_real_mysql_close(killer);
    }

    if (g_real_mysql_thread_end) {
        g_real_mysql_thread_end();
    }
}

bool DetectAndStripOrderBy(const std::string& sql, std::string& outStrippedSql) {
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

bool ExtractDbAndTableFromSql(const std::string& sql, std::string& outDb, std::string& outTable) {
    outDb.clear();
    outTable.clear();

    size_t len = sql.length();
    size_t i = 0;
    while (i < len && isspace((unsigned char)sql[i])) i++;

    // Verify query starts with SELECT (case-insensitive)
    if (i + 6 > len || _strnicmp(sql.c_str() + i, "SELECT", 6) != 0) {
        return false;
    }
    if (i + 6 < len && (isalnum((unsigned char)sql[i + 6]) || sql[i + 6] == '_')) {
        return false;
    }
    i += 6;

    // Scan for FROM keyword outside quotes and parentheses
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

void ClearHeidiTableSortInRegistry(const std::string& dbName, const std::string& tableName) {
    if (tableName.empty()) return;

    auto toLowerW = [](const std::string& s) -> std::wstring {
        if (s.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.length(), NULL, 0);
        std::wstring ws(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.length(), &ws[0], len);
        for (auto& wc : ws) {
            wc = (wchar_t)towlower(wc);
        }
        return ws;
    };

    std::wstring targetDbLower = toLowerW(dbName);
    std::wstring targetTableLower = toLowerW(tableName);

    HKEY hServersKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\HeidiSQL\\Servers", 0, KEY_READ, &hServersKey) != ERROR_SUCCESS) {
        LogDebug("[ClearHeidiTableSortInRegistry] Could not open HKCU\\Software\\HeidiSQL\\Servers");
        return;
    }

    DWORD sessionIndex = 0;
    wchar_t sessionName[256];
    DWORD sessionNameLen = 256;
    while (RegEnumKeyExW(hServersKey, sessionIndex++, sessionName, &sessionNameLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
        sessionNameLen = 256;
        HKEY hSessionKey = NULL;
        if (RegOpenKeyExW(hServersKey, sessionName, 0, KEY_READ, &hSessionKey) == ERROR_SUCCESS) {
            DWORD subkeyIndex = 0;
            wchar_t subkeyName[512];
            DWORD subkeyNameLen = 512;
            while (RegEnumKeyExW(hSessionKey, subkeyIndex++, subkeyName, &subkeyNameLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                subkeyNameLen = 512;
                std::wstring subkeyStr(subkeyName);
                size_t pipePos = subkeyStr.find(L'|');
                if (pipePos != std::wstring::npos) {
                    std::wstring regDb = subkeyStr.substr(0, pipePos);
                    std::wstring regTable = subkeyStr.substr(pipePos + 1);
                    for (auto& c : regDb) c = (wchar_t)towlower(c);
                    for (auto& c : regTable) c = (wchar_t)towlower(c);

                    bool tableMatch = (regTable == targetTableLower);
                    bool dbMatch = targetDbLower.empty() || regDb.empty() || (regDb == targetDbLower);

                    if (tableMatch && dbMatch) {
                        HKEY hTableKey = NULL;
                        if (RegOpenKeyExW(hSessionKey, subkeyName, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hTableKey) == ERROR_SUCCESS) {
                            LSTATUS sSort = RegDeleteValueW(hTableKey, L"Sort");
                            LSTATUS sFilter = RegDeleteValueW(hTableKey, L"Filter");
                            LogDebug("[ClearHeidiTableSortInRegistry] Cleared Sort (status: %ld) and Filter (status: %ld) for %S in session %S",
                                sSort, sFilter, subkeyName, sessionName);
                            RegCloseKey(hTableKey);
                        }
                    }
                }
            }
            RegCloseKey(hSessionKey);
        }
    }
    RegCloseKey(hServersKey);
}

std::string ReplaceLimitWithZero(const std::string& sql) {
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

static std::string g_lastCancelledDb;
static std::string g_lastCancelledTable;
static std::string g_lastCancelledQuery;
static std::chrono::steady_clock::time_point g_lastCancelledTime;
static std::mutex g_cancelHistoryMutex;

int ExecuteQueryWithOverlay(
    void* mysqlHandle,
    const std::string& query,
    bool hasOrderBy,
    const std::string& strippedQuery
) {
    // Check if a slow query on this table was just cancelled within 15 seconds (suppress bounce)
    std::string curDb, curTable;
    if (ExtractDbAndTableFromSql(query, curDb, curTable) && !curTable.empty()) {
        std::lock_guard<std::mutex> lock(g_cancelHistoryMutex);
        if (!g_lastCancelledTable.empty()) {
            auto now = std::chrono::steady_clock::now();
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastCancelledTime).count();
            if (elapsedMs < 15000) {
                bool tableMatch = (_stricmp(curTable.c_str(), g_lastCancelledTable.c_str()) == 0);
                bool dbMatch = curDb.empty() || g_lastCancelledDb.empty() || (_stricmp(curDb.c_str(), g_lastCancelledDb.c_str()) == 0);
                if (tableMatch && dbMatch && hasOrderBy) {
                    LogDebug("[ExecuteQueryWithOverlay] Suppressed duplicate bounce of query on table %s (%lld ms ago) - executing fast LIMIT 0",
                        curTable.c_str(), elapsedMs);
                    std::string fastEmptyQuery = ReplaceLimitWithZero(strippedQuery);
                    if (g_real_mysql_real_query) {
                        return g_real_mysql_real_query(mysqlHandle, fastEmptyQuery.c_str(), (unsigned long)fastEmptyQuery.length());
                    }
                    return 0;
                }
            } else {
                g_lastCancelledTable.clear();
                g_lastCancelledDb.clear();
                g_lastCancelledQuery.clear();
            }
        }
    }

    HANDLE hDoneEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!hDoneEvent) {
        return g_real_mysql_real_query ? g_real_mysql_real_query(mysqlHandle, query.c_str(), (unsigned long)query.length()) : 1;
    }

    std::atomic<bool> isFinished{ false };
    std::atomic<bool> isCancelled{ false };
    std::atomic<bool> rerunRequested{ false };
    std::atomic<HWND> overlayHwnd{ NULL };
    std::atomic<bool> stopMonitor{ false };

    int queryResult = 1;

    // 1. Worker thread for query execution
    std::thread worker([&]() {
        if (g_real_mysql_thread_init) {
            g_real_mysql_thread_init();
        }

        try {
            if (g_real_mysql_real_query) {
                queryResult = g_real_mysql_real_query(mysqlHandle, query.c_str(), (unsigned long)query.length());
            }

            int initialErrno = g_real_mysql_errno ? g_real_mysql_errno(mysqlHandle) : 0;
            const char* initialErr = g_real_mysql_error ? g_real_mysql_error(mysqlHandle) : "";
            LogDebug("[Worker] Initial query returned: %d, errno: %d, err: %s", queryResult, initialErrno, initialErr);

            if (rerunRequested.load() && !strippedQuery.empty()) {
                if (g_real_mysql_store_result) {
                    void* oldRes = g_real_mysql_store_result(mysqlHandle);
                    if (oldRes) {
                        LogDebug("[Worker] Drained pending result set from initial query");
                        if (g_real_mysql_free_result) {
                            g_real_mysql_free_result(oldRes);
                        }
                    }
                }
                if (g_real_mysql_next_result) {
                    while (g_real_mysql_next_result(mysqlHandle) == 0) {
                        if (g_real_mysql_store_result) {
                            void* extraRes = g_real_mysql_store_result(mysqlHandle);
                            if (extraRes && g_real_mysql_free_result) {
                                g_real_mysql_free_result(extraRes);
                            }
                        }
                    }
                }

                if (g_real_mysql_ping && g_real_mysql_ping(mysqlHandle) != 0) {
                    LogDebug("[Worker] Connection ping returned non-zero. Attempting mysql_reset_connection...");
                    if (g_real_mysql_reset_connection) {
                        int resetRes = g_real_mysql_reset_connection(mysqlHandle);
                        LogDebug("[Worker] mysql_reset_connection result: %d", resetRes);
                    }
                }

                LogDebug("[Worker] Executing stripped query without ORDER BY: %s", strippedQuery.c_str());
                if (g_real_mysql_real_query) {
                    queryResult = g_real_mysql_real_query(mysqlHandle, strippedQuery.c_str(), (unsigned long)strippedQuery.length());
                }
                int strippedErrno = g_real_mysql_errno ? g_real_mysql_errno(mysqlHandle) : 0;
                const char* strippedErr = g_real_mysql_error ? g_real_mysql_error(mysqlHandle) : "";
                LogDebug("[Worker] Stripped query completed with result: %d, errno: %d, err: %s", queryResult, strippedErrno, strippedErr);
            }
        } catch (...) {
            queryResult = 1;
        }

        isFinished.store(true);
        SetEvent(hDoneEvent);

        if (g_real_mysql_thread_end) {
            g_real_mysql_thread_end();
        }
    });

    // 2. Fast-path: wait up to 1000ms with zero message dispatch and zero background connections
    DWORD waitRes = WaitForSingleObject(hDoneEvent, 1000);
    if (waitRes == WAIT_OBJECT_0) {
        if (worker.joinable()) {
            worker.join();
        }
        CloseHandle(hDoneEvent);
        return queryResult;
    }

    // 3. Slow-path (> 1000ms): Retrieve Delphi parent window and shield input
    HWND hActiveParent = UIOverlay_FindActiveParent(g_dwMainThreadId);
    if (hActiveParent && IsWindow(hActiveParent)) {
        EnableWindow(hActiveParent, FALSE);
    }

    // 4. Spawn Dedicated Background UI Thread (overlayThread)
    UIOverlayParams overlayParams;
    overlayParams.hParent = hActiveParent;
    overlayParams.title = L"HeidiSQL - Executing Database Query";
    overlayParams.hasOrderBy = hasOrderBy;
    overlayParams.onCancel = [&]() {
        isCancelled.store(true);
        AsyncEngine_KillQuery(mysqlHandle, /*isFullCancel=*/ true);
    };
    overlayParams.onRerun = [&]() {
        rerunRequested.store(true);
        std::string dbName, tableName;
        ExtractDbAndTableFromSql(query, dbName, tableName);
        if (dbName.empty()) {
            std::lock_guard<std::mutex> lock(g_connMutex);
            auto it = g_connections.find(mysqlHandle);
            if (it != g_connections.end()) {
                dbName = it->second.db;
            }
        }
        ClearHeidiTableSortInRegistry(dbName, tableName);
        AsyncEngine_KillQuery(mysqlHandle, /*isFullCancel=*/ false);
    };
    overlayParams.onKillBlocker = [&](unsigned long blockerId) {
        AsyncEngine_KillSpecificThread(mysqlHandle, blockerId);
    };
    overlayParams.pOutHwnd = &overlayHwnd;

    std::thread overlayThread(UIOverlay_ThreadProc, overlayParams);

    // 4. Start Lock Monitor thread ONLY when query is genuinely slow (> 1000ms)
    std::thread lockMonitor([&]() {
        if (g_real_mysql_thread_init) {
            g_real_mysql_thread_init();
        }

        SavedConnectionInfo info;
        bool hasInfo = false;
        unsigned long threadId = 0;
        {
            std::lock_guard<std::mutex> lock(g_connMutex);
            auto it = g_connections.find(mysqlHandle);
            if (it != g_connections.end()) {
                info = it->second;
                threadId = info.threadId;
                hasInfo = true;
            }
        }
        if (threadId == 0 && g_real_mysql_thread_id) {
            threadId = g_real_mysql_thread_id(mysqlHandle);
            info.threadId = threadId;
        }

        if (hasInfo && threadId > 0 && g_real_mysql_init && g_real_mysql_real_connect && g_real_mysql_real_query && g_real_mysql_close) {
            void* monConn = CreateHelperConnection(info);
            if (monConn) {
                while (!stopMonitor.load()) {
                    char queryBuf[512];
                    snprintf(queryBuf, sizeof(queryBuf),
                        "SELECT STATE, INFO FROM information_schema.PROCESSLIST WHERE ID = %lu", threadId);

                    if (g_real_mysql_real_query(monConn, queryBuf, (unsigned long)strlen(queryBuf)) == 0) {
                        void* res = g_real_mysql_store_result ? g_real_mysql_store_result(monConn) : nullptr;
                        if (res) {
                            char** row = g_real_mysql_fetch_row ? g_real_mysql_fetch_row(res) : nullptr;
                            if (row && row[0]) {
                                std::string stateStr = row[0];
                                std::string stateLower = stateStr;
                                for (auto& c : stateLower) c = (char)tolower((unsigned char)c);

                                if (stateLower.find("metadata lock") != std::string::npos ||
                                    stateLower.find("table level lock") != std::string::npos ||
                                    stateLower.find("waiting for table") != std::string::npos) {
                                    
                                    LogDebug("[LockMonitor] Thread %lu is in lock wait state: %s", threadId, stateStr.c_str());

                                    unsigned long blockingId = 0;
                                    std::wstring warningMsg = L"Waiting for table metadata lock held by another transaction on RDS";

                                    // Primary inspection: performance_schema.metadata_locks
                                    char psQuery[1024];
                                    snprintf(psQuery, sizeof(psQuery),
                                        "SELECT p.PROCESSLIST_ID, p.PROCESSLIST_USER, p.PROCESSLIST_HOST, p.PROCESSLIST_INFO, p.PROCESSLIST_TIME "
                                        "FROM performance_schema.metadata_locks w "
                                        "JOIN performance_schema.metadata_locks b "
                                        "  ON w.OBJECT_SCHEMA = b.OBJECT_SCHEMA "
                                        " AND w.OBJECT_NAME = b.OBJECT_NAME "
                                        " AND w.OBJECT_TYPE = b.OBJECT_TYPE "
                                        " AND b.LOCK_STATUS = 'GRANTED' "
                                        "JOIN performance_schema.threads p "
                                        "  ON b.OWNER_THREAD_ID = p.THREAD_ID "
                                        "WHERE w.LOCK_STATUS = 'PENDING' "
                                        "  AND w.OWNER_THREAD_ID = (SELECT THREAD_ID FROM performance_schema.threads WHERE PROCESSLIST_ID = %lu) "
                                        "LIMIT 1", threadId);

                                    if (g_real_mysql_real_query(monConn, psQuery, (unsigned long)strlen(psQuery)) == 0) {
                                        void* psRes = g_real_mysql_store_result ? g_real_mysql_store_result(monConn) : nullptr;
                                        if (psRes) {
                                            char** psRow = g_real_mysql_fetch_row ? g_real_mysql_fetch_row(psRes) : nullptr;
                                            if (psRow && psRow[0]) {
                                                blockingId = strtoul(psRow[0], NULL, 10);
                                                const char* bUser = psRow[1] ? psRow[1] : "user";
                                                const char* bHost = psRow[2] ? psRow[2] : "host";
                                                const char* bInfo = psRow[3] ? psRow[3] : "transaction";

                                                wchar_t wBuf[512];
                                                swprintf_s(wBuf, L"Table is locked by Connection #%lu (%S@%S): %S",
                                                    blockingId, bUser, bHost, bInfo);
                                                warningMsg = wBuf;
                                            }
                                            if (g_real_mysql_free_result) g_real_mysql_free_result(psRes);
                                        }
                                    }

                                    // Fallback 1: information_schema.innodb_lock_waits
                                    if (blockingId == 0) {
                                        char fbQuery[1024];
                                        snprintf(fbQuery, sizeof(fbQuery),
                                            "SELECT b.trx_mysql_thread_id, p.USER, p.HOST, p.INFO, p.TIME "
                                            "FROM information_schema.innodb_lock_waits w "
                                            "JOIN information_schema.innodb_trx b ON w.blocking_trx_id = b.trx_id "
                                            "JOIN information_schema.PROCESSLIST p ON b.trx_mysql_thread_id = p.ID "
                                            "WHERE w.requesting_trx_id = (SELECT trx_id FROM information_schema.innodb_trx WHERE trx_mysql_thread_id = %lu) "
                                            "LIMIT 1", threadId);

                                        if (g_real_mysql_real_query(monConn, fbQuery, (unsigned long)strlen(fbQuery)) == 0) {
                                            void* fbRes = g_real_mysql_store_result ? g_real_mysql_store_result(monConn) : nullptr;
                                            if (fbRes) {
                                                char** fbRow = g_real_mysql_fetch_row ? g_real_mysql_fetch_row(fbRes) : nullptr;
                                                if (fbRow && fbRow[0]) {
                                                    blockingId = strtoul(fbRow[0], NULL, 10);
                                                    const char* bUser = fbRow[1] ? fbRow[1] : "user";
                                                    const char* bHost = fbRow[2] ? fbRow[2] : "host";
                                                    const char* bInfo = fbRow[3] ? fbRow[3] : "transaction";

                                                    wchar_t wBuf[512];
                                                    swprintf_s(wBuf, L"Table is locked by Connection #%lu (%S@%S): %S",
                                                        blockingId, bUser, bHost, bInfo);
                                                    warningMsg = wBuf;
                                                }
                                                if (g_real_mysql_free_result) g_real_mysql_free_result(fbRes);
                                            }
                                        }
                                    }

                                    // Fallback 2: performance_schema.data_lock_waits (MySQL 8.0+)
                                    if (blockingId == 0) {
                                        char dlwQuery[1024];
                                        snprintf(dlwQuery, sizeof(dlwQuery),
                                            "SELECT p.PROCESSLIST_ID, p.PROCESSLIST_USER, p.PROCESSLIST_HOST, p.PROCESSLIST_INFO, p.PROCESSLIST_TIME "
                                            "FROM performance_schema.data_lock_waits w "
                                            "JOIN performance_schema.threads p ON w.BLOCKING_THREAD_ID = p.THREAD_ID "
                                            "JOIN performance_schema.threads r ON w.REQUESTING_THREAD_ID = r.THREAD_ID "
                                            "WHERE r.PROCESSLIST_ID = %lu "
                                            "LIMIT 1", threadId);

                                        if (g_real_mysql_real_query(monConn, dlwQuery, (unsigned long)strlen(dlwQuery)) == 0) {
                                            void* dlwRes = g_real_mysql_store_result ? g_real_mysql_store_result(monConn) : nullptr;
                                            if (dlwRes) {
                                                char** dlwRow = g_real_mysql_fetch_row ? g_real_mysql_fetch_row(dlwRes) : nullptr;
                                                if (dlwRow && dlwRow[0]) {
                                                    blockingId = strtoul(dlwRow[0], NULL, 10);
                                                    const char* bUser = dlwRow[1] ? dlwRow[1] : "user";
                                                    const char* bHost = dlwRow[2] ? dlwRow[2] : "host";
                                                    const char* bInfo = dlwRow[3] ? dlwRow[3] : "transaction";

                                                    wchar_t wBuf[512];
                                                    swprintf_s(wBuf, L"Table is locked by Connection #%lu (%S@%S): %S",
                                                        blockingId, bUser, bHost, bInfo);
                                                    warningMsg = wBuf;
                                                }
                                                if (g_real_mysql_free_result) g_real_mysql_free_result(dlwRes);
                                            }
                                        }
                                    }

                                    HWND hCur = overlayHwnd.load();
                                    if (hCur && IsWindow(hCur)) {
                                        std::wstring* pText = new std::wstring(warningMsg);
                                        PostMessageW(hCur, WM_APP_LOCK_WARNING, (WPARAM)blockingId, (LPARAM)pText);
                                    }
                                }
                            }
                            if (g_real_mysql_free_result) g_real_mysql_free_result(res);
                        }
                    }

                    for (int i = 0; i < 15 && !stopMonitor.load(); ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }
                g_real_mysql_close(monConn);
            }
        }

        if (g_real_mysql_thread_end) {
            g_real_mysql_thread_end();
        }
    });

    // 6. Delphi main thread wait loop: drain WM_PAINT to keep Delphi refreshed without re-entrancy
    while (WaitForSingleObject(hDoneEvent, 50) == WAIT_TIMEOUT) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, WM_PAINT, WM_PAINT, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // 7. Cleanup & teardown
    stopMonitor.store(true);
    if (lockMonitor.joinable()) {
        lockMonitor.join();
    }

    HWND hCur = overlayHwnd.load();
    if (hCur && IsWindow(hCur)) {
        PostMessageW(hCur, WM_CLOSE, 0, 0);
    }

    if (overlayThread.joinable()) {
        overlayThread.join();
    }

    if (isCancelled.load()) {
        std::string cDb, cTable;
        ExtractDbAndTableFromSql(query, cDb, cTable);
        {
            std::lock_guard<std::mutex> lock(g_cancelHistoryMutex);
            g_lastCancelledDb = cDb;
            g_lastCancelledTable = cTable;
            g_lastCancelledQuery = query;
            g_lastCancelledTime = std::chrono::steady_clock::now();
            LogDebug("[ExecuteQueryWithOverlay] Recorded cancelled table %s (db: %s) to suppress bounce for 15s",
                cTable.c_str(), cDb.c_str());
        }

        // Purge any pending mouse clicks or keystrokes from Delphi's queue so accidental clicks don't re-select tree/grid
        MSG discardMsg;
        while (PeekMessageW(&discardMsg, NULL, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) {
            LogDebug("[ExecuteQueryWithOverlay] Purged pending mouse message: 0x%x", discardMsg.message);
        }
        while (PeekMessageW(&discardMsg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE)) {
            LogDebug("[ExecuteQueryWithOverlay] Purged pending key message: 0x%x", discardMsg.message);
        }
    }

    if (hActiveParent && IsWindow(hActiveParent)) {
        EnableWindow(hActiveParent, TRUE);
        SetActiveWindow(hActiveParent);
        SetForegroundWindow(hActiveParent);
        if (!isCancelled.load()) {
            RedrawWindow(hActiveParent, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
        }
    }

    if (worker.joinable()) {
        worker.join();
    }
    CloseHandle(hDoneEvent);

    return queryResult;
}
