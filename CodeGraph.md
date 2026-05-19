# CodeGraph Reference Manual — Ludo

CodeGraph is a tree-sitter-parsed AST knowledge graph of every symbol, edge, and
file in the project. Reads are sub-millisecond and return structural
information grep cannot provide.

**Index stats:** 838 files, 19,923 nodes, 49,995 edges, 36.5 MB DB.

---

## 1. Tools Overview

| Tool | Purpose | Returns |
|------|---------|---------|
| `codegraph_status` | Index health / stats | Node counts, DB size |
| `codegraph_files` | File listing by path / glob | Filtered tree or flat list |
| `codegraph_search` | Find symbol by name | Name, kind, file, line |
| `codegraph_node` | Get symbol detail | Signature, location, source code |
| `codegraph_callers` | Who calls this symbol | Caller list |
| `codegraph_callees` | What this symbol calls | Callee list |
| `codegraph_impact` | What would break if I change X | N-depth dependency fan-out |
| `codegraph_context` | Task-focused code map | Entry points + related symbols |
| `codegraph_explore` | Deep multi-file survey | Full source from relevant files |

---

## 2. Tool-by-Tool: Real Examples

### 2.1 `codegraph_status` — Index health

```
> codegraph_status
```

**Result:** Files indexed, total nodes, edges, DB size, breakdown by node kind
(function, struct, enum, variable, etc.).

**Use when:** You want to confirm the index is healthy before a complex query, or
you suspect a large file / third-party tree was indexed unexpectedly.

---

### 2.2 `codegraph_files` — File listing

```
> codegraph_files path=src format=flat
```

**Result:** All 32 files under `src/` — `download_manager.c`, `http_module.c`,
`lua_engine.c`, `gui.c`, `thread_queue.c`, `zip_module.c`, etc.

```
> codegraph_files path=plugins pattern=*.lua format=flat
```

**Result:** Only `.lua` files under `plugins/`.

```
> codegraph_files path=third_party/curl-8.19.0/lib maxDepth=2 format=tree
```

**Result:** The curl lib tree at 2 levels deep. Useful for scoping third-party
exploration.

**Use when:** You need to survey the directory structure, find files by pattern,
or understand the project layout before diving into a specific area.

---

### 2.3 `codegraph_search` — Find symbol by name

```
> codegraph_search query=download_manager limit=15
```

**Result:**

| Symbol | Kind | File | Line |
|--------|------|------|------|
| `download_manager_add` | function | `src/download_manager.c` | 1527 |
| `download_manager_init` | function | `src/download_manager.c` | 1362 |
| `download_manager_pause` | function | `src/download_manager.c` | 1631 |
| `download_manager_resume` | function | `src/download_manager.c` | 1648 |
| `download_manager_remove` | function | `src/download_manager.c` | 1685 |
| `download_manager_shutdown` | function | `src/download_manager.c` | 1489 |
| `download_manager_sync_ui` | function | `src/download_manager.c` | 1764 |
| `download_manager_get_list` | function | `src/download_manager.c` | 1732 |
| ... | | | |

```
> codegraph_search query=lua_ludo kind=function
```

**Result:** All `lua_ludo_*` functions (the Lua binding implementations).

```
> codegraph_search query=gui_log
```

**Result:** `gui_log` found at `src/gui.c:661` and `src/console_log.c:9`
(two implementations — GUI and console builds).

**Use when:** You know the symbol name but not its location.

---

### 2.4 `codegraph_node` — Symbol detail + source

```
> codegraph_node symbol=lua_ludo_new_download includeCode=true
```

**Result:** Full function signature, location, and source code:

```c
static int lua_ludo_new_download(lua_State *L) {
    const char *url          = luaL_checkstring(L, 1);
    const char *output_dir   = luaL_optstring(L, 2, NULL);
    // ... (full body)
}
```

```
> codegraph_node symbol=download_manager_add includeCode=false
```

**Result:** Location only — `src/download_manager.c:1527`. Use `includeCode=false`
(default) when you just need the file:line to navigate.

```
> codegraph_node symbol=perform_download
```

**Result:** Location only: `src/download_manager.c:738`.

**Use when:** You need the exact signature, source, or docstring of a known
symbol. Prefer this over opening the file with Read — it's faster and gives
you parsed structure.

---

### 2.5 `codegraph_callers` — Who calls this?

```
> codegraph_callers symbol=dm_log limit=15
```

**Result:** 15 callers across the codebase:

| Caller | File | Line |
|--------|------|------|
| `header_cb` | `src/download_manager.c` | 155 |
| `probe_download_head` | `src/download_manager.c` | 405 |
| `write_cb` | `src/download_manager.c` | 650 |
| `perform_download` | `src/download_manager.c` | 738 |
| `download_manager_init` | `src/download_manager.c` | 1362 |
| `download_manager_shutdown` | `src/download_manager.c` | 1489 |
| `download_manager_add` | `src/download_manager.c` | 1527 |
| `gui_log` | `src/gui.c` | 661 |
| `curl_shared_debug_cb` | `src/http_module.c` | 19 |
| `http_request` | `src/http_module.c` | 429 |
| `async_http_worker` | `src/http_module.c` | 934 |
| `http_raw_get` | `src/http_module.c` | 1110 |
| `lua_zip_create` | `src/zip_module.c` | 521 |
| `db_save_and_archive` | `src/download_manager.c` | 1093 |
| `header_ctx_set_filename` | `src/download_manager.c` | 348 |

```
> codegraph_callers symbol=lua_ludo_new_download
```

**Result:** No C callers — this is a Lua C function, called by the Lua runtime
when scripts call `ludo.newDownload()`.

```
> codegraph_callers symbol=http_request
```

**Result:** Shows all C functions that call `http_request` (useful for tracing
HTTP flow).

**Use when:** You want to understand who depends on a function, or trace the
call chain backward from an effect.

---

### 2.6 `codegraph_callees` — What does it call?

```
> codegraph_callees symbol=download_manager_add limit=30
```

**Result:** 11 callees:

| Callee | Location | Purpose |
|--------|----------|---------|
| `ludo_mutex_lock` | `src/thread_queue.c:24` | Lock queue mutex |
| `ludo_mutex_unlock` | `src/thread_queue.c:25` | Unlock queue mutex |
| `dm_log` | `src/download_manager.c:79` | Log message |
| `decode_filename_component` | `src/download_manager.c:315` | Decode URL-encoded filename |
| `filename_from_url` | `src/download_manager.c:377` | Extract filename from URL |
| `gui_dispatch_update` | `src/download_manager.c:273` | Notify UI of change |
| `probe_download_head` | `src/download_manager.c:405` | HEAD probe file size/status |
| `build_download_path` | `src/download_manager.c:395` | Build output path |
| `task_queue_push_task` | `src/thread_queue.c:122` | Push onto download queue |
| `calloc` | `zlib/zutil.c:300` | Zero-initialized alloc |
| `free` | `zlib/gzguts.h:114` | Free memory |

```
> codegraph_callees symbol=download_manager_init limit=15
```

**Result:** Everything the init function calls — `ludo_config_get`, `dm_log_init`,
`ludo_mutex_init`, `task_queue_init`, `curl_global_init`, `db_load`, `gui_dispatch_update`, etc.

**Use when:** You need to understand what a function depends on, trace its
implementation flow, or estimate the blast radius of adding a new dependency.

---

### 2.7 `codegraph_impact` — Blast radius analysis

```
> codegraph_impact symbol=ludo_mutex_lock depth=2
```

**Result:** Changing `ludo_mutex_lock` affects **57 symbols** across 8 files:

| File | Symbols affected |
|------|-----------------|
| `src/thread_queue.c` | `task_queue_push_task`, `task_queue_pop`, `task_queue_shutdown` |
| `src/download_manager.c` | `dm_log`, `perform_download`, `download_manager_init`, `download_manager_add`, `download_manager_pause`, `download_manager_resume`, `download_manager_remove`, `download_manager_sync_ui`, `db_save_and_archive`, `header_cb`, `write_cb`, `probe_download_head`, ... |
| `src/gui.c` | `gui_log`, `on_pause_clicked`, `on_resume_clicked`, `on_remove_clicked`, `url_worker_thread`, `gui_shutdown`, ... |
| `src/http_module.c` | `http_request`, `async_http_worker`, `http_raw_get`, `async_http_queue_push`, `async_http_shutdown`, ... |
| `src/zip_module.c` | `lua_zip_create` |
| `src/ludo_module.c` | `lua_ludo_pause_download`, `lua_ludo_remove_download` |
| `src/main.c` | `main` |
| `src/lua_engine.c` | `lua_engine_load_plugins`, `lua_engine_process_url`, `lua_engine_run_script` |

```
> codegraph_impact symbol=download_manager_add depth=3
```

**Result:** Everything that `download_manager_add` touches, plus everything that
touches those, 3 levels deep — good for assessing risky refactors.

**Use when:** You're planning to change a widely-used utility (mutex, log
function, config API) and need to know how many places to audit.

---

### 2.8 `codegraph_context` — Task-focused code map

```
> codegraph_context task="add a new Lua module to expose platform info"
```

**Result:** Returns entry points (`lua_engine.c`, `ludo_module.c`), patterns for
registering Lua functions, and related symbols — enough context to start coding
without reading every file.

```
> codegraph_context task="fix download resume after network loss"
```

**Result:** Returns `perform_download`, `download_manager_resume`,
`worker_thread`, `probe_download_head`, and related state-machine code.

```
> codegraph_context task="add a new plugin for a video site"
```

**Result:** Returns the plugin loader (`lua_engine_load_plugins`), the
`generic.lua` fallback, and several existing plugin `process()` functions as
reference patterns.

**Use when:** You're starting a new task and need a mental map of the relevant
code — one call instead of 5-10 manual searches.

---

### 2.9 `codegraph_explore` — Deep survey

```
> codegraph_explore query="download_manager task_queue thread_queue perform_download worker_thread"
```

**Result:** Full source code from `src/download_manager.c`, `src/thread_queue.c`,
`src/thread_queue.h`, and `src/download_manager.h` — all relevant sections in
one response. Includes a relationship map.

```
> codegraph_explore query="http_module http_request async_http lua_http_get"
```

**Result:** Full HTTP module source plus all async HTTP infrastructure.

```
> codegraph_explore query="lua_engine process_url load_plugins lua_engine_init"
```

**Result:** Full Lua engine source — plugin loading, URL processing lifecycle,
script execution.

**Use when:** You need to deeply understand an unfamiliar subsystem. This is
token-heavy but replaces many individual Read/node calls. **Budget: 2 calls max**
for a project of this size (~838 files).

---

## 3. When to Use CodeGraph vs. Grep

| Question type | Tool | Why |
|--------------|------|-----|
| "Where is X defined?" | `codegraph_search` | Sub-millisecond AST lookup |
| "What symbol is at line N?" | `codegraph_node` | Parsed structure |
| "What calls function Y?" | `codegraph_callers` | Edge traversal, not regex |
| "What does Y call?" | `codegraph_callees` | Edge traversal |
| "What would break if I changed Z?" | `codegraph_impact` | N-depth dependency fan-out |
| "Survey a new module" | `codegraph_explore` | One-call full source |
| "Find literal string 'Connection refused'" | `grep` | CodeGraph indexes symbols, not strings |
| "Find a comment mentioning TODO" | `grep` | Comments are not AST nodes |
| "Find every call to `newDownload` in Lua" | `grep` (`.lua` include) | Lua code is not C, won't be in the AST index |

**Rule of thumb:** Structural → CodeGraph. Literal text → grep. File detail → Read.

---

## 4. Index Lag

CodeGraph re-indexes files within ~500ms of save. If you edit a file and
immediately query it, the old index may be returned. Wait 1 second or re-query
if you get stale results.

---

## 5. Project-Specific Patterns

### Finding Lua binding implementations

All C functions exposed to Lua are prefixed `lua_ludo_` or `lua_http_`:

```
> codegraph_search query=lua_ludo kind=function
> codegraph_search query=lua_http kind=function
```

### Understanding the download pipeline

```
> codegraph_search query=download_manager kind=function
> codegraph_callers symbol=download_manager_add
> codegraph_callees symbol=perform_download
```

### Tracing the plugin lifecycle

```
> codegraph_search query=lua_engine kind=function
> codegraph_callers symbol=lua_engine_process_url
```

### Finding all mutex-protected code paths

```
> codegraph_impact symbol=ludo_mutex_lock depth=2
```

### Checking if a change breaks the GUI

```
> codegraph_impact symbol=gui_dispatch_update depth=2
```

---

## 6. Quick Reference

| Goal | Command |
|------|---------|
| Is the index ready? | `codegraph_status` |
| List all source files | `codegraph_files path=src` |
| Find a function | `codegraph_search query=funcname` |
| Get function source | `codegraph_node symbol=funcname includeCode=true` |
| Who calls me? | `codegraph_callers symbol=funcname` |
| What do I call? | `codegraph_callees symbol=funcname` |
| Blast radius | `codegraph_impact symbol=funcname depth=2` |
| Task context | `codegraph_context task="description"` |
| Deep survey | `codegraph_explore query="symbol1 symbol2 symbol3"` |
