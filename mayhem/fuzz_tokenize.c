/* Tokenizes and normalizes the input with every built-in tokenizer/normalizer combination.
   Byte 0 picks the lexicon, byte 1 the tokenize mode and string flags, the rest is the text. */
#include "common.h"

#include <stdio.h>
#include <stdlib.h>

static grn_ctx g_ctx;
static grn_obj *g_db;

#define MAX_LEXICONS 64
static grn_obj *g_lexicons[MAX_LEXICONS];
static size_t g_n_lexicons;

static const char *const TOKENIZERS[] = {
  "TokenDelimit",  "TokenUnigram",   "TokenBigram",       "TokenBigramSplitSymbol",
  "TokenBigramSplitSymbolAlpha", "TokenBigramSplitSymbolAlphaDigit",
  "TokenBigramIgnoreBlank", "TokenBigramIgnoreBlankSplitSymbol",
  "TokenTrigram",  "TokenDelimitNull", "TokenRegexp",     "TokenNgram",
  "TokenPattern",
};
static const char *const NORMALIZERS[] = {
  "NormalizerAuto", "NormalizerNFKC130", "NormalizerNFKC121", "NormalizerNFKC100", "",
};

int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
  size_t t, n;
  (void)argc; (void)argv;
  mayhem_init();
  g_db = mayhem_open(&g_ctx);
  if (!g_db) {
    mayhem_open_failed();
  }
  for (t = 0; t < sizeof(TOKENIZERS) / sizeof(TOKENIZERS[0]); t++) {
    for (n = 0; n < sizeof(NORMALIZERS) / sizeof(NORMALIZERS[0]); n++) {
      char cmd[256], name[64];
      grn_obj *lexicon;
      snprintf(name, sizeof(name), "Lexicon%zu_%zu", t, n);
      snprintf(cmd, sizeof(cmd),
               "table_create %s TABLE_PAT_KEY ShortText --default_tokenizer %s%s%s",
               name, TOKENIZERS[t], NORMALIZERS[n][0] ? " --normalizer " : "",
               NORMALIZERS[n]);
      mayhem_send(&g_ctx, cmd, strlen(cmd));
      g_ctx.rc = GRN_SUCCESS;
      lexicon = grn_ctx_get(&g_ctx, name, -1);
      if (lexicon && g_n_lexicons < MAX_LEXICONS) {
        g_lexicons[g_n_lexicons++] = lexicon;
      }
    }
  }
  if (g_n_lexicons == 0) {
    abort();
  }
  return 0;
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  grn_obj *lexicon;
  grn_tokenize_mode mode;
  uint32_t token_flags = 0;
  int string_flags = 0;
  grn_token_cursor *cursor;
  grn_obj *normalized;

  if (size < 2) {
    return -1;
  }
  lexicon = g_lexicons[data[0] % g_n_lexicons];
  mode = (data[1] & 0x1) ? GRN_TOKEN_ADD : GRN_TOKEN_GET;
  if (data[1] & 0x2) {
    token_flags |= GRN_TOKEN_CURSOR_ENABLE_TOKENIZED_DELIMITER;
  }
  if (data[1] & 0x4) {
    string_flags |= GRN_STRING_REMOVE_BLANK;
  }
  if (data[1] & 0x8) {
    string_flags |= GRN_STRING_WITH_TYPES | GRN_STRING_WITH_CHECKS;
  }
  if (data[1] & 0x10) {
    string_flags |= GRN_STRING_REMOVE_TOKENIZED_DELIMITER;
  }
  data += 2;
  size -= 2;

  normalized = grn_string_open(&g_ctx, (const char *)data, (unsigned int)size,
                               lexicon, string_flags);
  if (normalized) {
    grn_obj_close(&g_ctx, normalized);
  }

  cursor = grn_token_cursor_open(&g_ctx, lexicon, (const char *)data, size,
                                 mode, token_flags);
  if (cursor) {
    while (grn_token_cursor_next(&g_ctx, cursor) != GRN_ID_NIL ||
           grn_token_cursor_get_status(&g_ctx, cursor) == GRN_TOKEN_CURSOR_DOING) {
    }
    grn_token_cursor_close(&g_ctx, cursor);
  }
  if (mode == GRN_TOKEN_ADD) {
    grn_table_truncate(&g_ctx, lexicon);
  }
  g_ctx.rc = GRN_SUCCESS;
  return 0;
}
