// The two texture encoders behind texture_transcode.c, built with GL4ES_TRANSCODE: ARM astc-encoder (Apache-2.0)
// and etcpak (Bartosz Taudul; BSD-3-Clause).
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <thread>
#include <vector>

#include "astcenc.h"
#include "ProcessRGB.hpp"

// The encoder's threads: one a core, up to 8 (one where the build has no threads).
static unsigned int astc_threads(void) {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    return 1;
#else
    unsigned int n = std::thread::hardware_concurrency();
    return n<1 ? 1 : (n>8 ? 8 : n);
#endif
}

// One context for the process, for astc_threads() threads: GL calls come from one thread, and its tables cost
// more to build than an image.
static astcenc_context *astc_context(void) {
    static astcenc_context *context = NULL;
    static int tried = 0;
    if(!tried) {
        tried = 1;
        astcenc_config config;
        if(astcenc_config_init(ASTCENC_PRF_LDR, 4, 4, 1, ASTCENC_PRE_FASTEST, 0, &config)!=ASTCENC_SUCCESS
            || astcenc_context_alloc(&config, astc_threads(), &context, NULL)!=ASTCENC_SUCCESS)
            context = NULL;
    }
    return context;
}

extern "C" int gl4es_astc_encoder_ready(void) {
    return astc_context()!=NULL;
}

extern "C" int gl4es_encode_astc4x4(const unsigned char *rgba, int width, int height, unsigned char *out, size_t size) {
    astcenc_context *context = astc_context();
    if(!context)
        return 1;
    void *slice = (void*)rgba;
    astcenc_image image = { (unsigned int)width, (unsigned int)height, 1, ASTCENC_TYPE_U8, &slice };
    const astcenc_swizzle swizzle = { ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A };
    // The context shares an image's blocks among the threads that call it; any of them can finish it alone, so
    // a small image (a low mip) is encoded on this thread only.
    const unsigned int threads = (width*height>=256*256) ? astc_threads() : 1;
    std::vector<astcenc_error> errors(threads, ASTCENC_SUCCESS);
    std::vector<std::thread> workers;
    for(unsigned int i=1; i<threads; i++)
        workers.emplace_back([&, i]() { errors[i] = astcenc_compress_image(context, &image, &swizzle, out, size, i); });
    errors[0] = astcenc_compress_image(context, &image, &swizzle, out, size, 0);
    for(std::thread &worker : workers)
        worker.join();
    astcenc_compress_reset(context);
    for(astcenc_error error : errors)
        if(error!=ASTCENC_SUCCESS)
            return 1;
    return 0;
}

extern "C" int gl4es_encode_etc2(const unsigned char *rgba, int width, int height, int alpha, unsigned char *out) {
    // etcpak's ETC encoders read blue in the low byte.
    const size_t count = (size_t)width*height;
    uint32_t *bgra = (uint32_t*)malloc(count*4);
    if(!bgra)
        return 1;
    const uint32_t *src = (const uint32_t*)rgba;
    for(size_t i=0; i<count; i++)
        bgra[i] = (src[i]&0xFF00FF00) | ((src[i]&0xFF)<<16) | ((src[i]>>16)&0xFF);
    const uint32_t blocks = (uint32_t)((width/4)*(height/4));
    if(alpha)
        CompressEtc2Rgba(bgra, (uint64_t*)out, blocks, (size_t)width, true);
    else
        CompressEtc2Rgb(bgra, (uint64_t*)out, blocks, (size_t)width, true);
    free(bgra);
    return 0;
}
