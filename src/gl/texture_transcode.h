#ifndef _GL4ES_TEXTURE_TRANSCODE_H_
#define _GL4ES_TEXTURE_TRANSCODE_H_

#include "gles.h"

// The format a DXTn texture goes to the driver in: the DXTn format itself where the driver has S3TC, else ASTC
// 4x4 or ETC2 (GL4ES_TRANSCODE builds), else 0: gl4es decodes it to RGBA (texture_compressed.c).
GLenum dxt_upload_format(GLenum dxtformat);
// The size of a width x height image in format (DXTn, ASTC 4x4 or ETC2), in whole 4x4 blocks.
GLsizei dxt_upload_size(GLenum format, GLsizei width, GLsizei height);
// Transcodes width x height of dxtformat data to format (from dxt_upload_format); a malloc'ed block image of
// dxt_upload_size bytes, or NULL when the encoder fails.
GLvoid *dxt_transcode(GLenum dxtformat, GLenum format, GLsizei width, GLsizei height, const GLvoid *data);

#endif // _GL4ES_TEXTURE_TRANSCODE_H_
