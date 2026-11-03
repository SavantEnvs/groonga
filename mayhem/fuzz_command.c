/* Feeds the input to the Groonga command interpreter exactly as `groonga -n` does for a script on
   stdin: one grn_ctx_send() per line, against a fresh file-backed database in a private scratch directory
   that is deleted after every input. */
#include "common.h"
#include <ctype.h>

/* table_create and column_create take a `path` argument that Groonga uses as a file name as given,
   relative to the working directory or absolute: it would leave files behind that outlive the scratch
   directory, so a later input would see them, and could write anywhere. Inputs that name it are
   rejected. `path` is the 8th argument of table_create and the 6th of column_create, so a
   positional one is caught by counting the positional arguments (`--name value` pairs excluded);
   the named forms are `--path` and `path=` (URL style). A table or column that is merely called
   `path` is fine. */
static int
names_path(const char *line, size_t size)
{
  const char *p = line;
  const char *end = line + size;
  size_t limit, positional = 0;
  int after_option = 0;

  if (memmem(line, size, "table_create", 12)) {
    limit = 8;
  } else if (memmem(line, size, "column_create", 13)) {
    limit = 6;
  } else {
    return 0;
  }
  if (memmem(line, size, "--path", 6) || memmem(line, size, "path=", 5)) {
    return 1;
  }
  while (p < end) {
    const char *token;
    while (p < end && isspace((unsigned char)*p)) {
      p++;
    }
    token = p;
    while (p < end && !isspace((unsigned char)*p)) {
      p++;
    }
    if (p == token) {
      break;
    }
    if (p - token >= 2 && token[0] == '-' && token[1] == '-') {
      after_option = 1;
    } else if (after_option) {
      after_option = 0;
    } else {
      positional++;
    }
  }
  return positional > limit;
}

/* Commands change process-wide state that outlives the database: `log_level` the logger's level,
   `query_log_flags_set` the query-log flags, `thread_limit` the thread limit, `cache_limit` the
   query cache's size, and the query cache itself keeps `select` results from earlier inputs. So
   that an input behaves the same whatever ran before it, the logger level, query-log flags and
   thread limit are put back after every input, and each input gets a private query cache. */
static grn_log_level initial_log_level;
static unsigned int initial_query_log_flags;
static uint32_t initial_thread_limit;

int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
  grn_ctx ctx;
  (void)argc; (void)argv;
  mayhem_init();
  if (grn_ctx_init(&ctx, 0) == GRN_SUCCESS) {
    initial_log_level = grn_logger_get_max_level(&ctx);
    initial_query_log_flags = grn_query_logger_get_flags(&ctx);
    grn_ctx_fin(&ctx);
  }
  initial_thread_limit = grn_thread_get_limit();
  return 0;
}

static void
restore_global_state(grn_ctx *ctx)
{
  grn_logger_set_max_level(ctx, initial_log_level);
  grn_query_logger_set_flags(ctx, initial_query_log_flags);
  grn_thread_set_limit(initial_thread_limit);
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  grn_ctx ctx;
  grn_obj *db;
  grn_cache *cache, *default_cache;
  const char *p = (const char *)data;
  const char *end = p + size;

  while (p < end) {
    const char *nl = memchr(p, '\n', (size_t)(end - p));
    const char *line_end = nl ? nl : end;
    if (names_path(p, (size_t)(line_end - p))) {
      return -1;
    }
    p = nl ? nl + 1 : end;
  }
  p = (const char *)data;

  db = mayhem_open(&ctx);
  if (!db) {
    return 0;
  }
  default_cache = grn_cache_current_get(&ctx);
  cache = grn_cache_open(&ctx);
  if (cache) {
    grn_cache_current_set(&ctx, cache);
  }
  while (p < end) {
    const char *nl = memchr(p, '\n', (size_t)(end - p));
    const char *line_end = nl ? nl : end;
    if (!mayhem_send(&ctx, p, (size_t)(line_end - p))) {
      break;
    }
    p = nl ? nl + 1 : end;
  }
  if (cache) {
    grn_cache_current_set(&ctx, default_cache);
    grn_cache_close(&ctx, cache);
  }
  restore_global_state(&ctx);
  mayhem_close(&ctx, db);
  return 0;
}
