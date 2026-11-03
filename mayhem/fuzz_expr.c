/* Parses the input as a Groonga expression (query syntax, script syntax or the --match_columns /
   --scorer style variants) against a populated table with text, number, vector, reference, geo
   and index columns, then executes it with grn_table_select(). The database is built once; parsing
   and selecting (without the update flag) never mutate it. */
#include "common.h"

#include <stdlib.h>

static grn_ctx g_ctx;
static grn_obj *g_db;
static grn_obj *g_table;
static grn_obj *g_columns[3];

static const char *const SETUP[] = {
  "table_create Tags TABLE_PAT_KEY ShortText",
  "table_create Memos TABLE_HASH_KEY ShortText",
  "column_create Memos title COLUMN_SCALAR ShortText",
  "column_create Memos content COLUMN_SCALAR Text",
  "column_create Memos score COLUMN_SCALAR Int32",
  "column_create Memos rate COLUMN_SCALAR Float",
  "column_create Memos created_at COLUMN_SCALAR Time",
  "column_create Memos tags COLUMN_VECTOR Tags",
  "column_create Memos weights COLUMN_VECTOR Int32",
  "column_create Memos location COLUMN_SCALAR WGS84GeoPoint",
  "table_create Terms TABLE_PAT_KEY ShortText --default_tokenizer TokenBigram "
  "--normalizer NormalizerAuto",
  "column_create Terms memos_title COLUMN_INDEX|WITH_POSITION Memos title",
  "column_create Terms memos_content COLUMN_INDEX|WITH_POSITION Memos content",
  "column_create Tags memos_tags COLUMN_INDEX Memos tags",
  "load --table Memos",
  "[",
  "{\"_key\":\"a\",\"title\":\"Groonga fulltext search\",\"content\":\"Groonga is an embeddable "
  "fulltext search engine\",\"score\":5,\"rate\":0.5,\"created_at\":\"2020-01-01 00:00:00\","
  "\"tags\":[\"db\",\"search\"],\"weights\":[1,2,3],\"location\":\"35.6,139.7\"},",
  "{\"_key\":\"b\",\"title\":\"Mroonga storage engine\",\"content\":\"A MySQL storage engine "
  "using Groonga\",\"score\":3,\"rate\":1.5,\"created_at\":\"2021-06-15 12:30:00\","
  "\"tags\":[\"mysql\"],\"weights\":[4],\"location\":\"34.7,135.5\"},",
  "{\"_key\":\"c\",\"title\":\"\",\"content\":\"\",\"score\":-1,\"rate\":0,\"created_at\":0,"
  "\"tags\":[],\"weights\":[],\"location\":\"0,0\"}",
  "]",
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
  g_table = grn_ctx_get(&g_ctx, "Memos", -1);
  g_columns[0] = grn_obj_column(&g_ctx, g_table, "title", 5);
  g_columns[1] = grn_obj_column(&g_ctx, g_table, "content", 7);
  g_columns[2] = NULL;
  if (!g_table || !g_columns[0] || !g_columns[1]) {
    abort();
  }
  return 0;
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  grn_obj *expr, *var, *result;
  char *text;
  grn_expr_flags flags = GRN_EXPR_SYNTAX_QUERY;
  grn_operator default_op = GRN_OP_AND;
  grn_obj *default_column = NULL;
  uint8_t mode;

  if (size < 1) {
    return -1;
  }
  mode = data[0];
  data++;
  size--;

  switch (mode & 0x3) {
  case 0:
    flags = GRN_EXPR_SYNTAX_QUERY;
    break;
  case 1:
    flags = GRN_EXPR_SYNTAX_QUERY | GRN_EXPR_ALLOW_PRAGMA |
            GRN_EXPR_ALLOW_COLUMN | GRN_EXPR_ALLOW_LEADING_NOT;
    break;
  case 2:
    flags = GRN_EXPR_SYNTAX_SCRIPT;
    break;
  default:
    flags = GRN_EXPR_SYNTAX_SCRIPT | GRN_EXPR_ALLOW_COLUMN;
    break;
  }
  if (mode & 0x4) {
    default_op = GRN_OP_OR;
  }
  if (mode & 0x8) {
    default_column = g_columns[0];
  }

  /* The expression parser reads one byte past its length (lookahead for operators and the
     3-byte ideographic space), as every real caller passes a NUL-terminated string. */
  text = (char *)malloc(size + 1);
  if (!text) {
    abort();
  }
  memcpy(text, data, size);
  text[size] = '\0';

  GRN_EXPR_CREATE_FOR_QUERY(&g_ctx, g_table, expr, var);
  if (!expr) {
    free(text);
    return 0;
  }
  if (grn_expr_parse(&g_ctx, expr, text, (unsigned int)size,
                     default_column, GRN_OP_MATCH, default_op, flags) ==
      GRN_SUCCESS) {
    result = grn_table_select(&g_ctx, g_table, expr, NULL, GRN_OP_OR);
    if (result) {
      grn_obj_close(&g_ctx, result);
    }
  }
  g_ctx.rc = GRN_SUCCESS;
  grn_obj_close(&g_ctx, expr);
  free(text);
  return 0;
}
