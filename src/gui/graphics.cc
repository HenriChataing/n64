
#include <cstring>
#include <iostream>
#include <mutex>

#include <graphics.h>

/** Mutex for locking the graphics options while the UI thread is loading
 * the context. */
static std::mutex graphicsMutex;

/** Current video image configuration. */
namespace VideoImage {
static size_t width;
static size_t height;
static size_t colorDepth;
static void *data = NULL;

static GLuint texture = 0;
static bool dirty = false;
};

static void glPrintError(char const *msg);

/** Set the configuration of the framebuffer being displayed to the screen. */
void setVideoImage(size_t width, size_t height, size_t colorDepth, void *data)
{
    std::lock_guard<std::mutex> lock(graphicsMutex);

    std::cerr << "Video image : " << std::dec << width << " " << height;
    std::cerr << " " << colorDepth << std::endl;

    VideoImage::dirty |=
        VideoImage::width != width ||
        VideoImage::height != height ||
        VideoImage::colorDepth != colorDepth ||
        VideoImage::data != data;

    VideoImage::width = width;
    VideoImage::height = height;
    VideoImage::colorDepth = colorDepth;
    VideoImage::data = data;
}

/** Return the ID of a texture copied from the current video image, or
 * 0 if no vido image is set. */
bool getVideoImage(size_t *width, size_t *height, GLuint *id)
{
    std::lock_guard<std::mutex> lock(graphicsMutex);

    if (VideoImage::dirty) {
        VideoImage::dirty = false;
        if (VideoImage::texture != 0) {
            glDeleteTextures(1, &VideoImage::texture);
            VideoImage::dirty = false;
            VideoImage::texture = 0;
        }
        if (VideoImage::data != NULL) {
            GLenum type = VideoImage::colorDepth == 32
                        ? GL_UNSIGNED_INT_8_8_8_8
                        : GL_UNSIGNED_SHORT_4_4_4_4;

            glGenTextures(1, &VideoImage::texture);
            glPrintError("glGenTextures");
            glBindTexture(GL_TEXTURE_2D, VideoImage::texture);
            glPrintError("glBindTextures");
            glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
            glPixelStorei(GL_UNPACK_LSB_FIRST,  GL_FALSE);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPrintError("glPixelStorei");
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                         VideoImage::width, VideoImage::height,
                         0, GL_RGBA, type, VideoImage::data);
            glPrintError("glTexImage2D");
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glPrintError("glTexParameteri");
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    *width = VideoImage::width;
    *height = VideoImage::height;
    *id = VideoImage::texture;
    return VideoImage::data != NULL;
}

static char const *glGetErrorStr(GLenum err)
{
    switch (err) {
        case GL_NO_ERROR:          return "No error";
        case GL_INVALID_ENUM:      return "Invalid enum";
        case GL_INVALID_VALUE:     return "Invalid value";
        case GL_INVALID_OPERATION: return "Invalid operation";
        case GL_STACK_OVERFLOW:    return "Stack overflow";
        case GL_STACK_UNDERFLOW:   return "Stack underflow";
        case GL_OUT_OF_MEMORY:     return "Out of memory";
        default:                   return "Unknown error";
    }
}

static void glPrintError(char const *msg)
{
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::cout << "GL Error("<< msg <<"): " << glGetErrorStr(err) << std::endl;
    }
}
