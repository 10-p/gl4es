#include "texture_transcode.h"

#include <stdlib.h>
#include <string.h>

#include "../glx/hardext.h"
#include "stb_dxt_104.h"
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

static int dxt1_has_alpha(GLenum dxtformat) {
    return dxtformat==GL_COMPRESSED_RGBA_S3TC_DXT1_EXT || dxtformat==GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
}

// The RGBA of pw x ph texels (whole 4x4 blocks) of dxtformat data.
static unsigned char *decode_blocks(GLenum dxtformat, GLsizei pw, GLsizei ph, const GLvoid *data) {
    int simpleAlpha = 0, complexAlpha = 0;
    // A DXT1 texel of index 3 in three-colour mode is transparent only in the formats with alpha.
    return (unsigned char*)uncompressDXTc(pw, ph, dxtformat, -1, dxt1_has_alpha(dxtformat), &simpleAlpha, &complexAlpha, data);
}

static unsigned short rgb565(const unsigned char *c) {
    return ((c[0]*31+127)/255)<<11 | ((c[1]*63+127)/255)<<5 | (c[2]*31+127)/255;
}

static void rgb888(unsigned short c, int *out) {
    out[0] = ((c>>11)&31)*255/31;
    out[1] = ((c>>5)&63)*255/63;
    out[2] = (c&31)*255/31;
}

// A DXT1 block with transparent texels: three-colour mode (colour0 <= colour1), index 3 transparent, the opaque
// texels on the endpoints stb_dxt chose for them.
static void dxt1_punch_through(unsigned char *out, const unsigned char *block) {
    unsigned short c0 = out[0] | out[1]<<8, c1 = out[2] | out[3]<<8;
    if(c0>c1) {
        const unsigned short t = c0;
        c0 = c1;
        c1 = t;
    }
    int p[3][3];
    rgb888(c0, p[0]);
    rgb888(c1, p[1]);
    for(int k=0; k<3; k++)
        p[2][k] = (p[0][k]+p[1][k])/2;
    unsigned int indices = 0;
    for(int i=0; i<16; i++) {
        const unsigned char *t = block+i*4;
        unsigned int index = 3;
        if(t[3]>=128) {
            int best = 1<<30;
            for(int j=0; j<3; j++) {
                const int dr = t[0]-p[j][0], dg = t[1]-p[j][1], db = t[2]-p[j][2];
                const int d = dr*dr+dg*dg+db*db;
                if(d<best) {
                    best = d;
                    index = j;
                }
            }
        }
        indices |= index<<(2*i);
    }
    out[0] = c0&255; out[1] = c0>>8;
    out[2] = c1&255; out[3] = c1>>8;
    for(int i=0; i<4; i++)
        out[4+i] = (indices>>(8*i))&255;
}

// DXTn blocks of pw x ph RGBA texels (whole 4x4 blocks): stb_dxt's colour blocks (DXT5 alpha blocks too), DXT3's
// explicit 4-bit alpha, and DXT1 with alpha in three-colour mode where a block has a transparent texel.
static void encode_dxt(GLenum dxtformat, const unsigned char *rgba, GLsizei pw, GLsizei ph, unsigned char *out) {
    const int dxt3 = dxtformat==GL_COMPRESSED_RGBA_S3TC_DXT3_EXT || dxtformat==GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT;
    const int dxt5 = dxtformat==GL_COMPRESSED_RGBA_S3TC_DXT5_EXT || dxtformat==GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT;
    const int dxt1a = dxt1_has_alpha(dxtformat);
    unsigned char block[64];
    for(int by=0; by<ph; by+=4)
        for(int bx=0; bx<pw; bx+=4) {
            for(int y=0; y<4; y++)
                memcpy(block+y*16, rgba+((by+y)*pw+bx)*4, 16);
            if(dxt3) {
                memset(out, 0, 8);
                for(int i=0; i<16; i++)
                    out[i/2] |= ((block[i*4+3]*15+127)/255)<<(4*(i&1));
                out += 8;
                stb_compress_dxt_block(out, block, 0, STB_DXT_NORMAL);
                out += 8;
            } else if(dxt5) {
                stb_compress_dxt_block(out, block, 1, STB_DXT_NORMAL);
                out += 16;
            } else {
                int opaque = -1, transparent = 0;
                for(int i=0; i<16; i++)
                    if(block[i*4+3]<128) transparent = 1; else if(opaque<0) opaque = i;
                // The transparent texels take an opaque one's colour, so that they do not pull the endpoints.
                if(dxt1a && transparent && opaque>=0)
                    for(int i=0; i<16; i++)
                        if(block[i*4+3]<128)
                            memcpy(block+i*4, block+opaque*4, 3);
                stb_compress_dxt_block(out, block, 0, STB_DXT_NORMAL);
                if(dxt1a && transparent)
                    dxt1_punch_through(out, block);
                out += 8;
            }
        }
}

// Encodes pw x ph RGBA texels (whole 4x4 blocks) to format; 0 on success.
static int encode_blocks(GLenum dxtformat, GLenum format, const unsigned char *rgba, GLsizei pw, GLsizei ph, unsigned char *out, GLsizei size) {
    if(isDXTc(format)) {
        encode_dxt(format, rgba, pw, ph, out);
        return 0;
    }
#ifdef GL4ES_TRANSCODE
    return (format==GL_COMPRESSED_RGBA_ASTC_4x4_KHR || format==GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR)
        ? gl4es_encode_astc4x4(rgba, pw, ph, out, size)
        : gl4es_encode_etc2(rgba, pw, ph, dxt_has_alpha(dxtformat), out);
#else
    return 1;
#endif
}

GLvoid *dxt_transcode(GLenum dxtformat, GLenum format, GLsizei width, GLsizei height, const GLvoid *data) {
#ifdef GL4ES_TRANSCODE
    const GLsizei pw = (width+3)&~3;
    const GLsizei ph = (height+3)&~3;
    unsigned char *rgba = decode_blocks(dxtformat, pw, ph, data);
    const GLsizei size = dxt_upload_size(format, width, height);
    unsigned char *out = (unsigned char*)malloc(size);
    const int error = encode_blocks(dxtformat, format, rgba, pw, ph, out, size);
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

GLvoid *dxt_decode(GLenum dxtformat, GLsizei width, GLsizei height, const GLvoid *data) {
    const GLsizei pw = (width+3)&~3;
    const GLsizei ph = (height+3)&~3;
    unsigned char *rgba = decode_blocks(dxtformat, pw, ph, data);
    if(pw!=width || ph!=height)
        for(int y=0; y<height; y++)
            memmove(rgba+y*width*4, rgba+y*pw*4, width*4);
    return rgba;
}

GLvoid *dxt_encode(GLenum dxtformat, GLenum format, GLsizei width, GLsizei height, const GLvoid *rgba) {
    const GLsizei pw = (width+3)&~3;
    const GLsizei ph = (height+3)&~3;
    const unsigned char *src = (const unsigned char*)rgba;
    unsigned char *padded = (unsigned char*)malloc(pw*ph*4);
    for(int y=0; y<ph; y++)
        for(int x=0; x<pw; x++)
            memcpy(padded+(y*pw+x)*4, src+(((y<height)?y:height-1)*width+((x<width)?x:width-1))*4, 4);
    const GLsizei size = dxt_upload_size(format, width, height);
    unsigned char *out = (unsigned char*)malloc(size);
    const int error = encode_blocks(dxtformat, format, padded, pw, ph, out, size);
    free(padded);
    if(error) {
        free(out);
        return NULL;
    }
    return out;
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
