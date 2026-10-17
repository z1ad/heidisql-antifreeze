#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "async_engine.h"

extern "C" WINUSERAPI VOID WINAPI DisableProcessWindowsGhosting(VOID);

HMODULE g_hProxyModule = NULL;
static HMODULE g_hOriginalDll = NULL;

// Raw function pointer definitions for the 30 HeidiSQL imports
typedef uint64_t (__stdcall *t_mysql_affected_rows)(void* Handle);
typedef const char* (__stdcall *t_mysql_character_set_name)(void* Handle);
typedef void (__stdcall *t_mysql_close)(void* Handle);
typedef void (__stdcall *t_mysql_data_seek)(void* Result, uint64_t Offset);
typedef unsigned int (__stdcall *t_mysql_errno)(void* Handle);
typedef const char* (__stdcall *t_mysql_error)(void* Handle);
typedef void* (__stdcall *t_mysql_fetch_field_direct)(void* Result, unsigned int FieldNo);
typedef void* (__stdcall *t_mysql_fetch_field)(void* Result);
typedef unsigned long* (__stdcall *t_mysql_fetch_lengths)(void* Result);
typedef char** (__stdcall *t_mysql_fetch_row)(void* Result);
typedef void (__stdcall *t_mysql_free_result)(void* Result);
typedef const char* (__stdcall *t_mysql_get_client_info)(void);
typedef const char* (__stdcall *t_mysql_get_server_info)(void* Handle);
typedef void* (__stdcall *t_mysql_init)(void* Handle);
typedef const char* (__stdcall *t_mysql_info)(void* Handle);
typedef int (__stdcall *t_mysql_num_fields)(void* Result);
typedef uint64_t (__stdcall *t_mysql_num_rows)(void* Result);
typedef int (__stdcall *t_mysql_options)(void* Handle, int Option, const void* arg);
typedef int (__stdcall *t_mysql_optionsv)(void* Handle, int Option, const void* arg, const void* val);
typedef int (__stdcall *t_mysql_ping)(void* Handle);
typedef void* (__stdcall *t_mysql_real_connect)(void* Handle, const char* Host, const char* User, const char* Passwd, const char* Db, unsigned int Port, const char* UnixSocket, unsigned long ClientFlag);
typedef int (__stdcall *t_mysql_real_query)(void* Handle, const char* Query, unsigned long Length);
typedef const char* (__stdcall *t_mysql_stat)(void* Handle);
typedef void* (__stdcall *t_mysql_store_result)(void* Handle);
typedef unsigned long (__stdcall *t_mysql_thread_id)(void* Handle);
typedef int (__stdcall *t_mysql_next_result)(void* Handle);
typedef int (__stdcall *t_mysql_set_character_set)(void* Handle, const char* csname);
typedef unsigned char (__stdcall *t_mysql_thread_init)(void);
typedef void (__stdcall *t_mysql_thread_end)(void);
typedef unsigned int (__stdcall *t_mysql_warning_count)(void* Handle);
typedef int (__stdcall *t_mariadb_cancel)(void* Handle);

// Resolved target pointers
static t_mysql_affected_rows real_mysql_affected_rows = nullptr;
static t_mysql_character_set_name real_mysql_character_set_name = nullptr;
static t_mysql_close real_mysql_close = nullptr;
static t_mysql_data_seek real_mysql_data_seek = nullptr;
static t_mysql_errno real_mysql_errno = nullptr;
static t_mysql_error real_mysql_error = nullptr;
static t_mysql_fetch_field_direct real_mysql_fetch_field_direct = nullptr;
static t_mysql_fetch_field real_mysql_fetch_field = nullptr;
static t_mysql_fetch_lengths real_mysql_fetch_lengths = nullptr;
static t_mysql_fetch_row real_mysql_fetch_row = nullptr;
static t_mysql_free_result real_mysql_free_result = nullptr;
static t_mysql_get_client_info real_mysql_get_client_info = nullptr;
static t_mysql_get_server_info real_mysql_get_server_info = nullptr;
static t_mysql_init real_mysql_init = nullptr;
static t_mysql_info real_mysql_info = nullptr;
static t_mysql_num_fields real_mysql_num_fields = nullptr;
static t_mysql_num_rows real_mysql_num_rows = nullptr;
static t_mysql_options real_mysql_options = nullptr;
static t_mysql_optionsv real_mysql_optionsv = nullptr;
static t_mysql_ping real_mysql_ping = nullptr;
static t_mysql_real_connect real_mysql_real_connect = nullptr;
static t_mysql_real_query real_mysql_real_query = nullptr;
static t_mysql_stat real_mysql_stat = nullptr;
static t_mysql_store_result real_mysql_store_result = nullptr;
static t_mysql_thread_id real_mysql_thread_id = nullptr;
static t_mysql_next_result real_mysql_next_result = nullptr;
static t_mysql_set_character_set real_mysql_set_character_set = nullptr;
static t_mysql_thread_init real_mysql_thread_init = nullptr;
static t_mysql_thread_end real_mysql_thread_end = nullptr;
static t_mysql_warning_count real_mysql_warning_count = nullptr;
static t_mariadb_cancel real_mariadb_cancel = nullptr;

static void LoadOriginalDll() {
    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(g_hProxyModule, modulePath, MAX_PATH);

    std::wstring pathStr(modulePath);
    size_t lastSlash = pathStr.find_last_of(L"\\/");
    std::wstring dir = (lastSlash != std::wstring::npos) ? pathStr.substr(0, lastSlash + 1) : L"";

    std::vector<std::wstring> candidates = {
        dir + L"libmariadb_orig.dll",
        dir + L"libmariadb.dll.orig",
        dir + L"libmariadb_real.dll"
    };

    for (const auto& candidate : candidates) {
        g_hOriginalDll = LoadLibraryW(candidate.c_str());
        if (g_hOriginalDll) break;
    }

    if (!g_hOriginalDll) {
        OutputDebugStringA("[HeidiSQL-Proxy] Warning: Could not find original libmariadb_orig.dll\n");
        return;
    }

    #define BIND_PROC(name) real_##name = (t_##name)GetProcAddress(g_hOriginalDll, #name)
    BIND_PROC(mysql_affected_rows);
    BIND_PROC(mysql_character_set_name);
    BIND_PROC(mysql_close);
    BIND_PROC(mysql_data_seek);
    BIND_PROC(mysql_errno);
    BIND_PROC(mysql_error);
    BIND_PROC(mysql_fetch_field_direct);
    BIND_PROC(mysql_fetch_field);
    BIND_PROC(mysql_fetch_lengths);
    BIND_PROC(mysql_fetch_row);
    BIND_PROC(mysql_free_result);
    BIND_PROC(mysql_get_client_info);
    BIND_PROC(mysql_get_server_info);
    BIND_PROC(mysql_init);
    BIND_PROC(mysql_info);
    BIND_PROC(mysql_num_fields);
    BIND_PROC(mysql_num_rows);
    BIND_PROC(mysql_options);
    BIND_PROC(mysql_optionsv);
    BIND_PROC(mysql_ping);
    BIND_PROC(mysql_real_connect);
    BIND_PROC(mysql_real_query);
    BIND_PROC(mysql_stat);
    BIND_PROC(mysql_store_result);
    BIND_PROC(mysql_thread_id);
    BIND_PROC(mysql_next_result);
    BIND_PROC(mysql_set_character_set);
    BIND_PROC(mysql_thread_init);
    BIND_PROC(mysql_thread_end);
    BIND_PROC(mysql_warning_count);
    BIND_PROC(mariadb_cancel);
    #undef BIND_PROC

    AsyncEngine_Init(g_hOriginalDll, g_dwMainThreadId);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH: {
        DisableProcessWindowsGhosting();
        g_hProxyModule = hinstDLL;
        g_dwMainThreadId = GetCurrentThreadId();

        wchar_t hostExe[MAX_PATH];
        GetModuleFileNameW(NULL, hostExe, MAX_PATH);
        std::wstring exeStr(hostExe);
        size_t slash = exeStr.find_last_of(L"\\/");
        std::wstring exeName = (slash != std::wstring::npos) ? exeStr.substr(slash + 1) : exeStr;
        for (auto& c : exeName) c = towlower(c);

        if (exeName == L"heidisql.exe") {
            OutputDebugStringA("[HeidiSQL-Proxy] Attached to HeidiSQL process!\n");
        }

        LoadOriginalDll();
        break;
    }
    case DLL_PROCESS_DETACH:
        AsyncEngine_Shutdown();
        if (g_hOriginalDll) {
            FreeLibrary(g_hOriginalDll);
            g_hOriginalDll = NULL;
        }
        break;
    }
    return TRUE;
}

// ---------------- Intercepted Functions ----------------

extern "C" int __stdcall proxy_mysql_real_query(void* Handle, const char* Query, unsigned long Length) {
    if (!real_mysql_real_query) return 1;
    if (!Handle) return 1;

    // Fast background thread passthrough
    if (GetCurrentThreadId() != g_dwMainThreadId) {
        return real_mysql_real_query(Handle, Query, Length);
    }

    // Check for ORDER BY clause
    std::string queryStr(Query, Length);
    std::string strippedQuery;
    bool hasOrderBy = DetectAndStripOrderBy(queryStr, strippedQuery);

    std::string preview = queryStr.length() > 120 ? queryStr.substr(0, 120) + "..." : queryStr;
    LogDebug("[proxy_mysql_real_query] Main thread query: %s (hasOrderBy: %d)", preview.c_str(), hasOrderBy ? 1 : 0);

    return ExecuteQueryWithOverlay(
        Handle,
        queryStr,
        hasOrderBy,
        strippedQuery
    );
}

extern "C" void* __stdcall proxy_mysql_store_result(void* Handle) {
    if (!Handle || !real_mysql_store_result) return nullptr;
    return real_mysql_store_result(Handle);
}

// ---------------- Passthrough & Tracking Functions ----------------

extern "C" uint64_t __stdcall proxy_mysql_affected_rows(void* Handle) {
    return real_mysql_affected_rows ? real_mysql_affected_rows(Handle) : 0;
}

extern "C" const char* __stdcall proxy_mysql_character_set_name(void* Handle) {
    return real_mysql_character_set_name ? real_mysql_character_set_name(Handle) : "utf8mb4";
}

extern "C" void __stdcall proxy_mysql_close(void* Handle) {
    AsyncEngine_RemoveConnection(Handle);
    if (real_mysql_close) real_mysql_close(Handle);
}

extern "C" void __stdcall proxy_mysql_data_seek(void* Result, uint64_t Offset) {
    if (real_mysql_data_seek) real_mysql_data_seek(Result, Offset);
}

extern "C" unsigned int __stdcall proxy_mysql_errno(void* Handle) {
    return real_mysql_errno ? real_mysql_errno(Handle) : 0;
}

extern "C" const char* __stdcall proxy_mysql_error(void* Handle) {
    return real_mysql_error ? real_mysql_error(Handle) : "";
}

extern "C" void* __stdcall proxy_mysql_fetch_field_direct(void* Result, unsigned int FieldNo) {
    return real_mysql_fetch_field_direct ? real_mysql_fetch_field_direct(Result, FieldNo) : nullptr;
}

extern "C" void* __stdcall proxy_mysql_fetch_field(void* Result) {
    return real_mysql_fetch_field ? real_mysql_fetch_field(Result) : nullptr;
}

extern "C" unsigned long* __stdcall proxy_mysql_fetch_lengths(void* Result) {
    return real_mysql_fetch_lengths ? real_mysql_fetch_lengths(Result) : nullptr;
}

extern "C" char** __stdcall proxy_mysql_fetch_row(void* Result) {
    return real_mysql_fetch_row ? real_mysql_fetch_row(Result) : nullptr;
}

extern "C" void __stdcall proxy_mysql_free_result(void* Result) {
    if (real_mysql_free_result) real_mysql_free_result(Result);
}

extern "C" const char* __stdcall proxy_mysql_get_client_info(void) {
    return real_mysql_get_client_info ? real_mysql_get_client_info() : "libmariadb proxy 3.3.5";
}

extern "C" const char* __stdcall proxy_mysql_get_server_info(void* Handle) {
    return real_mysql_get_server_info ? real_mysql_get_server_info(Handle) : "Unknown";
}

extern "C" void* __stdcall proxy_mysql_init(void* Handle) {
    return real_mysql_init ? real_mysql_init(Handle) : nullptr;
}

extern "C" const char* __stdcall proxy_mysql_info(void* Handle) {
    return real_mysql_info ? real_mysql_info(Handle) : "";
}

extern "C" int __stdcall proxy_mysql_num_fields(void* Result) {
    return real_mysql_num_fields ? real_mysql_num_fields(Result) : 0;
}

extern "C" uint64_t __stdcall proxy_mysql_num_rows(void* Result) {
    return real_mysql_num_rows ? real_mysql_num_rows(Result) : 0;
}

extern "C" int __stdcall proxy_mysql_options(void* Handle, int Option, const void* arg) {
    AsyncEngine_RecordOption(Handle, Option, arg);
    return real_mysql_options ? real_mysql_options(Handle, Option, arg) : 0;
}

extern "C" int __stdcall proxy_mysql_optionsv(void* Handle, int Option, const void* arg, const void* val) {
    AsyncEngine_RecordOptionV(Handle, Option, arg, val);
    return real_mysql_optionsv ? real_mysql_optionsv(Handle, Option, arg, val) : 0;
}

extern "C" int __stdcall proxy_mysql_ping(void* Handle) {
    return real_mysql_ping ? real_mysql_ping(Handle) : 0;
}

extern "C" void* __stdcall proxy_mysql_real_connect(void* Handle, const char* Host, const char* User, const char* Passwd, const char* Db, unsigned int Port, const char* UnixSocket, unsigned long ClientFlag) {
    if (!real_mysql_real_connect) return nullptr;
    void* res = real_mysql_real_connect(Handle, Host, User, Passwd, Db, Port, UnixSocket, ClientFlag);
    if (res) {
        unsigned long threadId = 0;
        if (real_mysql_thread_id) {
            threadId = real_mysql_thread_id(Handle);
        }
        LogDebug("[Connect] Connected to %s:%u, db: %s, threadId: %lu", Host ? Host : "", Port, Db ? Db : "", threadId);
        if (real_mysql_real_query) {
            real_mysql_real_query(res, "SET SESSION lock_wait_timeout = 15", 33);
        }
        AsyncEngine_SaveConnection(Handle, Host, User, Passwd, Db, Port, UnixSocket, ClientFlag, threadId);
    }
    return res;
}

extern "C" const char* __stdcall proxy_mysql_stat(void* Handle) {
    return real_mysql_stat ? real_mysql_stat(Handle) : "";
}

extern "C" unsigned long __stdcall proxy_mysql_thread_id(void* Handle) {
    return real_mysql_thread_id ? real_mysql_thread_id(Handle) : 0;
}

extern "C" int __stdcall proxy_mysql_next_result(void* Handle) {
    return real_mysql_next_result ? real_mysql_next_result(Handle) : -1;
}

extern "C" int __stdcall proxy_mysql_set_character_set(void* Handle, const char* csname) {
    return real_mysql_set_character_set ? real_mysql_set_character_set(Handle, csname) : 0;
}

extern "C" unsigned char __stdcall proxy_mysql_thread_init(void) {
    return real_mysql_thread_init ? real_mysql_thread_init() : 0;
}

extern "C" void __stdcall proxy_mysql_thread_end(void) {
    if (real_mysql_thread_end) real_mysql_thread_end();
}

extern "C" unsigned int __stdcall proxy_mysql_warning_count(void* Handle) {
    return real_mysql_warning_count ? real_mysql_warning_count(Handle) : 0;
}

extern "C" int __stdcall proxy_mariadb_cancel(void* Handle) {
    return real_mariadb_cancel ? real_mariadb_cancel(Handle) : 0;
}
