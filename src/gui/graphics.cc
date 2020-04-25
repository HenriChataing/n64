
#include <cstring>
#include <iostream>
#include <mutex>

#include <graphics.h>
#include <debugger.h>
#include <r4300/state.h>

#include <png.h>

/** Mutex for locking the graphics options while the UI thread is loading
 * the context. */
static std::mutex graphicsMutex;

/** Print GL errors that occured within the execution of the
 * previous commands */
static void glPrintError(char const *msg);

/** Current video image configuration. */
namespace VideoImage {
static size_t width;
static size_t height;
static size_t colorDepth;
static void *data = NULL;

static GLuint texture = 0;
static bool dirty = false;
};

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

/** Refresh the screen, called once during vertical blank. */
void refreshVideoImage(void)
{
    VideoImage::dirty = true;
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
                        : GL_UNSIGNED_SHORT_5_5_5_1;

            glGenTextures(1, &VideoImage::texture);
            glPrintError("glGenTextures");
            glBindTexture(GL_TEXTURE_2D, VideoImage::texture);
            glPrintError("glBindTextures");
            glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE);
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

static uint8_t convertComponent16to32(unsigned val)
{
    return (val * 255u + 16u) / 31u;
}

void exportAsPNG(char const *filename)
{
    if (VideoImage::data == NULL) {
        std::cerr << "Cannot export framebuffer: invalid information" << std::endl;
        return;
    }

    size_t width = VideoImage::width;
    size_t height = VideoImage::height;
    size_t colorDepth = VideoImage::colorDepth;
    unsigned char *data = (unsigned char *)VideoImage::data;

    unsigned png_color_type = PNG_COLOR_TYPE_RGB;
    size_t nr_channels = 3;
    size_t nr_vals = nr_channels * width * height;
    FILE *f = fopen(filename, "wb");

    if (f == NULL) {
        std::cerr << "Failed to open file '" << filename << "'" << std::endl;
        return;
    }

    png_byte *png_bytes = new png_byte[nr_vals];
    png_byte **png_rows = new png_byte *[height];

    for (size_t i = 0; i < width * height; i++) {
        if (colorDepth == 32) {
            png_bytes[nr_channels * i]     = data[4 * i + 3];
            png_bytes[nr_channels * i + 1] = data[4 * i + 2];
            png_bytes[nr_channels * i + 2] = data[4 * i + 1];
            /* alpha channel data[4 * i]; */
        } else {
            u16 pixel = ((u16)data[2 * i] << 8) | data[2 * i + 1];
            png_bytes[nr_channels * i]     = convertComponent16to32((pixel >> 11) & 0x1fu);
            png_bytes[nr_channels * i + 1] = convertComponent16to32((pixel >>  6) & 0x1fu);
            png_bytes[nr_channels * i + 2] = convertComponent16to32((pixel >>  1) & 0x1fu);
            /* alpha channel ((pixel & 1u) << 8) - 1u */;
        }
    }
    for (size_t i = 0; i < height; i++) {
        png_rows[i] = &png_bytes[i * width * nr_channels];
    }

    png_structp png = NULL;
    png_infop info = NULL;

    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png == NULL) {
        std::cerr << "Failed to create png write struct" << std::endl;
        goto abort;
    }
    info = png_create_info_struct(png);
    if (info == NULL) {
        std::cerr << "Failed to create png info struct" << std::endl;
        goto abort;
    }

    if (setjmp(png_jmpbuf(png))) {
        std::cerr << "Failed to write png image" << std::endl;
        goto abort;
    }

    png_init_io(png, f);
    png_set_IHDR(png, info, width, height, 8,
                 png_color_type,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    png_write_image(png, png_rows);
    png_write_end(png, NULL);

abort:
    delete png_bytes;
    delete png_rows;
    png_destroy_write_struct(&png, &info);
    fclose(f);
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
