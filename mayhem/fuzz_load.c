/* Loads the input as the JSON body of `load` into tables with every scalar/vector/reference
   column type plus an index, via the public grn_load() API. Tables are truncated after every
   input so state never carries over. */
#include "common.h"

#include <stdlib.h>

static grn_ctx g_ctx;
static grn_obj *g_db;
static const char *const TABLES[] = {"Items", "Tags", "Terms"};

static const char *const SETUP[] = {
  "table_create Tags TABLE_PAT_KEY ShortText",
  "table_create Terms TABLE_PAT_KEY ShortText --default_tokenizer TokenBigram "
  "--normalizer NormalizerAuto",
  "table_create Items TABLE_HASH_KEY ShortText",
  "column_create Items bool_value COLUMN_SCALAR Bool",
  "column_create Items int8_value COLUMN_SCALAR Int8",
  "column_create Items int32_value COLUMN_SCALAR Int32",
  "column_create Items uint64_value COLUMN_SCALAR UInt64",
  "column_create Items float_value COLUMN_SCALAR Float",
  "column_create Items time_value COLUMN_SCALAR Time",
  "column_create Items text_value COLUMN_SCALAR Text",
  "column_create Items long_text_value COLUMN_SCALAR LongText",
  "column_create Items geo_value COLUMN_SCALAR WGS84GeoPoint",
  "column_create Items tokyo_geo_value COLUMN_SCALAR TokyoGeoPoint",
  "column_create Items ref_value COLUMN_SCALAR Tags",
  "column_create Items tags COLUMN_VECTOR Tags",
  "column_create Items int_vector COLUMN_VECTOR Int32",
  "column_create Items float_vector COLUMN_VECTOR Float32",
  "column_create Items text_vector COLUMN_VECTOR ShortText",
  "column_create Items weighted_tags COLUMN_VECTOR|WITH_WEIGHT Tags",
  "column_create Terms items_text COLUMN_INDEX|WITH_POSITION Items text_value",
  "column_create Tags items_tags COLUMN_INDEX Items tags",
};

int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
  size_t i;
  (void)argc; (void)argv;
  mayhem_init();
  g_db = mayhem_open(&g_ctx);
  if (!g_db) {
    mayhem_open_failed();
  }
  for (i = 0; i < sizeof(SETUP) / sizeof(SETUP[0]); i++) {
    mayhem_send(&g_ctx, SETUP[i], strlen(SETUP[i]));
  }
  if (!grn_ctx_get(&g_ctx, "Items", -1)) {
    abort();
  }
  return 0;
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  size_t i;
  grn_rc rc;

  rc = grn_load(&g_ctx, GRN_CONTENT_JSON, "Items", 5, NULL, 0,
                (const char *)data, (unsigned int)size, NULL, 0, NULL, 0);
  g_ctx.rc = GRN_SUCCESS;
  for (i = 0; i < sizeof(TABLES) / sizeof(TABLES[0]); i++) {
    grn_obj *table = grn_ctx_get(&g_ctx, TABLES[i], -1);
    if (table) {
      grn_table_truncate(&g_ctx, table);
    }
  }
  g_ctx.rc = GRN_SUCCESS;
  return rc == GRN_SUCCESS ? 0 : -1;
}
