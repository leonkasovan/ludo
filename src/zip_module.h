#ifndef LUDO_ZIP_MODULE_H
#define LUDO_ZIP_MODULE_H

#include <lua.h>

/*
 * Registers the "zip" library into the given Lua state.
 * After this call Lua scripts can call:
 *
 *   status [, errmsg] = zip.create(output_path, {file1, file2, ...})
 *   status [, errmsg] = zip.create(output_path, directory [, glob_filter])
 *
 * Parameters:
 *   output_path  (string) — path to the ZIP file to create / overwrite.
 *   {files}      (table)  — array of file paths to pack; each entry is stored
 *                           using only its basename (no directory component).
 *   directory    (string) — root directory to pack recursively.
 *   glob_filter  (string, optional) — case-insensitive glob pattern applied to
 *                           file basenames (supports * and ? wildcards).
 *                           If omitted or empty every file is included.
 *
 * Return values:
 *   status  (integer) — 0 on success, -1 on failure.
 *   errmsg  (string)  — present only on failure; human-readable error message.
 *
 * Files are compressed with raw DEFLATE (zlib 1.2.8).  The resulting archive
 * is compatible with standard ZIP readers (PKZIP 2.0 / Info-ZIP).
 */
void zip_module_register(lua_State *L);

#endif /* LUDO_ZIP_MODULE_H */
