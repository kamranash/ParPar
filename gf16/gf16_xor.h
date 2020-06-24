
#include "../src/hedley.h"

#define FUNCS(v) \
	void* gf16_xor_jit_init_##v(int polynomial); \
	void* gf16_xor_jit_init_mut_##v(); \
	void gf16_xor_prepare_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen); \
	void gf16_xor_finish_##v(void *HEDLEY_RESTRICT dst, size_t len); \
	void gf16_xor_jit_mul_##v(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch); \
	void gf16_xor_jit_muladd_##v(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch); \
	extern int gf16_xor_available_##v

FUNCS(sse2);
FUNCS(avx2);
FUNCS(avx512);

#undef FUNCS

unsigned gf16_xor_jit_muladd_multi_avx512(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch);

void gf16_xor_jit_uninit(void* scratch);
void gf16_xor_jit_uninit_avx512(void* scratch);

// non-JIT version
void* gf16_xor_init_sse2(int polynomial);
void gf16_xor_mul_sse2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
void gf16_xor_muladd_sse2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);

