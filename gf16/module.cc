#ifdef _OPENMP
#include <omp.h>
#endif
#include "../src/stdint.h"
#include <string.h>
#include <stdlib.h>
#include "gf16mul.h"

#define CACHELINE_SIZE 64

// these lookup tables consume a hefty 192KB... oh well
static uint16_t input_lookup[32768]; // logarithms of input constants
static uint16_t gf_exp[65536]; // pre-calculated exponents in GF(2^16)
void ppgf_init_constants() {
	int exp = 0, n = 1;
	for (int i = 0; i < 32768; i++) {
		do {
			gf_exp[exp] = n;
			exp++; // exp will reach 65534 by the end of the loop
			n <<= 1;
			if(n > 65535) n ^= 0x1100B;
		} while( !(exp%3) || !(exp%5) || !(exp%17) || !(exp%257) );
		input_lookup[i] = exp;
	}
	gf_exp[exp] = n;
	gf_exp[65535] = gf_exp[0];
}

static inline uint16_t calc_factor(uint_fast16_t inputBlock, uint_fast16_t recoveryBlock) {
	// calculate POW(inputBlockConstant, recoveryBlock) in GF
	uint_fast32_t result = input_lookup[inputBlock] * recoveryBlock;
	// clever bit hack for 'result %= 65535' from MultiPar sources
	result = (result >> 16) + (result & 65535);
	result = (result >> 16) + (result & 65535);
	
	return gf_exp[result];
}

static Galois16Mul* gf = NULL;
static std::vector<void*> gfScratch;

static int maxNumThreads = 1, defaultNumThreads = 1;

static void setup_gf(Galois16Methods method = GF16_AUTO, size_t size_hint = 0) {
	if(!gfScratch.empty()) {
		for(unsigned i=0; i<gfScratch.size(); i++)
			if(gfScratch[i])
				gf->mutScratch_free(gfScratch[i]);
		gfScratch.clear();
	}
	delete gf;
	gf = new Galois16Mul(method);
	
	gfScratch.reserve(maxNumThreads);
	for(int i=0; i<maxNumThreads; i++)
		gfScratch.push_back(gf->mutScratch_alloc());
}
void ppgf_maybe_setup_gf() {
	if(!gf) setup_gf();
}


#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CEIL_DIV(a, b) (((a) + (b)-1) / (b))
#define ROUND_DIV(a, b) (((a) + ((b)>>1)) / (b))

#if defined(__cplusplus) && __cplusplus >= 201100 && !(defined(_MSC_VER) && defined(__clang__)) && !defined(__APPLE__)
	// C++11 method
	// len needs to be a multiple of alignment, although it sometimes works if it isn't...
	#include <cstdlib>
	#define ALIGN_ALLOC(buf, len, align) *(void**)&(buf) = aligned_alloc(align, ((len) + (align)-1) & ~((align)-1))
	#define ALIGN_FREE free
#elif defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
	#define ALIGN_ALLOC(buf, len, align) *(void**)&(buf) = _aligned_malloc((len), align)
	#define ALIGN_FREE _aligned_free
#else
	#include <stdlib.h>
	#define ALIGN_ALLOC(buf, len, align) if(posix_memalign((void**)&(buf), align, (len))) (buf) = NULL
	#define ALIGN_FREE free
#endif

// performs multiple multiplies for a region, using threads
// note that inputs will get trashed
/* REQUIRES:
   - input and each pointer in outputs must be aligned
   - len must be a multiple of stride
   - input and length of each output is the same and == len
   - number of outputs and scales is same and == numOutputs
*/
void ppgf_multiply_mat(const void** inputs, uint_fast16_t* iNums, unsigned int numInputs, size_t len, void** outputs, uint_fast16_t* oNums, unsigned int numOutputs, int add) {
	
	/*
	if(gf->needPrepare()) {
		int out;
		#pragma omp parallel for
		for(out = 0; out < (int)numOutputs; out++) {
			unsigned int in = 0;
			for(; in < numInputs-3; in+=4) {
				gf_val_32_t inNum4[4] = {
					calc_factor(iNums[in], oNums[out]),
					calc_factor(iNums[in+1], oNums[out]),
					calc_factor(iNums[in+2], oNums[out]),
					calc_factor(iNums[in+3], oNums[out])
				};
				gf.multiply_regionX.w16(&gf, inputs + in, outputs[out], inNum4, (int)len, add || in>0);
			}
			for(; in < numInputs; in++)
				gf.multiply_region.w32(&gf, inputs[in], outputs[out], calc_factor(iNums[in], oNums[out]), (int)len, true);
		}
		return;
	}
	*/
	
	unsigned int factStride = sizeof(uint16_t) * numInputs;
	factStride = (factStride + CACHELINE_SIZE-1) & ~(CACHELINE_SIZE-1);
	uint16_t* factors;
	ALIGN_ALLOC(factors, factStride * maxNumThreads, CACHELINE_SIZE);
	
	// break the slice into smaller chunks so that we maximise CPU cache usage
	int numChunks = ROUND_DIV(len, gf->info().idealChunkSize);
	if(numChunks < 1) numChunks = 1;
	unsigned int alignMask = gf->info().stride-1;
	unsigned int chunkSize = (CEIL_DIV(len, numChunks) + alignMask) & ~alignMask; // we'll assume that input chunks are memory aligned here
	
	// avoid nested loop issues by combining chunk & output loop into one
	// the loop goes through outputs before chunks
	int loop = 0;
	#pragma omp parallel for num_threads(maxNumThreads)
	for(loop = 0; loop < (int)(numOutputs * numChunks); loop++) {
		size_t offset = (loop / numOutputs) * chunkSize;
		unsigned int out = loop % numOutputs;
		int procSize = MIN(len-offset, chunkSize);
#ifdef _OPENMP
		int threadNum = omp_get_thread_num();
#else
		const int threadNum = 0;
#endif
		uint16_t* vals = (uint16_t*)((uint8_t*)factors + factStride * threadNum);
		unsigned int i;
		for(i=0; i<numInputs; i++)
			vals[i] = calc_factor(iNums[i], oNums[out]);

		if(!add) memset(((uint8_t*)outputs[out])+offset, 0, procSize);
		gf->mul_add_multi(numInputs, offset, outputs[out], inputs, procSize, vals, gfScratch[threadNum]);
	}
	
	ALIGN_FREE(factors);
}


void ppgf_prep_input(size_t destLen, size_t inputLen, char* dest, char* src) {
	ppgf_maybe_setup_gf();
	if(inputLen < destLen) {
		// need to zero out empty space at end (for final block)
		memset(dest + inputLen, 0, destLen - inputLen);
	}
	gf->prepare(dest, src, inputLen);
}
void ppgf_finish_input(unsigned int numInputs, uint16_t** inputs, size_t len) {
	ppgf_maybe_setup_gf();
	if(gf->needPrepare()) {
		// TODO: multi-thread this?
		for(int in = 0; in < (int)numInputs; in++)
			gf->finish(inputs[in], len);
	}
}

void ppgf_get_method(int* rMethod, const char** rMethLong, int* align, int* stride) {
	ppgf_maybe_setup_gf();
	const Galois16MethodInfo& info = gf->info();
	*rMethod = info.id;
	*rMethLong = info.name;
	*align = info.alignment;
	*stride = info.stride;
}

int ppgf_get_num_threads() {
#ifdef _OPENMP
	return maxNumThreads;
#else
	return 1;
#endif
}
void ppgf_set_num_threads(int threads) {
#ifdef _OPENMP
	maxNumThreads = threads;
	if(maxNumThreads < 1) maxNumThreads = defaultNumThreads;
	if(gf && (unsigned)maxNumThreads > gfScratch.size()) {
		gfScratch.reserve(maxNumThreads);
		for(unsigned i=gfScratch.size(); i<(unsigned)maxNumThreads; i++)
			gfScratch.push_back(gf->mutScratch_alloc());
	}
#endif
}
void ppgf_init_gf_module() {
#ifdef _OPENMP
	maxNumThreads = omp_get_num_procs();
	if(maxNumThreads < 1) maxNumThreads = 1;
	defaultNumThreads = maxNumThreads;
#endif
}

int ppgf_set_method(int meth, int size_hint) {
	setup_gf((Galois16Methods)meth, size_hint);
	return 0;
}
