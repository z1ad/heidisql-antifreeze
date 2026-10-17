# HeidiSQL AntiFreeze ❄️➡️⚡

> **A high-performance, drop-in DLL proxy for HeidiSQL that permanently eliminates UI freezes, Windows "(Not Responding)" ghosting, and lock contention hangs on MySQL / MariaDB / AWS RDS.**
>
> *Note: Engineered specifically to eliminate HeidiSQL data tab freezes, unindexed `ORDER BY` lockups, and RDS metadata lock contention. While based on the standard `libmariadb.dll` C connector interface, other client applications are currently untested.*

---

## The Problem

HeidiSQL is a fast, beloved database client written in Delphi (VCL). However, when opening large tables, sorting columns in the **Data** tab, or running queries against tables with active metadata locks:
1. **Single-Threaded Delphi GUI Freeze**: HeidiSQL executes data browsing queries on the main GUI thread. When a query is slow, the entire Windows event pump halts.
2. **Windows DWM Ghosting**: After 5 seconds of inactivity, Windows marks HeidiSQL as **"(Not Responding)"** and replaces the window with an unresponsive ghost bitmap.
3. **Unstoppable Sorts**: If an unindexed column is sorted on a multi-million row table, HeidiSQL blocks indefinitely while the server performs a full filesort.
4. **Metadata Lock Deadlocks**: If another transaction holds an exclusive metadata lock (e.g., `ALTER TABLE` or uncommitted DDL), HeidiSQL hangs indefinitely with no indication of what is blocking it.
5. **The Persistent Sort Trap**: HeidiSQL saves table sort orders in the Windows Registry (`HKEY_CURRENT_USER\Software\HeidiSQL\Servers\...`). Even if you kill the application, re-opening that table immediately re-executes the exact same slow sort!

---

## The Solution: HeidiSQL AntiFreeze

`HeidiSQL AntiFreeze` is a zero-configuration, drop-in replacement for HeidiSQL's `libmariadb.dll`. It intercepts database calls on the GUI thread, offloading slow executions to background workers while running an isolated Win32 message loop with a live, responsive modal overlay.

```
+-------------------------------------------------------------------------------+
|                      HeidiSQL Delphi GUI Main Thread                         |
|                                     |                                         |
|                  calls mysql_real_query(...) on libmariadb.dll               |
+-------------------------------------+-----------------------------------------+
                                      |
                                      v
+-------------------------------------------------------------------------------+
|                       HeidiDefrost Proxy DLL Engine                           |
|                                                                               |
|  [ Fast-Path: 0 - 1000ms ]          [ Slow-Path: > 1000ms ]                   |
|  Synchronous pass-through           Spawns dedicated Win32 UI overlay         |
|  Zero overhead or flicker           Shields Delphi inputs (prevents AV crash) |
|                                     Monitors performance_schema locks         |
|                                     Provides 1-click [Cancel] & [Rerun]       |
+-------------------------------------------------------------------------------+
                                      |
         +----------------------------+----------------------------+
         |                                                         |
         v                                                         v
+----------------------------------+     +--------------------------------------+
|    Original libmariadb_orig.dll  |     |   Dedicated Helper Connection        |
|  Direct connection to MySQL/RDS  |     |  Executes KILL QUERY / rds_kill      |
+----------------------------------+     +--------------------------------------+
```

---

## Key Features

- **⚡ Zero UI Freezes & No DWM Ghosting**: Calls `DisableProcessWindowsGhosting()` on startup. Fast queries (< 1.0s) execute with zero overhead; slower queries automatically trigger a dedicated, responsive Win32 overlay with a live second counter.
- **🛑 True Server-Side Cancellation**: Clicking `[Cancel Query]` connects via an auxiliary helper connection and issues native `KILL QUERY <thread_id>` or AWS RDS `CALL mysql.rds_kill_query(<thread_id>)`. The running query is immediately killed on MySQL without corrupting or tearing down the client TCP socket.
- **🔍 Real-Time Metadata Lock Detection**: While waiting on slow queries, a background monitor inspects `information_schema.PROCESSLIST` and `performance_schema.metadata_locks`. If your query is waiting on a lock, the overlay turns amber and displays the exact blocker:
  ```text
  ⚠️ Table is locked by Connection #42981 (app_user@10.0.1.45): ALTER TABLE audit_logs ADD COLUMN ...
  [ Kill Blocker #42981 ]
  ```
  You can terminate the blocking query on the server with a single click.
- **🚀 One-Click `[Rerun Without ORDER BY]` with Auto-Registry Cleanup**:
  - Automatically parses and strips slow top-level `ORDER BY` clauses.
  - Deletes the persisted `"Sort"` and `"Filter"` keys from HeidiSQL's registry (`HKCU\Software\HeidiSQL\Servers\<Session>\<Db>|<Table>`).
  - Displays the rows in natural order immediately, saving you from having to manually find the table, right-click, and click "Clear data tab filter".
- **🛡️ Cancel Bounce Suppression**: Prevents Delphi focus change events and modal error dialogs from triggering an automatic duplicate re-run on cancel. Suppressed bounce queries execute a fast `LIMIT 0` in < 0.2ms, cleanly returning 0 rows without displaying error dialogs.
- **🔒 Input Shielding**: Uses `EnableWindow(hParent, FALSE)` while slow queries run, preventing accidental clicks on tabs, menus, or grid headers that would cause Delphi VCL re-entrancy crashes.

---

## Installation

### Method 1: Using `deploy.bat` (Recommended)

1. Ensure HeidiSQL is closed.
2. Clone this repository and run `build.bat` using MSVC x64:
   ```cmd
   build.bat
   ```
3. Deploy directly to your HeidiSQL installation directory:
   ```cmd
   deploy.bat
   ```
   *(This automatically creates backups `libmariadb_orig.dll` and `libmariadb.dll.bak` before installing).*

### Method 2: Manual Installation

1. Navigate to your HeidiSQL directory (typically `C:\Users\<User>\AppData\Local\Programs\HeidiSQL\` or `C:\Program Files\HeidiSQL\`).
2. Rename `libmariadb.dll` to `libmariadb_orig.dll`.
3. Copy the compiled `bin\libmariadb.dll` from this project into your HeidiSQL directory.
4. Launch HeidiSQL.

---

## Verification & Testing

This project includes a comprehensive automated test harness in [`src/test_harness.cpp`](src/test_harness.cpp) covering 7 validation suites:
- **Test 1**: Top-level `ORDER BY` parser and subquery protection.
- **Test 2**: 31/31 export symbol resolution matching HeidiSQL imports.
- **Test 3**: Passthrough execution stability (`mysql_get_client_info`).
- **Test 4**: Background thread vs. GUI thread routing.
- **Test 5**: `mysql_store_result` direct non-blocking streaming.
- **Test 6**: SQL database and table identifier extractor.
- **Test 7**: `ReplaceLimitWithZero` query rewriting.

To run the verification suite:
```cmd
test.bat
```

---

## Rollback / Uninstallation

To restore HeidiSQL's original library at any time:
```cmd
restore.bat
```
or manually rename `libmariadb_orig.dll` back to `libmariadb.dll`.

---

## Architecture & Project Structure

```text
├── src/
│   ├── proxy_main.cpp      # DLL entry point (DllMain), export table, forwarders
│   ├── async_engine.h      # Core engine header and public prototypes
│   ├── async_engine.cpp    # Worker thread dispatcher, registry cleaner, lock monitor
│   ├── ui_overlay.h        # Dedicated Win32 modal overlay header
│   ├── ui_overlay.cpp      # Isolated background UI message loop, buttons, styling
│   ├── exports.def         # 31 explicit symbols exported by libmariadb.dll
│   └── test_harness.cpp    # Standalone validation and verification test suite
├── build.bat               # One-click MSVC x64 build script
├── deploy.bat              # Safe deployment script with automatic backups
├── restore.bat             # Instant rollback script
├── test.bat                # Automated verification test runner
├── LICENSE                 # MIT License
└── README.md
```

---

## Compatibility

- **HeidiSQL**: Tested and verified on HeidiSQL 12.x (x64).
- **Databases**: MySQL 5.7 / 8.0+, MariaDB 10.x+, AWS RDS MySQL & Aurora.
- **Operating Systems**: Windows 10, Windows 11, Windows Server (x64).

---

## License

This project is licensed under the [MIT License](LICENSE).
