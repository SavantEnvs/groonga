/* Shared setup for the Mayhem harnesses: one process-wide grn_init(), a silent logger, and a
   file-backed database in a private temporary directory. A path-less (in-memory) database cannot
   be used: `table_create` there fails and its error path dereferences a NULL grn_ja. */
#ifndef MAYHEM_COMMON_H
#define MAYHEM_COMMON_H

#define _GNU_SOURCE
#include <groonga.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void
mayhem_log(grn_ctx *ctx, grn_log_level level, const char *timestamp,
           const char *title, const char *message, const char *location,
           void *user_data)
{
  (void)ctx; (void)level; (void)timestamp; (void)title;
  (void)message; (void)location; (void)user_data;
}

static void
mayhem_output(grn_ctx *ctx, int flags, void *user_data)
{
  char *chunk;
  unsigned int chunk_size;
  int recv_flags;
  (void)flags; (void)user_data;
  grn_ctx_recv(ctx, &chunk, &chunk_size, &recv_flags);
}

static void
mayhem_init(void)
{
  static const grn_logger logger = {
    GRN_LOG_NOTICE, GRN_LOG_TIME | GRN_LOG_MESSAGE, NULL, mayhem_log, NULL, NULL
  };
  grn_init();
  grn_logger_set(NULL, &logger);
}

static char mayhem_dir[256];

static int
mayhem_remove_entry(const char *path, const struct stat *st, int type,
                    struct FTW *ftw)
{
  (void)st; (void)type; (void)ftw;
  return remove(path);
}

static void
mayhem_remove_dir(void)
{
  if (mayhem_dir[0]) {
    nftw(mayhem_dir, mayhem_remove_entry, 16, FTW_DEPTH | FTW_PHYS);
    mayhem_dir[0] = '\0';
  }
}

/* The database is mmap()ed from files in the scratch directory, and Groonga writes through the
   mapping without checking that the file system can back it: on a full or tiny scratch directory the
   first write faults with SIGBUS (grn_io_create, io.c:388), which is the environment's fault and not
   a library bug. A database needs about 1 MiB at start-up and a few MiB after minutes of fuzzing. */
#define MAYHEM_SCRATCH_MIN_FREE ((uint64_t)32 << 20)
#define MAYHEM_SCRATCH_PROBE ((off_t)16 << 20)

static int mayhem_scratch_short;

static int
mayhem_scratch_ok(const char *dir)
{
  static int probed;
  struct statvfs sv;
  if (statvfs(dir, &sv) != 0 ||
      (uint64_t)sv.f_bavail * sv.f_frsize < MAYHEM_SCRATCH_MIN_FREE) {
    return 0;
  }
  if (!probed) {
    char probe[300];
    int fd, rc;
    snprintf(probe, sizeof(probe), "%s/probe", dir);
    fd = open(probe, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
      return 0;
    }
    rc = posix_fallocate(fd, 0, MAYHEM_SCRATCH_PROBE);
    close(fd);
    unlink(probe);
    if (rc != 0) {
      return 0;
    }
    probed = 1;
  }
  return 1;
}

/* Opens ctx and a new file-backed database in a fresh temporary directory. NULL on failure, with
   mayhem_scratch_short set when the scratch directory has no room for it. */
static grn_obj *
mayhem_open(grn_ctx *ctx)
{
  grn_obj *db;
  char path[300];
  const char *base = getenv("TMPDIR");
  if (!base || !base[0]) {
    base = "/tmp";
  }
  if (grn_ctx_init(ctx, 0) != GRN_SUCCESS) {
    return NULL;
  }
  snprintf(mayhem_dir, sizeof(mayhem_dir), "%s/groonga-fuzz-XXXXXX", base);
  if (!mkdtemp(mayhem_dir)) {
    mayhem_dir[0] = '\0';
    mayhem_scratch_short = 1;
    grn_ctx_fin(ctx);
    return NULL;
  }
  if (!mayhem_scratch_ok(mayhem_dir)) {
    mayhem_scratch_short = 1;
    remove(mayhem_dir);
    mayhem_dir[0] = '\0';
    grn_ctx_fin(ctx);
    return NULL;
  }
  snprintf(path, sizeof(path), "%s/db", mayhem_dir);
  atexit(mayhem_remove_dir);
  db = grn_db_create(ctx, path, NULL);
  if (!db) {
    mayhem_remove_dir();
    grn_ctx_fin(ctx);
    return NULL;
  }
  grn_ctx_recv_handler_set(ctx, mayhem_output, NULL);
  return db;
}

/* For LLVMFuzzerInitialize when mayhem_open() failed: a worker without scratch space exits cleanly
   instead of crashing, so it is not reported as a defect of the library. */
static void
mayhem_open_failed(void)
{
  if (mayhem_scratch_short) {
    fprintf(stderr, "groonga fuzz harness: no usable scratch space in %s (needs %d MiB free)\n",
            getenv("TMPDIR") && getenv("TMPDIR")[0] ? getenv("TMPDIR") : "/tmp",
            (int)(MAYHEM_SCRATCH_MIN_FREE >> 20));
    exit(0);
  }
  abort();
}

static void
mayhem_close(grn_ctx *ctx, grn_obj *db)
{
  grn_obj_close(ctx, db);
  grn_ctx_fin(ctx);
  mayhem_remove_dir();
}

/* Runs one command line and returns whether the context is still usable. */
static int
mayhem_send(grn_ctx *ctx, const char *line, size_t size)
{
  grn_ctx_send(ctx, line, (unsigned int)size, 0);
  return ctx->stat != GRN_CTX_QUIT;
}

#endif
