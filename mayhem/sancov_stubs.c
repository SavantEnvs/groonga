/* No-op SanitizerCoverage callbacks for the -standalone reproducers. The instrumented library calls
   them, and with an empty SANITIZER_FLAGS (no ASan runtime to supply them) the link would fail. The
   standalone binaries do not collect coverage, so weak do-nothing definitions are enough. */
#include <stdint.h>

#define STUB(name, ...) __attribute__((weak)) void name(__VA_ARGS__) {}

__attribute__((weak)) __thread uintptr_t __sancov_lowest_stack;

STUB(__sanitizer_cov_8bit_counters_init, char *start, char *stop)
STUB(__sanitizer_cov_pcs_init, const uintptr_t *start, const uintptr_t *stop)
STUB(__sanitizer_cov_trace_pc_indir, uintptr_t callee)
STUB(__sanitizer_cov_trace_cmp1, uint8_t a, uint8_t b)
STUB(__sanitizer_cov_trace_cmp2, uint16_t a, uint16_t b)
STUB(__sanitizer_cov_trace_cmp4, uint32_t a, uint32_t b)
STUB(__sanitizer_cov_trace_cmp8, uint64_t a, uint64_t b)
STUB(__sanitizer_cov_trace_const_cmp1, uint8_t a, uint8_t b)
STUB(__sanitizer_cov_trace_const_cmp2, uint16_t a, uint16_t b)
STUB(__sanitizer_cov_trace_const_cmp4, uint32_t a, uint32_t b)
STUB(__sanitizer_cov_trace_const_cmp8, uint64_t a, uint64_t b)
STUB(__sanitizer_cov_trace_switch, uint64_t val, uint64_t *cases)
STUB(__sanitizer_cov_trace_div4, uint32_t val)
STUB(__sanitizer_cov_trace_div8, uint64_t val)
STUB(__sanitizer_cov_trace_gep, uintptr_t idx)
