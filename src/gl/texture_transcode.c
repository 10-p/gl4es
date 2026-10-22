#include "texture_transcode.h"

#include <stdlib.h>

#include "../glx/hardext.h"
#include "texture.h"

#ifndef GL_COMPRESSED_RGBA_ASTC_4x4_KHR
#define GL_COMPRESSED_RGBA_ASTC_4x4_KHR             0x93B0
#endif
#ifndef GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR
#define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR     0x93D0
#endif
#ifndef GL_COMPRESSED_RGB8_ETC2
#define GL_COMPRESSED_RGB8_ETC2                     0x9274
#define GL_COMPRESSED_SRGB8_ETC2                    0x9275
#define GL_COMPRESSED_RGBA8_ETC2_EAC                0x9278
#define GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC         0x9279
#endif

GLboolean isDXTcSRGB(GLenum format);
GLvoid *uncompressDXTc(GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, int transparent0, int* simpleAlpha, int* complexAlpha, const GLvoid *data);

#ifdef GL4ES_TRANSCODE
// texture_encoders.cpp: RGBA (red in the low byte) of whole 4x4 blocks to ASTC 4x4 or ETC2; 0 on success.
int gl4es_encode_astc4x4(const unsigned char *rgba, int width, int height, unsigned char *out, size_t size);
// Nonzero when the ASTC encoder's context could be made (made once, kept for the process).
int gl4es_astc_encoder_ready(void);
int gl4es_encode_etc2(const unsigned char *rgba, int width, int height, int alpha, unsigned char *out);
#endif

static int dxt_has_alpha(GLenum dxtformat) {
    return dxtformat!=GL_COMPRESSED_RGB_S3TC_DXT1_EXT && dxtformat!=GL_COMPRESSED_SRGB_S3TC_DXT1_EXT;
}

GLenum dxt_upload_format(GLenum dxtformat) {
    const int srgb = isDXTcSRGB(dxtformat);
    if(hardext.s3tc && (!srgb || hardext.s3tcsrgb))
        return dxtformat;
#ifdef GL4ES_TRANSCODE
    if(hardext.astc && gl4es_astc_encoder_ready())
        return srgb?GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR:GL_COMPRESSED_RGBA_ASTC_4x4_KHR;
    // DXT1 with alpha has one bit of it; etcpak has no punch-through encoder, so it goes to ETC2 with EAC alpha.
    if(hardext.etc2)
        return dxt_has_alpha(dxtformat)
            ? (srgb?GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:GL_COMPRESSED_RGBA8_ETC2_EAC)
            : (srgb?GL_COMPRESSED_SRGB8_ETC2:GL_COMPRESSED_RGB8_ETC2);
#endif
    return 0;
}

GLsizei dxt_upload_size(GLenum format, GLsizei width, GLsizei height) {
    const GLsizei blocks = ((width+3)/4) * ((height+3)/4);
    switch(format) {
        case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
        case GL_COMPRESSED_SRGB_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGB8_ETC2:
        case GL_COMPRESSED_SRGB8_ETC2:
            return blocks*8;
        default:
            return blocks*16;
    }
}

GLvoid *dxt_transcode(GLenum dxtformat, GLenum format, GLsizei width, GLsizei height, const GLvoid *data) {
#ifdef GL4ES_TRANSCODE
    const GLsizei pw = (width+3)&~3;
    const GLsizei ph = (height+3)&~3;
    int simpleAlpha = 0, complexAlpha = 0;
    // A DXT1 texel of index 3 in three-colour mode is transparent only in the formats with alpha.
    const int transparent0 = (dxtformat==GL_COMPRESSED_RGBA_S3TC_DXT1_EXT || dxtformat==GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT);
    unsigned char *rgba = (unsigned char*)uncompressDXTc(pw, ph, dxtformat, -1, transparent0, &simpleAlpha, &complexAlpha, data);
    const GLsizei size = dxt_upload_size(format, width, height);
    unsigned char *out = (unsigned char*)malloc(size);
    int error = (format==GL_COMPRESSED_RGBA_ASTC_4x4_KHR || format==GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR)
        ? gl4es_encode_astc4x4(rgba, pw, ph, out, size)
        : gl4es_encode_etc2(rgba, pw, ph, dxt_has_alpha(dxtformat), out);
    free(rgba);
    if(error) {
        free(out);
        return NULL;
    }
    return out;
#else
    return NULL;
#endif
}

// The public interface (include/gl4estranscode.h).
unsigned int gl4es_dxt_upload_format(unsigned int dxtformat) {
    return dxt_upload_format(dxtformat);
}

int gl4es_dxt_upload_size(unsigned int format, int width, int height) {
    return dxt_upload_size(format, width, height);
}

void *gl4es_dxt_transcode(unsigned int dxtformat, unsigned int format, int width, int height, const void *data) {
    return dxt_transcode(dxtformat, format, width, height, data);
}
