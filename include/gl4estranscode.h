#ifndef _GL4ES_TRANSCODE_H_
#define _GL4ES_TRANSCODE_H_

// gl4es' handling of DXTn textures, for a renderer that uploads some textures to the driver itself (around gl4es):
// the format a DXTn image goes to the driver in (the DXTn format itself where the driver has S3TC, else ASTC 4x4
// or ETC2 in a GL4ES_TRANSCODE build, else 0: decode it to RGBA), the size of an image in that format, and the
// transcode. Valid once gl4es has initialised on the context.

#ifdef __cplusplus
extern "C" {
#endif

unsigned int gl4es_dxt_upload_format(unsigned int dxtformat);
int gl4es_dxt_upload_size(unsigned int format, int width, int height);
// A malloc'ed block image of gl4es_dxt_upload_size bytes (release it with free), or NULL when the encoder fails.
void *gl4es_dxt_transcode(unsigned int dxtformat, unsigned int format, int width, int height, const void *data);

#ifdef __cplusplus
}
#endif

#endif // _GL4ES_TRANSCODE_H_
