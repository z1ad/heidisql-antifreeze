#pragma once
#ifndef ASYNC_ENGINE_H
#define ASYNC_ENGINE_H

#include <windows.h>
#include <functional>
#include <atomic>
#include <chrono>
#include <string>

extern DWORD g_dwMainThreadId;
extern HMODULE g_hOrigDll;

typedef int (__stdcall *fn_mariadb_cancel)(void* mysql);
typedef unsigned long (__stdcall *fn_mysql_thread_id)(void* mysql);
typedef unsigned char (__stdcall *fn_mysql_thread_init)(void);
typedef void (__stdcall *fn_mysql_thread_end)(void);
typedef void* (__stdcall *fn_mysql_init)(void* Handle);
typedef void* (__stdcall *fn_mysql_real_connect)(void* Handle, const char* Host, const char* User, const char* Passwd, const char* Db, unsigned int Port, const char* UnixSocket, unsigned long ClientFlag);
typedef int (__stdcall *fn_mysql_real_query)(void* Handle, const char* Query, unsigned long Length);
typedef void (__stdcall *fn_mysql_close)(void* Handle);
typedef int (__stdcall *fn_mysql_options)(void* Handle, int Option, const void* arg);
typedef int (__stdcall *fn_mysql_optionsv)(void* Handle, int Option, const void* arg, const void* val);
typedef unsigned int (__stdcall *fn_mysql_errno)(void* Handle);
typedef const char* (__stdcall *fn_mysql_error)(void* Handle);
typedef void* (__stdcall *fn_mysql_store_result)(void* Handle);
typedef void (__stdcall *fn_mysql_free_result)(void* Result);
typedef int (__stdcall *fn_mysql_next_result)(void* Handle);
typedef int (__stdcall *fn_mysql_ping)(void* Handle);
typedef int (__stdcall *fn_mysql_reset_connection)(void* Handle);
typedef char** (__stdcall *fn_mysql_fetch_row)(void* Result);
typedef unsigned long* (__stdcall *fn_mysql_fetch_lengths)(void* Result);
typedef int (__stdcall *fn_mysql_num_fields)(void* Result);
typedef uint64_t (__stdcall *fn_mysql_num_rows)(void* Result);

extern fn_mariadb_cancel g_real_mariadb_cancel;
extern fn_mysql_thread_id g_real_mysql_thread_id;
extern fn_mysql_thread_init g_real_mysql_thread_init;
extern fn_mysql_thread_end g_real_mysql_thread_end;
extern fn_mysql_init g_real_mysql_init;
extern fn_mysql_real_connect g_real_mysql_real_connect;
extern fn_mysql_real_query g_real_mysql_real_query;
extern fn_mysql_close g_real_mysql_close;
extern fn_mysql_options g_real_mysql_options;
extern fn_mysql_optionsv g_real_mysql_optionsv;
extern fn_mysql_error g_real_mysql_error;
extern fn_mysql_errno g_real_mysql_errno;
extern fn_mysql_store_result g_real_mysql_store_result;
extern fn_mysql_free_result g_real_mysql_free_result;
extern fn_mysql_next_result g_real_mysql_next_result;
extern fn_mysql_ping g_real_mysql_ping;
extern fn_mysql_reset_connection g_real_mysql_reset_connection;
extern fn_mysql_fetch_row g_real_mysql_fetch_row;
extern fn_mysql_fetch_lengths g_real_mysql_fetch_lengths;
extern fn_mysql_num_fields g_real_mysql_num_fields;
extern fn_mysql_num_rows g_real_mysql_num_rows;

void AsyncEngine_Init(HMODULE hOrigDll, DWORD mainThreadId);
void AsyncEngine_Shutdown();

void AsyncEngine_RecordOption(void* handle, int option, const void* arg);
void AsyncEngine_RecordOptionV(void* handle, int option, const void* arg, const void* val);
void AsyncEngine_SaveConnection(void* handle, const char* host, const char* user, const char* passwd, const char* db, unsigned int port, const char* unixSocket, unsigned long clientFlag, unsigned long threadId);
void AsyncEngine_RemoveConnection(void* handle);
void AsyncEngine_KillQuery(void* mysqlHandle, bool isFullCancel);
void AsyncEngine_KillSpecificThread(void* mysqlHandle, unsigned long targetThreadId);

bool DetectAndStripOrderBy(const std::string& sql, std::string& outStrippedSql);
bool ExtractDbAndTableFromSql(const std::string& sql, std::string& outDb, std::string& outTable);
void ClearHeidiTableSortInRegistry(const std::string& dbName, const std::string& tableName);
std::string ReplaceLimitWithZero(const std::string& sql);

void LogDebug(const char* fmt, ...);

int ExecuteQueryWithOverlay(
    void* mysqlHandle,
    const std::string& query,
    bool hasOrderBy,
    const std::string& strippedQuery
);

#endif // ASYNC_ENGINE_H
