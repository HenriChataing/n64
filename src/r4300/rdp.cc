
#include <climits>
#include <cstring>
#include <iomanip>
#include <iostream>

#include <debugger.h>
#include <r4300/rdp.h>
#include <r4300/hw.h>
#include <r4300/state.h>

/*
 * Notes:
 * - framebuffer and zbuffer use the 9bit datapath to the DRAM, the extra bit
 *   being used to improve precision on the z and coverage values.
 *   The actual 16 bit color format is: R(5):G(5):B(5):cvg(3),
 *   and the z format: z(14),dz(4).
 *
 * - the RDP graphics pipeline performs most operations at 8 bits per component
 *   RGBA pixel. After searching the texels, the texture unit will convert them
 *   to 32bit RGBA format.
 *
 * - the RDP color palette uses the 4 upper banks of the texture memory.
 *   The banks are loaded with identical values in order to be able to perform
 *   up to 4 parallel accesses. In fine: the color palette is a quadricated
 *   array of 256 16bit color values.
 */

namespace R4300 {

using namespace R4300;

/** Image data format set by the SetColorImage DPC command. */
enum image_data_format {
    IMAGE_DATA_FORMAT_RGBA = 0,
    IMAGE_DATA_FORMAT_YUV = 1,
    IMAGE_DATA_FORMAT_CI = 2,
    IMAGE_DATA_FORMAT_IA = 3,
    IMAGE_DATA_FORMAT_I = 4,
};

/** Pixel size. */
enum pixel_size {
    PIXEL_SIZE_4B = 0,
    PIXEL_SIZE_8B = 1,
    PIXEL_SIZE_16B = 2,
    PIXEL_SIZE_32B = 3,
};

/* Image data format with the pixel size included, for easier matching. */
enum image_data_type {
    IMAGE_DATA_FORMAT_I_4 = 0,
    IMAGE_DATA_FORMAT_IA_3_1,
    IMAGE_DATA_FORMAT_CI_4,
    IMAGE_DATA_FORMAT_I_8,
    IMAGE_DATA_FORMAT_IA_4_4,
    IMAGE_DATA_FORMAT_CI_8,
    IMAGE_DATA_FORMAT_RGBA_5_5_5_1,
    IMAGE_DATA_FORMAT_IA_8_8,
    IMAGE_DATA_FORMAT_YUV_16,
    IMAGE_DATA_FORMAT_RGBA_8_8_8_8,
    IMAGE_DATA_FORMAT_INVAL = -1,
};

enum image_data_type convert_image_data_format(enum image_data_format format,
                                               enum pixel_size size) {
    const enum image_data_type types[8][4] = {
        { IMAGE_DATA_FORMAT_INVAL,          IMAGE_DATA_FORMAT_INVAL,
          IMAGE_DATA_FORMAT_RGBA_5_5_5_1,   IMAGE_DATA_FORMAT_RGBA_8_8_8_8, },
        { IMAGE_DATA_FORMAT_INVAL,          IMAGE_DATA_FORMAT_INVAL,
          IMAGE_DATA_FORMAT_YUV_16,         IMAGE_DATA_FORMAT_INVAL, },
        { IMAGE_DATA_FORMAT_CI_4,           IMAGE_DATA_FORMAT_CI_8,
          IMAGE_DATA_FORMAT_INVAL,          IMAGE_DATA_FORMAT_INVAL, },
        { IMAGE_DATA_FORMAT_IA_3_1,         IMAGE_DATA_FORMAT_IA_4_4,
          IMAGE_DATA_FORMAT_IA_8_8,         IMAGE_DATA_FORMAT_INVAL, },
        { IMAGE_DATA_FORMAT_I_4,            IMAGE_DATA_FORMAT_I_8,
          IMAGE_DATA_FORMAT_INVAL,          IMAGE_DATA_FORMAT_INVAL, },
        { IMAGE_DATA_FORMAT_INVAL,          IMAGE_DATA_FORMAT_INVAL,
          IMAGE_DATA_FORMAT_INVAL,          IMAGE_DATA_FORMAT_INVAL, },
        { IMAGE_DATA_FORMAT_INVAL,          IMAGE_DATA_FORMAT_INVAL,
          IMAGE_DATA_FORMAT_INVAL,          IMAGE_DATA_FORMAT_INVAL, },
        { IMAGE_DATA_FORMAT_INVAL,          IMAGE_DATA_FORMAT_INVAL,
          IMAGE_DATA_FORMAT_INVAL,          IMAGE_DATA_FORMAT_INVAL, },
    };
    return types[format][size];
}

/**
 * Internal color representation. The RDP graphics pipeline performs most
 * operations at 8 bits per component RGBA pixel. After searching the texels,
 * the texture unit will convert them to 32bit RGBA format.
 */
typedef struct color {
    u8 r, g, b, a;
} color_t;

/**
 * Representation of the current RDP configuration.
 */
struct rdp {
    /* Color registers. */
    u32         fill_color;
    color_t     fog_color;
    color_t     blend_color;
    color_t     prim_color;
    color_t     env_color;

    /* Coefficients for YUV->RGB color conversion,
     * in S1.7 fixpoint format, sign extended to S8.7. */
    struct {
        i16 k0, k1, k2, k3, k4, k5;
    } convert;

    /* Parameters for chroma keying. */
    struct {
        unsigned width_r;
        unsigned width_g;
        unsigned width_b;
        color_t  center; /* alpha ignored */
        color_t  scale;  /* alpha ignored */
    } key;
};

/**
 * Representation of the internal RDP state for the rendering
 * of a single pixel.
 */
typedef struct pixel {
    /* RS */
    struct { i32 x, y; }    edge_coefs;
    struct { i32 s, t, w; } texture_coefs;
    struct {
        u32 z;
        u16 deltaz;
    }                       zbuffer_coefs;
    color_t                 shade_color;
    /* TX */
    struct tile const *     tile;
    color_t                 texel_colors[4];
    i32                     lod_frac;
    i32                     prim_lod_frac;
    /* TF */
    color_t                 texel0_color;
    color_t                 texel1_color;   /* 2-cycle mode */
    /* CC */
    color_t                 combined_color;
    /* BL */
    color_t                 blended_color;  /* 2-cycle mode */
    /* MI */
    color_t                 mem_color;
    u32                     mem_z; /* U15.3 */
    i16                     mem_deltaz; /* S15 */
    unsigned                mem_color_addr;
    unsigned                mem_z_addr;
} pixel_t;

struct rdp rdp;

/** Current texture image configuration. */
static struct {
    enum image_data_format  format;
    enum pixel_size         size;
    enum image_data_type    type;
    unsigned width;         /**< Width of image in pixels. */
    u32      addr;
} texture_image;

/** Current color image configuration. */
static struct {
    enum image_data_format  format;
    enum pixel_size         size;
    enum image_data_type    type;
    unsigned width;         /**< Width of image in pixels. */
    u32      addr;
} color_image;

/** Current Z image configuration. */
static struct {
    u32      addr;
} z_image;

static struct tile {
    enum image_data_type type;
    enum image_data_format format;
    enum pixel_size size;
    unsigned line;          /**< Size of tile line in 64b words, max of 4KB */
    unsigned tmem_addr;     /**< Starting Tmem address for this tile in 64b words, 4KB range */
    /* Palette number for 4b Color Indexed texels.
     * This number is used as the MS 4b of an 8b index. */
    unsigned palette;
    bool clamp_t;           /**< Clamp enable for T direction */
    bool mirror_t;          /**< Mirror enable for T direction */
    /* Mask for wrapping/mirroring in T direction.
     * If this field is zero then clamp, otherwise pass
     * (mask) LSBs of T address.*/
    unsigned mask_t;
    unsigned shift_t;       /**< Level of detail shift for T addresses */
    bool clamp_s;           /**< Clamp enable for S direction */
    bool mirror_s;          /**< Mirror enable for S direction */
    /* Mask for wrapping/mirroring in S direction.
     * If this field is zero then clamp, otherwise pass
     * (mask) LSBs of T address.*/
    unsigned mask_s;
    unsigned shift_s;       /**< Level of detail shift for S addresses */
    /* Coordinates of tile in texture space, in U10.2 fixpoint format. */
    unsigned sl;
    unsigned tl;
    unsigned sh;
    unsigned th;
} tiles[8];

static struct {
    unsigned xh;
    unsigned yh;
    unsigned xl;
    unsigned yl;
    bool skipOddLines;
    bool skipEvenLines;
} scissor;

/** Cycle type set by the SetOtherModes DPC command */
enum cycle_type {
    CYCLE_TYPE_1CYCLE = 0,
    CYCLE_TYPE_2CYCLE = 1,
    CYCLE_TYPE_COPY   = 2,
    CYCLE_TYPE_FILL   = 3,
};

enum tlut_type {
    TLUT_TYPE_RGBA = 0,
    TLUT_TYPE_IA   = 1,
};

enum sample_type {
    SAMPLE_TYPE_1X1 = 0,
    SAMPLE_TYPE_2X2 = 1,
    SAMPLE_TYPE_4X1 = 2, /* When cycle_type == COPY */
};

enum rgb_dither_sel {
    SEL_RGB_MAGIC_SQUARE = 0,
    SEL_RGB_BAYER_MATRIX = 1,
    SEL_RGB_NOISE = 2,
    SEL_RGB_NONE = 3,
};

enum alpha_dither_sel {
    SEL_ALPHA_PATTERN = 0,
    SEL_ALPHA_NEG_PATTERN = 1,
    SEL_ALPHA_NOISE = 2,
    SEL_ALPHA_NONE = 3,
};

enum blender_src_sel {
    /* P and M mux input source selection */
    BLENDER_SRC_SEL_IN_COLOR = 0,
    BLENDER_SRC_SEL_MEM_COLOR = 1,
    BLENDER_SRC_SEL_BLEND_COLOR = 2,
    BLENDER_SRC_SEL_FOG_COLOR = 3,

    /* A mux input source selection */
    BLENDER_SRC_SEL_IN_ALPHA = 0,
    BLENDER_SRC_SEL_FOG_ALPHA = 1,
    BLENDER_SRC_SEL_SHADE_ALPHA = 2,
    BLENDER_SRC_SEL_0 = 3,

    /* B mux input source selection */
    BLENDER_SRC_SEL_1_AMUX = 0,
    BLENDER_SRC_SEL_MEM_ALPHA = 1,
    BLENDER_SRC_SEL_1 = 2,
};

enum z_mode {
    ZMODE_OPAQUE = 0,
    ZMODE_INTERPENETRATING = 1,
    ZMODE_TRANSPARENT = 2,
    ZMODE_DECAL = 3,
};

enum cvg_dest {
    DEST_CLAMP = 0,
    DEST_WRAP = 1,
    DEST_ZAP = 2,
    DEST_SAVE = 3,
};

enum z_source_sel {
    SEL_PRIMITIVE = 0,
    SEL_PIXEL = 0,
};

/**
 * yl, ym, yh are saved in signed S29.2 fixpoint format
 * (signed extended from S11.2). Other values are in signed S15.16
 * fixpoint format.
 */
struct edge_coefs {
    i32 yl, ym, yh;
    i32 xl, xm, xh;
    i32 dxldy, dxmdy, dxhdy;
};

/** All coefficients are in signed S15.16 fixpoint format. */
struct shade_coefs {
    i32 r, g, b, a;
    i32 drdx, dgdx, dbdx, dadx;
    i32 drde, dgde, dbde, dade;
    i32 drdy, dgdy, dbdy, dady;
};

/** All coefficients are in signed S10.21 fixpoint format. */
struct texture_coefs {
    i32 s, t, w;
    i32 dsdx, dtdx, dwdx;
    i32 dsde, dtde, dwde;
    i32 dsdy, dtdy, dwdy;
};

/** All coefficients are in signed S15.16 fixpoint format. */
struct zbuffer_coefs {
    i32 z;
    i32 dzdx;
    i32 dzde;
    i32 dzdy;
};

static struct {
    bool atomic_prim;
    enum cycle_type cycle_type;
    bool persp_tex_en;
    bool detail_tex_en;
    bool sharpen_tex_en;
    bool tex_lod_en;
    bool tlut_en;
    enum tlut_type tlut_type;
    enum sample_type sample_type;
    bool mid_texel;
    bool bi_lerp_0;
    bool bi_lerp_1;
    bool convert_one;
    bool key_en;
    enum rgb_dither_sel rgb_dither_sel;
    enum alpha_dither_sel alpha_dither_sel;
    enum blender_src_sel b_m1a_0;
    enum blender_src_sel b_m1a_1;
    enum blender_src_sel b_m1b_0;
    enum blender_src_sel b_m1b_1;
    enum blender_src_sel b_m2a_0;
    enum blender_src_sel b_m2a_1;
    enum blender_src_sel b_m2b_0;
    enum blender_src_sel b_m2b_1;
    bool force_blend;
    bool alpha_cvg_select;
    bool cvg_times_alpha;
    enum z_mode z_mode;
    enum cvg_dest cvg_dest;
    bool color_on_cvg;
    bool image_read_en;
    bool z_update_en;
    bool z_compare_en;
    bool antialias_en;
    enum z_source_sel z_source_sel;
    bool dither_alpha_en;
    bool alpha_compare_en;
} other_modes;

struct {
    unsigned sub_a_R_0;
    unsigned sub_b_R_0;
    unsigned mul_R_0;
    unsigned add_R_0;

    unsigned sub_a_A_0;
    unsigned sub_b_A_0;
    unsigned mul_A_0;
    unsigned add_A_0;

    unsigned sub_a_R_1;
    unsigned sub_b_R_1;
    unsigned mul_R_1;
    unsigned add_R_1;

    unsigned sub_a_A_1;
    unsigned sub_b_A_1;
    unsigned mul_A_1;
    unsigned add_A_1;
} combine_mode;

static u32 prim_z;
static u16 prim_deltaz;

static u8 noise() {
    return 128;
}

static float i32_fixpoint_to_float(i32 val, int radix) {
    unsigned long div = 1lu << radix;
    double fval = (i64)val;// val < 0 ? - : (i64)val;
    return fval / (float)div;
}

static float s29_2_to_float(i32 val) {
    return i32_fixpoint_to_float(val, 2);
}

static float s15_16_to_float(i32 val) {
    return i32_fixpoint_to_float(val, 16);
}

static float s10_21_to_float(i32 val) {
    return i32_fixpoint_to_float(val, 21);
}


static void print_pixel(pixel_t *px) {
    debugger::debug(Debugger::RDP,
        "  x,y,z:{},{},{}"
        "  tex:{},{},{}"
        "  shade:{},{},{},{}"
        "  texel0:{},{},{},{}"
        "  combined:{},{},{},{}"
        "  blended:{},{},{},{}"
        "  mem:{},{},{},{}",
        px->edge_coefs.x, px->edge_coefs.y, px->zbuffer_coefs.z,
        s10_21_to_float(px->texture_coefs.s),
        s10_21_to_float(px->texture_coefs.t),
        s10_21_to_float(px->texture_coefs.w),
        (int)px->shade_color.r, (int)px->shade_color.g,
        (int)px->shade_color.b, (int)px->shade_color.a,
        (int)px->texel0_color.r, (int)px->texel0_color.g,
        (int)px->texel0_color.b, (int)px->texel0_color.a,
        (int)px->combined_color.r, (int)px->combined_color.g,
        (int)px->combined_color.b, (int)px->combined_color.a,
        (int)px->blended_color.r, (int)px->blended_color.g,
        (int)px->blended_color.b, (int)px->blended_color.a,
        (int)px->mem_color.r, (int)px->mem_color.g,
        (int)px->mem_color.b, (int)px->mem_color.a);
}

namespace FillMode {

/**
 * In fill mode most of the rendering pipeline is bypassed.
 * Pixels are written by two or four depending on the color format.
 */

/** @brief Fills the line with coordinates (xs,y), (xe, y) with the
 * fill color. Input coordinates are in 10.2 fixedpoint format. */
static void renderLine(unsigned y, unsigned xs, unsigned xe) {
    if (y < scissor.yh || y >= scissor.yl ||
        (scissor.skipOddLines && ((y >> 2) % 2)) ||
        (scissor.skipEvenLines && !((y >> 2) % 2)))
        return;

    // Clip x coordinate and convert to integer values
    // from fixed point 10.2 format.
    y = y >> 2;
    xs = std::max(xs, scissor.xh) >> 2;
    xe = std::min(xe, scissor.xl) >> 2;

    unsigned offset = color_image.addr +
                      ((xs + y * color_image.width) << (color_image.size - 1));
    unsigned length = (xe - xs) << (color_image.size - 1);

    if ((offset + length) > sizeof(state.dram)) {
        debugger::warn(Debugger::RDP,
            "(fill) renderLine out-of-bounds, start:{}, length:{}",
            offset, length);
        debugger::halt("FillMode::renderLine out-of-bounds");
        return;
    }

    if (color_image.type == IMAGE_DATA_FORMAT_RGBA_5_5_5_1) {
        u32 filler = __builtin_bswap32(rdp.fill_color);
        if (xs % 2) {
            // Copy first half-word manually.
            *(u16 *)(void *)&state.dram[offset] =
                __builtin_bswap16(rdp.fill_color);
            offset += 2; xs++;
        }
        // Now aligned to u32, can copy two half-words at a time.
        u32 *base = (u32 *)(void *)&state.dram[offset];
        unsigned x;
        for (x = xs; (x+1) <= xe; x+=2, base++) {
            *base = filler;
        }
        if (x <= xe) {
            *(u16 *)base = filler;
        }
    }
    else if (color_image.type == IMAGE_DATA_FORMAT_RGBA_8_8_8_8) {
        u32 *base = (u32 *)(void *)&state.dram[offset];
        for (unsigned x = xs; x < xe; x++, base++) {
            *base = __builtin_bswap32(rdp.fill_color);
        }
    }
    else {
        debugger::halt("FillMode::renderLine unsupported image data format");
    }
}

}; /* FillMode */

namespace Cycle1Mode {

/**
 * The pipeline in cycle1 mode is as follows (ref section 12.1.2)
 *
 *  +----+    +----+    +----+    +----+    +----+    +----+
 *  | RS | -> | TX | -> | TF | -> | CC | -> | BL | -> | MI | -> DRAM
 *  |    |    |    |    |    |    |    |    |    | <- |    | <-
 *  +----+    +----+    +----+    +----+    +----+    +----+
 *               ^
 *              DRAM
 *
 *  - RS (Rasterizer) Generates pixels and their attributes.
 *  - TX (Texture Mapping) Generates the four texels closest to a pixel in
 *       the texture map.
 *  - TF (Texture Filtering) Bilinear filtering of the four texels to
 *       generate one texel, OR performs step 1 of YUV-RGB conversion
 *  - CC (Color Combinator) Combines various colors into one color,
 *       OR performs step 2 of YUV-RGB conversion.
 *  - BL (Blending) Blends the pixel with the pixel in framebuffer memory,
 *       OR applies fog and writes to the framebuffer.
 *  - MI (Memory Interface) Framebuffer loads and stores.
 */

static void pipeline_tx(pixel_t *px);
static void pipeline_tx_load(struct tile const *tile, unsigned addr, color_t *tx);
static void pipeline_tf(pixel_t *px);
static void pipeline_cc(pixel_t *px);
static void pipeline_bl(pixel_t *px);
static void pipeline_mi_store(pixel_t *px);
static void pipeline_mi_load(pixel_t *px);

/**
 * Execute the texture pipeline module TX.
 * Inputs the point texture coordinates s, t, w generated by the rasterizer,
 * and outputs four texels sampled as 2x2 or 4x1 depending on the sample_type
 * configuration.
 */
static void pipeline_tx(pixel_t *px) {
    i32 s = px->texture_coefs.s;
    i32 t = px->texture_coefs.t;
    i32 w = px->texture_coefs.w;
    struct tile const *tile = px->tile;

    /* Perform perspective correction if enabled.
     * W is the normalized inverse depth. */
    if (other_modes.persp_tex_en && w != 0) {
        s /= w;
        t /= w;
    }
    /* Apply shifts for different LODs. */
    if (tile->shift_s < 11) {
        s >>= tile->shift_s;
    } else {
        s <<= 16 - tile->shift_s;
    }
    if (tile->shift_t < 11) {
        t >>= tile->shift_t;
    } else {
        t <<= 16 - tile->shift_t;
    }

    /* Convert the texture coordinates to tile based coordinates
     * values, removing the fractional part.
     * Apply wrap, mirror and clamp processing. */
    i32 s_tile = (i32)((s >> 19) - tile->sl) >> 2;
    i32 t_tile = (i32)((t >> 19) - tile->tl) >> 2;
    // For texture filtering:
    // i32 s_frac = ;
    // i32 t_frac = ;

    i32 s_tile_max = (tile->sh - tile->sl) >> 2;
    i32 t_tile_max = (tile->th - tile->tl) >> 2;
    u32 mirror_s_bit = 1u << tile->mask_s;
    u32 mask_s = mirror_s_bit - 1u;
    u32 mirror_t_bit = 1u << tile->mask_t;
    u32 mask_t = mirror_t_bit - 1u;

    /* Clamping, implicit when the mask is null. */
    if (mask_s == 0 || tile->clamp_s) {
        if (s_tile < 0)             s_tile = 0;
        if (s_tile > s_tile_max)    s_tile = s_tile_max;
    }
    if (mask_t == 0 || tile->clamp_t) {
        if (t_tile < 0)             t_tile = 0;
        if (t_tile > t_tile_max)    t_tile = t_tile_max;
    }
    /* Mirroring and wrapping. */
    if (mask_s != 0) {
        if (tile->mirror_s && ((u32)s_tile & mirror_s_bit) != 0)
            s_tile = ~(u32)s_tile & mask_s;
        else
            s_tile = (u32)s_tile & mask_s;
    }
    if (mask_t != 0) {
        if (tile->mirror_t && ((u32)t_tile & mirror_t_bit) != 0)
            t_tile = ~(u32)t_tile & mask_t;
        else
            t_tile = (u32)t_tile & mask_t;
    }

    /* Address of the texel closest to the rasterized point.
     * The value is an offset into tmem memory, multiplied by two
     * to account for 4bit texel addressing. */
    unsigned addr =
        (tile->tmem_addr << 4) +
        ((tile->tl >> 2) + t_tile) * (tile->line << 4) +
        (((tile->sl >> 2) + s_tile) << tile->size);

    switch (other_modes.sample_type) {
        case SAMPLE_TYPE_1X1:
            pipeline_tx_load(tile, addr, &px->texel_colors[0]);
            px->texel_colors[1] = px->texel_colors[0];
            px->texel_colors[2] = px->texel_colors[0];
            px->texel_colors[3] = px->texel_colors[0];
            break;
        case SAMPLE_TYPE_2X2:
            pipeline_tx_load(tile, addr,
                             &px->texel_colors[0]);
            pipeline_tx_load(tile, addr + (1u << tile->size),
                             &px->texel_colors[1]);
            pipeline_tx_load(tile, addr + (tile->line << 2),
                             &px->texel_colors[2]);
            pipeline_tx_load(tile, addr + (tile->line << 2) + (1u << tile->size),
                             &px->texel_colors[3]);
            break;
        case SAMPLE_TYPE_4X1:
            pipeline_tx_load(tile, addr, &px->texel_colors[0]);
            pipeline_tx_load(tile, addr + (1u << tile->size), &px->texel_colors[1]);
            pipeline_tx_load(tile, addr + (2u << tile->size), &px->texel_colors[2]);
            pipeline_tx_load(tile, addr + (3u << tile->size), &px->texel_colors[3]);
            break;
    }
}

/**
 * Lookup a texel color from palette memory.
 * Converts the color to 8-bit per component RGBA values according to the
 * configured tlut_type.
 *
 * Note: the RDP performs parallel palette loads for different texel smaples,
 * as a simplification the color is always loaded from the first palette.
 * This can bring different results if the user overwrites a palette
 * copy loading a tile.
 */
static void pipeline_palette_load(u8 ci, color_t *tx) {
    u16 val = __builtin_bswap16(*(u16 *)&state.tmem[0x800 + (ci << 1)]);
    switch (other_modes.tlut_type) {
    /* I[15:8],A[7:0] =>
     * R [15:8]
     * G [15:8]
     * B [15:8]
     * A [7:0] */
    case TLUT_TYPE_IA:
        tx->r = tx->g = tx->b = val >> 8;
        tx->a = val;
        break;
    /* R[15:11],G[10:6],G[5:1],A[0] =>
     * R {[15:11],[15:13]}
     * G {[10:6],[10:8]}
     * B {[5:1],[5:3]}
     * A 255*[0] */
    case TLUT_TYPE_RGBA: {
        u8 r = (val >> 11) & 0x1fu;
        u8 g = (val >>  6) & 0x1fu;
        u8 b = (val >>  1) & 0x1fu;
        tx->r = (r << 3) | (r >> 2);
        tx->g = (g << 3) | (g >> 2);
        tx->b = (b << 3) | (b >> 2);
        tx->a = (val & 1u) ? 255 : 0;
        break;
    }
    }
}

/**
 * Load a texel from texture RAM.
 * Perform palette lookup if the tile format is color index.
 * The RDP graphics pipeline performs most operations at 8 bits per component
 * RGBA pixel. After searching the texels, the texture unit will convert them
 * to 32bit RGBA format.
 */
static void pipeline_tx_load(struct tile const *tile, unsigned addr, color_t *tx) {
    switch (tile->type) {
    /* I[3:0] =>
     * R {[3:0],[3:0]}
     * G {[3:0],[3:0]}
     * B {[3:0],[3:0]}
     * A {[3:0],[3:0]} */
    case IMAGE_DATA_FORMAT_I_4: {
        unsigned shift = (addr & 1u) ? 0 : 4;
        u8 i = (state.tmem[addr >> 1] >> shift) & 0xfu;
        tx->r = tx->g = tx->b = tx->a = i | (i << 4);
        break;
    }
    /* I[3:1],A[0] =>
     * R {[3:1],[3:1],[3:2]}
     * G {[3:1],[3:1],[3:2]}
     * B {[3:1],[3:1],[3:2]}
     * A 255*[0] */
    case IMAGE_DATA_FORMAT_IA_3_1: {
        unsigned shift = (addr & 1u) ? 0 : 4;
        u8 ia = (state.tmem[addr >> 1] >> shift) & 0xfu;
        u8 i = ia >> 1;
        tx->r = tx->g = tx->b = (i >> 1) | (i << 2) | (i << 5);
        tx->a = (ia & 1u) ? 255 : 0;
        break;
    }
    /* CI[3:0] */
    case IMAGE_DATA_FORMAT_CI_4: {
        unsigned shift = (addr & 1u) ? 0 : 4;
        u8 ci = (state.tmem[addr >> 1] >> shift) & 0xfu;
        ci |= tile->palette << 4;
        pipeline_palette_load(ci, tx);
        break;
    }
    /* I[7:0] =>
     * R [7:0]
     * G [7:0]
     * B [7:0]
     * A [7:0] */
    case IMAGE_DATA_FORMAT_I_8:
        tx->r = tx->g = tx->b = tx->a = state.tmem[addr >> 1];
        break;
    /* I[7:4],A[3:0] =>
     * R {[7:4],[7:4]}
     * G {[7:4],[7:4]}
     * B {[7:4],[7:4]}
     * A {[3:0],[3:0]} */
    case IMAGE_DATA_FORMAT_IA_4_4: {
        u8 ia = state.tmem[addr >> 1];
        u8 i = ia >> 4;
        u8 a = ia & 0xfu;
        tx->r = tx->g = tx->b = i | (i << 4);
        tx->a = a | (a << 4);
        break;
    }
    /* CI[7:0] */
    case IMAGE_DATA_FORMAT_CI_8:
        pipeline_palette_load(state.tmem[addr >> 1], tx);
        break;
    /* R[15:11],G[10:6],G[5:1],A[0] =>
     * R {[15:11],[15:13]}
     * G {[10:6],[10:8]}
     * B {[5:1],[5:3]}
     * A 255*[0] */
    case IMAGE_DATA_FORMAT_RGBA_5_5_5_1: {
        u16 rgba = __builtin_bswap16(*(u16 *)&state.tmem[addr >> 1]);
        u8 r = (rgba >> 11) & 0x1fu;
        u8 g = (rgba >>  6) & 0x1fu;
        u8 b = (rgba >>  1) & 0x1fu;
        tx->r = (r << 3) | (r >> 2);
        tx->g = (g << 3) | (g >> 2);
        tx->b = (b << 3) | (b >> 2);
        tx->a = (rgba & 1u) ? 255 : 0;
        break;
    }
    /* I[15:8],A[7:0] =>
     * R [15:8]
     * G [15:8]
     * B [15:8]
     * A [7:0] */
    case IMAGE_DATA_FORMAT_IA_8_8:
        debugger::halt("pipeline_tx_load: unsupported image data type IA_8_8");
        break;
    case IMAGE_DATA_FORMAT_YUV_16:
        debugger::halt("pipeline_tx_load: unsupported image data type YUV_16");
        break;
    /* R[31:24],G[23:16],G[15:8],A[7:0] =>
     * R [31:24]
     * G [23:16]
     * B [15:8]
     * A [7:0] */
    case IMAGE_DATA_FORMAT_RGBA_8_8_8_8: {
        u32 rgba = __builtin_bswap32(*(u32 *)&state.tmem[addr >> 1]);
        tx->r = (rgba >> 24) & 0xffu;
        tx->g = (rgba >> 16) & 0xffu;
        tx->b = (rgba >>  8) & 0xffu;
        tx->a = (rgba >>  0) & 0xffu;
        break;
    }
    default:
        debugger::halt("pipeline_tx_load: unexpected image data type");
        break;
    }
    // if (other_modes.tlut_en) {
    //     debugger::halt("tlut_en not implemented");
    // } else {
    //     debugger::halt("tlut_en not enabled");
    //     tx->r = tx->g = tx->b = tx->a = 0;
    // }
}

static void pipeline_tf(pixel_t *px) {
    px->texel0_color = px->texel_colors[0];
    px->lod_frac = 255;
}

/**
 * The CC combines the TX generated texels with the RS generated step RGBA
 * pixel values. The color combiner is the final stage paint mixer that takes
 * two color values from various color sources and linearly interpolates
 * between the two colors. CC basically performs the following equation:
 *
 *      newcolor = (A-B) × C + D
 *
 * In the above equation, A, B, C and D can be color values input from various
 * sources. If D = B, it will be a simple bilinear conversion.
 *
 *  COMBINED        output from 1-cycle mode
 *  TEXEL0          texture map output
 *  TEXEL1          texture map output from tile+1
 *  PRIMITIVE       primitive color
 *  SHADE           shade color
 *  ENVIRONMENT     environment color
 *  CENTER          chroma key center value
 *  SCALE           chroma key scale value
 *  COMBINED_ALPHA
 *  TEXEL0_ALPHA
 *  TEXEL1_ALPHA
 *  PRIMITIVE_ALPHA
 *  SHADE_ALPHA
 *  ENVIRONMENT_ALPHA
 *  LOD_FRACTION    LOD fraction
 *  PRIM_LOD_FRAC   Primitive LOD fraction
 *  NOISE           noise (random)
 *  K4              color conversion constant K4
 *  K5              color conversion constant K5
 *  1               1.0
 *  0               0.0
 */
static void pipeline_cc(pixel_t *px) {
    color_t sub_a;
    color_t sub_b;
    color_t mul;
    color_t add;

    switch (combine_mode.sub_a_R_1) {
    case 0 /* COMBINED */:      sub_a = px->combined_color; break;
    case 1 /* TEXEL0 */:        sub_a = px->texel0_color; break;
    case 2 /* TEXEL1 */:        sub_a = px->texel1_color; break;
    case 3 /* PRIMITIVE */:     sub_a = rdp.prim_color; break;
    case 4 /* SHADE */:         sub_a = px->shade_color; break;
    case 5 /* ENVIRONMENT */:   sub_a = rdp.env_color; break;
    case 6 /* 1 */:             sub_a.r = sub_a.g = sub_a.b = 255; break;
    case 7 /* NOISE */:
        sub_a.r = noise();
        sub_a.g = noise();
        sub_a.b = noise();
        break;
    default /* 0 */:            sub_a.r = sub_a.g = sub_a.b = 0; break;
    }
    switch (combine_mode.sub_b_R_1) {
    case 0 /* COMBINED */:      sub_b = px->combined_color; break;
    case 1 /* TEXEL0 */:        sub_b = px->texel0_color; break;
    case 2 /* TEXEL1 */:        sub_b = px->texel1_color; break;
    case 3 /* PRIMITIVE */:     sub_b = rdp.prim_color; break;
    case 4 /* SHADE */:         sub_b = px->shade_color; break;
    case 5 /* ENVIRONMENT */:   sub_b = rdp.env_color; break;
    case 6 /* CENTER */:        sub_b = rdp.key.center; break;
    case 7 /* K4 */:            sub_b.r = sub_b.g = sub_b.b = rdp.convert.k4; break;
    default /* 0 */:            sub_b.r = sub_b.g = sub_b.b = 0; break;
    }
    switch (combine_mode.mul_R_1) {
    case 0 /* COMBINED */:      mul = px->combined_color; break;
    case 1 /* TEXEL0 */:        mul = px->texel0_color; break;
    case 2 /* TEXEL1 */:        mul = px->texel1_color; break;
    case 3 /* PRIMITIVE */:     mul = rdp.prim_color; break;
    case 4 /* SHADE */:         mul = px->shade_color; break;
    case 5 /* ENVIRONMENT */:   mul = rdp.env_color; break;
    case 6 /* SCALE */:         mul = rdp.key.scale; break;
    case 7 /* COMBINED A */:    mul.r = mul.g = mul.b = px->combined_color.a; break;
    case 8 /* TEXEL0 A */:      mul.r = mul.g = mul.b = px->texel0_color.a; break;
    case 9 /* TEXEL1 A */:      mul.r = mul.g = mul.b = px->texel1_color.a; break;
    case 10 /* PRIMITIVE A */:  mul.r = mul.g = mul.b = rdp.prim_color.a; break;
    case 11 /* SHADE A */:      mul.r = mul.g = mul.b = px->shade_color.a; break;
    case 12 /* ENVIRONMENT A */:    mul.r = mul.g = mul.b = rdp.env_color.a; break;
    case 13 /* LOD FRACTION */:     mul.r = mul.g = mul.b = px->lod_frac; break;
    case 14 /* PRIM LOD FRAC */:    mul.r = mul.g = mul.b = px->prim_lod_frac; break;
    case 15 /* K5 */:           mul.r = mul.g = mul.b = rdp.convert.k4; break;
    default /* 0 */:            mul.r = mul.g = mul.b = 0; break;
    }
    switch (combine_mode.add_R_1) {
    case 0 /* COMBINED */:      add = px->combined_color; break;
    case 1 /* TEXEL0 */:        add = px->texel0_color; break;
    case 2 /* TEXEL1 */:        add = px->texel1_color; break;
    case 3 /* PRIMITIVE */:     add = rdp.prim_color; break;
    case 4 /* SHADE */:         add = px->shade_color; break;
    case 5 /* ENVIRONMENT */:   add = rdp.env_color; break;
    case 6 /* 1 */:             add.r = add.g = add.b = 255; break;
    default /* 0 */:            add.r = add.g = add.b = 0; break;
    }

    /* The multiplier is converted to 0.8 fixpoint format. */
    px->combined_color.r = ((((sub_a.r - sub_b.r)) * mul.r) >> 8) + add.r;
    px->combined_color.g = ((((sub_a.g - sub_b.g)) * mul.g) >> 8) + add.g;
    px->combined_color.b = ((((sub_a.b - sub_b.b)) * mul.b) >> 8) + add.b;

    switch (combine_mode.sub_a_A_1) {
    case 0 /* COMBINED A */:    sub_a.a = px->combined_color.a; break;
    case 1 /* TEXEL0 A */:      sub_a.a = px->texel0_color.a; break;
    case 2 /* TEXEL1 A */:      sub_a.a = px->texel1_color.a; break;
    case 3 /* PRIMITIVE A */:   sub_a.a = rdp.prim_color.a; break;
    case 4 /* SHADE A */:       sub_a.a = px->shade_color.a; break;
    case 5 /* ENVIRONMENT A */: sub_a.a = rdp.env_color.a; break;
    case 6 /* 1 */:             sub_a.a = 255; break;
    default /* 0 */:            sub_a.a = 0; break;
    }
    switch (combine_mode.sub_b_A_1) {
    case 0 /* COMBINED A */:    sub_b.a = px->combined_color.a; break;
    case 1 /* TEXEL0 A */:      sub_b.a = px->texel0_color.a; break;
    case 2 /* TEXEL1 A */:      sub_b.a = px->texel1_color.a; break;
    case 3 /* PRIMITIVE A */:   sub_b.a = rdp.prim_color.a; break;
    case 4 /* SHADE A */:       sub_b.a = px->shade_color.a; break;
    case 5 /* ENVIRONMENT A */: sub_b.a = rdp.env_color.a; break;
    case 6 /* 1 */:             sub_b.a = 255; break;
    default /* 0 */:            sub_b.a = 0; break;
    }
    switch (combine_mode.mul_A_1) {
    case 0 /* LOD FRACTION */:  mul.a = px->lod_frac; break;
    case 1 /* TEXEL0 A */:      mul.a = px->texel0_color.a; break;
    case 2 /* TEXEL1 A */:      mul.a = px->texel1_color.a; break;
    case 3 /* PRIMITIVE A */:   mul.a = rdp.prim_color.a; break;
    case 4 /* SHADE A */:       mul.a = px->shade_color.a; break;
    case 5 /* ENVIRONMENT A */: mul.a = rdp.env_color.a; break;
    case 6 /* PRIM LOD FRAC */: mul.a = px->prim_lod_frac; break;
    default /* 0 */:            mul.a = 0; break;
    }
    switch (combine_mode.add_A_1) {
    case 0 /* COMBINED A */:    add.a = px->combined_color.a; break;
    case 1 /* TEXEL0 A */:      add.a = px->texel0_color.a; break;
    case 2 /* TEXEL1 A */:      add.a = px->texel1_color.a; break;
    case 3 /* PRIMITIVE A */:   add.a = rdp.prim_color.a; break;
    case 4 /* SHADE A */:       add.a = px->shade_color.a; break;
    case 5 /* ENVIRONMENT A */: add.a = rdp.env_color.a; break;
    case 6 /* 1 */:             add.a = 255; break;
    default /* 0 */:            add.a = 0; break;
    }

    px->combined_color.a = (((sub_a.a - sub_b.a) * mul.a) >> 8) + add.a;
}

/**
 * The blender BL takes the combined pixels as input and blends them
 * into the pixels of the frame buffer. Translucent colors are implemented
 * by blending with the color pixels in the frame buffer.
 * In addition, BL performs some of the anti-aliasing of polygon edges
 * by conditionally blending colors based on depth ranges.
 * In 2-cycle mode, fog processing can also be performed.
 *
 * The blender operates according to the following formula:
 *
 *   color = (a * p + b * m) / (a + b)
 *
 * Where a, b, m, p can be configured to different input sources.
 * In two cycle mode, the formula is applied twice; and the result of the
 * first cycle can be injected as input of the second cycle.
 */
static void pipeline_bl(pixel_t *px) {
    color_t p = { 0 };
    color_t m = { 0 };
    u8 a = 0;
    u8 b = 0;

    switch (other_modes.b_m1a_0) {
    case BLENDER_SRC_SEL_IN_COLOR: p = px->combined_color; break; // blended color in second cycle
    case BLENDER_SRC_SEL_MEM_COLOR: p = px->mem_color; break;
    case BLENDER_SRC_SEL_BLEND_COLOR: p = rdp.blend_color; break;
    case BLENDER_SRC_SEL_FOG_COLOR: p = rdp.fog_color; break;
    }
    switch (other_modes.b_m1b_0) {
    case BLENDER_SRC_SEL_IN_ALPHA: a = px->combined_color.a; break;
    case BLENDER_SRC_SEL_FOG_ALPHA: a = rdp.fog_color.a; break;
    case BLENDER_SRC_SEL_SHADE_ALPHA: a = px->shade_color.a; break;
    case BLENDER_SRC_SEL_0: a = 0; break;
    }
    switch (other_modes.b_m2a_0) {
    case BLENDER_SRC_SEL_IN_COLOR: m = px->combined_color; break;  // blended color in second cycle
    case BLENDER_SRC_SEL_MEM_COLOR: m = px->mem_color; break;
    case BLENDER_SRC_SEL_BLEND_COLOR: m = rdp.blend_color; break;
    case BLENDER_SRC_SEL_FOG_COLOR: m = rdp.fog_color; break;
    }
    switch (other_modes.b_m2b_0) {
    case BLENDER_SRC_SEL_1_AMUX: b = 255 - a; break;
    case BLENDER_SRC_SEL_MEM_ALPHA: b = px->mem_color.a; break;
    case BLENDER_SRC_SEL_1: b = 255; break;
    case BLENDER_SRC_SEL_0: b = 0; break;
    }

    if ((a + b) == 0) {
        debugger::warn(Debugger::RDP, "pipeline_bl: divide by 0");
        return;
    }

    px->blended_color.r = ((p.r * a + m.r * b) / (a + b));
    px->blended_color.g = ((p.g * a + m.g * b) / (a + b));
    px->blended_color.b = ((p.b * a + m.b * b) / (a + b));
    px->blended_color.a = ((p.a * a + m.a * b) / (a + b));

    if (other_modes.alpha_compare_en) {
        // dither_alpha_en
    }
}

/**
 * Read the pixel color saved in the current color image.
 * The color is read from px->mem_color_addr and saved to
 * px->mem_color.
 */
static void pipeline_mi_load(pixel_t *px) {
    u8 *addr = &state.dram[px->mem_color_addr];
    switch (color_image.type) {
    case IMAGE_DATA_FORMAT_I_8:
        debugger::halt("pipeline_mi_load: unsupported image data type I_8");
        break;
    /* R[15:11],G[10:6],G[5:1],A[0] =>
     * R {[15:11],[15:13]}
     * G {[10:6],[10:8]}
     * B {[5:1],[5:3]}
     * A 255*[0] */
    case IMAGE_DATA_FORMAT_RGBA_5_5_5_1: {
        u16 rgba = __builtin_bswap16(*(u16 *)addr);
        u8 r = (rgba >> 11) & 0x1fu;
        u8 g = (rgba >>  6) & 0x1fu;
        u8 b = (rgba >>  1) & 0x1fu;
        px->mem_color.r = (r << 3) | (r >> 2);
        px->mem_color.g = (g << 3) | (g >> 2);
        px->mem_color.b = (b << 3) | (b >> 2);
        px->mem_color.a = (rgba & 1u) ? 255 : 0;
        break;
    }
    /* R[31:24],G[23:16],G[15:8],A[7:0] =>
     * R [31:24]
     * G [23:16]
     * B [15:8]
     * A [7:0] */
    case IMAGE_DATA_FORMAT_RGBA_8_8_8_8: {
        u32 rgba = __builtin_bswap32(*(u32 *)addr);
        px->mem_color.r = (rgba >> 24) & 0xffu;
        px->mem_color.g = (rgba >> 16) & 0xffu;
        px->mem_color.b = (rgba >>  8) & 0xffu;
        px->mem_color.a = (rgba >>  0) & 0xffu;
        break;
    }
    default:
        debugger::halt("pipeline_mi_store: unexpected image data type");
        break;
    }
}

/**
 * Read the pixel depth saved in the current zbuffer image.
 * The depth is read from px->mem_z_addr and saved to
 * px->mem_z, px->mem_deltaz.
 */
static void pipeline_mi_load_z(pixel_t *px) {
    /* The stepped Z is saved in the zbuffer as a 14bit floating point
     * number with 11bit mantissa and 3bit exponent. */
    u16 mem_z = __builtin_bswap16(*(u16 *)&state.dram[px->mem_z_addr]);
    u16 mem_z_01 = state.loadHiddenBits(px->mem_z_addr);
    /* Convert 11 bit mantissa and 3 bit exponent to U15.3 number */
    struct {
        int shift;
        u32 add;
    } z_format[8] = {
        6, 0x00000lu,
        5, 0x20000lu,
        4, 0x30000lu,
        3, 0x38000lu,
        2, 0x3c000lu,
        1, 0x3e000lu,
        0, 0x3f000lu,
        0, 0x3f800lu,
    };
    u16 mantissa = (mem_z >> 2) & 0x7ffu;
    u16 exponent = (mem_z >> 13) & 0x7u;
    px->mem_z = ((u32)mantissa << z_format[exponent].shift) |
                z_format[exponent].add;

    /* The DeltaZ is also encoded into 4 bit integer for storage into
     * the Z-buffer using the following equation:
     * mem_deltaz = log2( px->deltaz ) */
    u16 mem_deltaz = (mem_z & 0x3u) << 2 | mem_z_01;
    px->mem_deltaz = (i16)(u16)(1u << mem_deltaz);
}

/**
 * Write the blended color to the current color image.
 * The color is read from px->blended_color and written
 * to px->mem_color_addr.
 */
static void pipeline_mi_store(pixel_t *px) {
    u8 *addr = &state.dram[px->mem_color_addr];
    switch (color_image.type) {
    case IMAGE_DATA_FORMAT_I_8:
        debugger::halt("pipeline_mi_store: unsupported image data type I_8");
        break;
    case IMAGE_DATA_FORMAT_RGBA_5_5_5_1: {
        u16 r = px->blended_color.r >> 3;
        u16 g = px->blended_color.g >> 3;
        u16 b = px->blended_color.b >> 3;
        u16 a = px->blended_color.a >> 7;
        u16 color = (r << 11) | (g << 6) | (b << 1) | a;
        *(u16 *)addr = __builtin_bswap16(color);
        break;
    }
    case IMAGE_DATA_FORMAT_RGBA_8_8_8_8: {
        u32 color =
            ((u32)px->blended_color.r << 24) |
            ((u32)px->blended_color.g << 16) |
            ((u32)px->blended_color.b << 8) |
            px->blended_color.a;
        *(u32 *)addr = __builtin_bswap32(color);
        break;
    }
    default:
        debugger::halt("pipeline_mi_store: unexpected image data type");
        break;
    }
}

/**
 * Write the pixel depth to the current zbuffer image.
 * The depth is read from px->z, px->deltaz and written
 * to px->mem_z_addr.
 */
static void pipeline_mi_store_z(pixel_t *px) {
    if (!other_modes.z_update_en)
        return;

    /* Convert U15.3 number into 11 bit mantissa and 3 bit exponent */
    struct {
        int exponent;
        int shift;
    } z_format[128] = {
        { 0, 6 }, { 0, 6 }, { 0, 6 }, { 0, 6 },
        { 0, 6 }, { 0, 6 }, { 0, 6 }, { 0, 6 },
        { 0, 6 }, { 0, 6 }, { 0, 6 }, { 0, 6 },
        { 0, 6 }, { 0, 6 }, { 0, 6 }, { 0, 6 },
        { 0, 6 }, { 0, 6 }, { 0, 6 }, { 0, 6 },
        { 0, 6 }, { 0, 6 }, { 0, 6 }, { 0, 6 },
        { 0, 6 }, { 0, 6 }, { 0, 6 }, { 0, 6 },
        { 0, 6 }, { 0, 6 }, { 0, 6 }, { 0, 6 },
        { 0, 6 }, { 0, 6 }, { 0, 6 }, { 0, 6 },
        { 0, 6 }, { 0, 6 }, { 0, 6 }, { 0, 6 },
        { 0, 6 }, { 0, 6 }, { 0, 6 }, { 0, 6 },
        { 0, 6 }, { 0, 6 }, { 0, 6 }, { 0, 6 },
        { 0, 6 }, { 0, 6 }, { 0, 6 }, { 0, 6 },
        { 0, 6 }, { 0, 6 }, { 0, 6 }, { 0, 6 },
        { 0, 6 }, { 0, 6 }, { 0, 6 }, { 0, 6 },
        { 0, 6 }, { 0, 6 }, { 0, 6 }, { 0, 6 },
        { 1, 5 }, { 1, 5 }, { 1, 5 }, { 1, 5 },
        { 1, 5 }, { 1, 5 }, { 1, 5 }, { 1, 5 },
        { 1, 5 }, { 1, 5 }, { 1, 5 }, { 1, 5 },
        { 1, 5 }, { 1, 5 }, { 1, 5 }, { 1, 5 },
        { 1, 5 }, { 1, 5 }, { 1, 5 }, { 1, 5 },
        { 1, 5 }, { 1, 5 }, { 1, 5 }, { 1, 5 },
        { 1, 5 }, { 1, 5 }, { 1, 5 }, { 1, 5 },
        { 1, 5 }, { 1, 5 }, { 1, 5 }, { 1, 5 },
        { 2, 4 }, { 2, 4 }, { 2, 4 }, { 2, 4 },
        { 2, 4 }, { 2, 4 }, { 2, 4 }, { 2, 4 },
        { 2, 4 }, { 2, 4 }, { 2, 4 }, { 2, 4 },
        { 2, 4 }, { 2, 4 }, { 2, 4 }, { 2, 4 },
        { 3, 3 }, { 3, 3 }, { 3, 3 }, { 3, 3 },
        { 3, 3 }, { 3, 3 }, { 3, 3 }, { 3, 3 },
        { 4, 2 }, { 4, 2 }, { 4, 2 }, { 4, 2 },
        { 5, 1 }, { 5, 1 }, { 6, 0 }, { 7, 0 },
    };

    u32 z;
    u16 deltaz;

    if (other_modes.z_source_sel) {
        z = prim_z;
        deltaz = prim_deltaz;
    } else {
        z = px->zbuffer_coefs.z;
        deltaz = px->zbuffer_coefs.deltaz + prim_deltaz;
    }

    u32 idx = z >> 11;
    u16 exponent = z_format[idx].exponent;
    u16 mantissa = (z >> z_format[idx].shift) & 0x7ffu;

    /* The DeltaZ is also encoded into 4 bit integer for storage into
     * the Z-buffer using the following equation:
     * mem_deltaz = log2( px->deltaz ) */
    u16 log2_deltaz = deltaz ?
        sizeof(unsigned) * CHAR_BIT - __builtin_clz(deltaz) : 0;

    u16 mem_z = exponent << 13 | mantissa << 2 | log2_deltaz >> 2;
    u16 mem_z_01 = log2_deltaz & 0x3u;

    *(u16 *)&state.dram[px->mem_z_addr] = __builtin_bswap16(mem_z);
    state.storeHiddenBits(px->mem_z_addr, mem_z_01);
}

/** Optionally compare and update a z memory value with a computed stepped
 * pixel z value. Returns true iff the pixel is nearer than the memory pixel. */
static bool z_compare(pixel_t *px) {
    bool compare = true;

    if (other_modes.z_compare_en) {
        pipeline_mi_load_z(px);

        u32 comp_z;
        u16 comp_deltaz;

        if (other_modes.z_source_sel) {
            comp_z = prim_z;
            comp_deltaz = prim_deltaz;
        } else {
            comp_z = px->zbuffer_coefs.z;
            comp_deltaz = px->zbuffer_coefs.deltaz + prim_deltaz;
        }

        u32 mem_z = px->mem_z;
        // u16 mem_deltaz = px->mem_deltaz;
        u32 near = comp_z >= ((u32)comp_deltaz << 3) ?
            comp_z - ((u32)comp_deltaz << 3) : 0;
        // u32 far = comp_z + ((u32)comp_deltaz << 3);
        bool behind  = mem_z < near;
        // bool infront = mem_z > far;

        compare &= !behind;
    }

    return compare;
}

/** @brief Fills the line with coordinates (xs,y), (xe, y) with the
 * fill color. Input coordinates are in 10.2 fixedpoint format. */
static void render_span(bool left, unsigned level, unsigned tile,
                        i32 y, i32 xs, i32 xe,
                        struct shade_coefs const *shade,
                        struct texture_coefs const *texture,
                        struct zbuffer_coefs const *zbuffer) {

    if (y < (i32)scissor.yh || y > (i32)scissor.yl || xe <= xs ||
        (scissor.skipOddLines && ((y >> 2) % 2)) ||
        (scissor.skipEvenLines && !((y >> 2) % 2)))
        return;

    // Clip x coordinate and convert to integer values
    // from fixed point 10.2 format.
    y = y >> 2;
    xs = std::max(xs >> 14, (i32)scissor.xh) >> 2;
    xe = std::min(xe >> 14, (i32)scissor.xl) >> 2;
    xs = std::max(xs, 0);
    xe = std::min(xe, (i32)color_image.width);

    unsigned offset =
        color_image.addr + ((xs + y * color_image.width) << (color_image.size - 1));
    unsigned length = (xe - xs) << (color_image.size - 1);

    if ((offset + length) > sizeof(state.dram)) {
        debugger::warn(Debugger::RDP,
            "(cycle1) render_span out-of-bounds, start:{}, length:{}",
            offset, length);
        debugger::halt("Cycle1Mode::render_span out-of-bounds");
        return;
    }

    unsigned px_size = 1 << (color_image.size - 1);
    pixel_t px = { 0 };
    px.mem_color_addr = offset;
    px.shade_color.a = 255;
    px.texel0_color.a = 255;
    px.lod_frac = 255;
    px.edge_coefs.y = y;
    px.mem_color_addr = left ? offset : offset + length - px_size;

    i32 shade_r = 0;
    i32 shade_g = 0;
    i32 shade_b = 0;
    i32 shade_a = 0;
    i32 z = 0;

    if (shade) {
        shade_r = shade->r;
        shade_g = shade->g;
        shade_b = shade->b;
        shade_a = shade->a;
    }
    if (texture) {
        px.texture_coefs.s = texture->s;
        px.texture_coefs.t = texture->t;
        px.texture_coefs.w = texture->w;
        px.tile = &tiles[tile];
    }
    if (zbuffer) {
        unsigned z_offset = z_image.addr + (xs + y * color_image.width) * 2;
        unsigned z_length = (xe - xs) * 2;

        if ((z_offset + z_length) > sizeof(state.dram)) {
            debugger::warn(Debugger::RDP,
                "(cycle1) render_span zbuffer out-of-bounds, start:{}, length:{}",
                offset, length);
            debugger::halt("Cycle1Mode::render_span zbuffer out-of-bounds");
            return;
        }

        px.mem_z_addr = left ? z_offset : z_offset + z_length - 2;
        z = zbuffer->z;
        px.zbuffer_coefs.deltaz =
           ((zbuffer->dzdx > 0 ? (u32)zbuffer->dzdx : (u32)-zbuffer->dzdx) +
            (zbuffer->dzdy > 0 ? (u32)zbuffer->dzdy : (u32)-zbuffer->dzdy)) >> 16;
    }

    if (left) {
        for (px.edge_coefs.x = xs; px.edge_coefs.x < xe; px.edge_coefs.x++) {
            if (shade) {
                px.shade_color.r = shade_r >> 16;
                px.shade_color.g = shade_g >> 16;
                px.shade_color.b = shade_b >> 16;
                px.shade_color.a = shade_a >> 16;
            }
            if (zbuffer) {
                /* Convert from S15.16 to U15.3
                 * TODO check clamp to 0 ? */
                px.zbuffer_coefs.z = z < 0 ? 0 : (u32)z >> 13;
            }

            if (z_compare(&px)) {
                if (texture) {
                    pipeline_tx(&px);
                    pipeline_tf(&px);
                }
                pipeline_cc(&px);
                pipeline_mi_load(&px);
                pipeline_bl(&px);
                pipeline_mi_store(&px);
                pipeline_mi_store_z(&px);
                print_pixel(&px);
            }

            px.mem_color_addr += px_size;
            if (shade) {
                shade_r += shade->drdx;
                shade_g += shade->dgdx;
                shade_b += shade->dbdx;
                shade_a += shade->dadx;
            }
            if (texture) {
                px.texture_coefs.s += texture->dsdx;
                px.texture_coefs.t += texture->dtdx;
                px.texture_coefs.w += texture->dwdx;
            }
            if (zbuffer) {
                px.mem_z_addr += 2;
                z += zbuffer->dzdx;
            }
        }
    } else {
        for (px.edge_coefs.x = xe; px.edge_coefs.x > xs; px.edge_coefs.x--) {
            if (shade) {
                px.shade_color.r = shade_r >> 16;
                px.shade_color.g = shade_g >> 16;
                px.shade_color.b = shade_b >> 16;
                px.shade_color.a = shade_a >> 16;
            }
            if (zbuffer) {
                /* Convert from S15.16 to U15.3
                 * TODO check clamp to 0 ? */
                px.zbuffer_coefs.z = z < 0 ? 0 : (u32)z >> 13;
            }

            if (z_compare(&px)) {
                if (texture) {
                    pipeline_tx(&px);
                    pipeline_tf(&px);
                }
                pipeline_cc(&px);
                pipeline_mi_load(&px);
                pipeline_bl(&px);
                pipeline_mi_store(&px);
                pipeline_mi_store_z(&px);
                print_pixel(&px);
            }

            px.mem_color_addr -= px_size;
            if (shade) {
                shade_r -= shade->drdx;
                shade_g -= shade->dgdx;
                shade_b -= shade->dbdx;
                shade_a -= shade->dadx;
            }
            if (texture) {
                px.texture_coefs.s -= texture->dsdx;
                px.texture_coefs.t -= texture->dtdx;
                px.texture_coefs.w -= texture->dwdx;
            }
            if (zbuffer) {
                px.mem_z_addr -= 2;
                z -= zbuffer->dzdx;
            }
        }
    }
}

}; /* Cycle1Mode */

static i32 read_s15_16(u64 val, u64 frac, unsigned shift) {
    u32 top = ((val >> shift) << 16) & 0xffff0000lu;
    u32 bottom = (frac >> shift) & 0xffffu;
    return (i32)(top | bottom);
}

static void read_edge_coefs(u64 cmd, u64 const *params, struct edge_coefs *edge) {
    u32 yl      = (cmd >> 32) & 0x3fffu;
    if (yl & (UINT32_C(1) << 13))
        yl |= UINT32_C(0xffffc000);
    u32 ym      = (cmd >> 16) & 0x3fffu;
    if (ym & (UINT32_C(1) << 13))
        ym |= UINT32_C(0xffffc000);
    u32 yh      = (cmd >>  0) & 0x3fffu;
    if (yh & (UINT32_C(1) << 13))
        yh |= UINT32_C(0xffffc000);
    edge->yl    = yl;
    edge->ym    = ym;
    edge->yh    = yh;
    edge->xl    = (u32)(params[0] >> 32);
    edge->dxldy = (u32)(params[0] >>  0);
    edge->xh    = (u32)(params[1] >> 32);
    edge->dxhdy = (u32)(params[1] >>  0);
    edge->xm    = (u32)(params[2] >> 32);
    edge->dxmdy = (u32)(params[2] >>  0);
}

static void read_shade_coefs(u64 const *params, struct shade_coefs *shade) {
    shade->r    = read_s15_16(params[0], params[2], 48);
    shade->g    = read_s15_16(params[0], params[2], 32);
    shade->b    = read_s15_16(params[0], params[2], 16);
    shade->a    = read_s15_16(params[0], params[2],  0);
    shade->drdx = read_s15_16(params[1], params[3], 48);
    shade->dgdx = read_s15_16(params[1], params[3], 32);
    shade->dbdx = read_s15_16(params[1], params[3], 16);
    shade->dadx = read_s15_16(params[1], params[3],  0);
    shade->drde = read_s15_16(params[4], params[6], 48);
    shade->dgde = read_s15_16(params[4], params[6], 32);
    shade->dbde = read_s15_16(params[4], params[6], 16);
    shade->dade = read_s15_16(params[4], params[6],  0);
    shade->drdy = read_s15_16(params[5], params[7], 48);
    shade->dgdy = read_s15_16(params[5], params[7], 32);
    shade->dbdy = read_s15_16(params[5], params[7], 16);
    shade->dady = read_s15_16(params[5], params[7],  0);
}

static void read_texture_coefs(u64 const *params, struct texture_coefs *texture) {
    texture->s    = read_s15_16(params[0], params[2], 48);
    texture->t    = read_s15_16(params[0], params[2], 32);
    texture->w    = read_s15_16(params[0], params[2], 16);
    texture->dsdx = read_s15_16(params[1], params[3], 48);
    texture->dtdx = read_s15_16(params[1], params[3], 32);
    texture->dwdx = read_s15_16(params[1], params[3], 16);
    texture->dsde = read_s15_16(params[4], params[6], 48);
    texture->dtde = read_s15_16(params[4], params[6], 32);
    texture->dwde = read_s15_16(params[4], params[6], 16);
    texture->dsdy = read_s15_16(params[5], params[7], 48);
    texture->dtdy = read_s15_16(params[5], params[7], 32);
    texture->dwdy = read_s15_16(params[5], params[7], 16);
}

static void read_zbuffer_coefs(u64 const *params, struct zbuffer_coefs *zbuffer) {
    zbuffer->z    = (u32)(params[0] >> 32);
    zbuffer->dzdx = (u32)(params[0] >>  0);
    zbuffer->dzde = (u32)(params[1] >> 32);
    zbuffer->dzdy = (u32)(params[1] >>  0);
}

static void print_edge_coefs(struct edge_coefs const *edge) {
    debugger::debug(Debugger::RDP, "  yl: {}", s29_2_to_float(edge->yl));
    debugger::debug(Debugger::RDP, "  ym: {}", s29_2_to_float(edge->ym));
    debugger::debug(Debugger::RDP, "  yh: {}", s29_2_to_float(edge->yh));
    debugger::debug(Debugger::RDP, "  xl: {}", s15_16_to_float(edge->xl));
    debugger::debug(Debugger::RDP, "  xm: {}", s15_16_to_float(edge->xm));
    debugger::debug(Debugger::RDP, "  xh: {}", s15_16_to_float(edge->xh));
    debugger::debug(Debugger::RDP, "  dxldy: {}", s15_16_to_float(edge->dxldy));
    debugger::debug(Debugger::RDP, "  dxmdy: {}", s15_16_to_float(edge->dxmdy));
    debugger::debug(Debugger::RDP, "  dxhdy: {}", s15_16_to_float(edge->dxhdy));
}

static void print_shade_coefs(struct shade_coefs const *shade) {
    debugger::debug(Debugger::RDP, "  r: {}", s15_16_to_float(shade->r));
    debugger::debug(Debugger::RDP, "  g: {}", s15_16_to_float(shade->g));
    debugger::debug(Debugger::RDP, "  b: {}", s15_16_to_float(shade->b));
    debugger::debug(Debugger::RDP, "  a: {}", s15_16_to_float(shade->a));
    debugger::debug(Debugger::RDP, "  drdx: {}", s15_16_to_float(shade->drdx));
    debugger::debug(Debugger::RDP, "  dgdx: {}", s15_16_to_float(shade->dgdx));
    debugger::debug(Debugger::RDP, "  dbdx: {}", s15_16_to_float(shade->dbdx));
    debugger::debug(Debugger::RDP, "  dadx: {}", s15_16_to_float(shade->dadx));
    debugger::debug(Debugger::RDP, "  drde: {}", s15_16_to_float(shade->drde));
    debugger::debug(Debugger::RDP, "  dgde: {}", s15_16_to_float(shade->dgde));
    debugger::debug(Debugger::RDP, "  dbde: {}", s15_16_to_float(shade->dbde));
    debugger::debug(Debugger::RDP, "  dade: {}", s15_16_to_float(shade->dade));
    debugger::debug(Debugger::RDP, "  drdy: {}", s15_16_to_float(shade->drdy));
    debugger::debug(Debugger::RDP, "  dgdy: {}", s15_16_to_float(shade->dgdy));
    debugger::debug(Debugger::RDP, "  dbdy: {}", s15_16_to_float(shade->dbdy));
    debugger::debug(Debugger::RDP, "  dady: {}", s15_16_to_float(shade->dady));
}

static void print_texture_coefs(struct texture_coefs const *texture) {
    debugger::debug(Debugger::RDP, "  s: {}", s10_21_to_float(texture->s));
    debugger::debug(Debugger::RDP, "  t: {}", s10_21_to_float(texture->t));
    debugger::debug(Debugger::RDP, "  w: {}", s10_21_to_float(texture->w));
    debugger::debug(Debugger::RDP, "  dsdx: {}", s10_21_to_float(texture->dsdx));
    debugger::debug(Debugger::RDP, "  dtdx: {}", s10_21_to_float(texture->dtdx));
    debugger::debug(Debugger::RDP, "  dwdx: {}", s10_21_to_float(texture->dwdx));
    debugger::debug(Debugger::RDP, "  dsde: {}", s10_21_to_float(texture->dsde));
    debugger::debug(Debugger::RDP, "  dtde: {}", s10_21_to_float(texture->dtde));
    debugger::debug(Debugger::RDP, "  dwde: {}", s10_21_to_float(texture->dwde));
    debugger::debug(Debugger::RDP, "  dsdy: {}", s10_21_to_float(texture->dsdy));
    debugger::debug(Debugger::RDP, "  dtdy: {}", s10_21_to_float(texture->dtdy));
    debugger::debug(Debugger::RDP, "  dwdy: {}", s10_21_to_float(texture->dwdy));
}

static void print_zbuffer_coefs(struct zbuffer_coefs const *zbuffer) {
    debugger::debug(Debugger::RDP, "  z: {}", s15_16_to_float(zbuffer->z));
    debugger::debug(Debugger::RDP, "  dzdx: {}", s15_16_to_float(zbuffer->dzdx));
    debugger::debug(Debugger::RDP, "  dzde: {}", s15_16_to_float(zbuffer->dzde));
    debugger::debug(Debugger::RDP, "  dzdy: {}", s15_16_to_float(zbuffer->dzdy));
}

static void render_triangle(u64 command, u64 const *params,
                            bool has_shade, bool has_texture, bool has_zbuffer) {

    bool left           = ((command >> 55) & 0x1u);
    unsigned level      = (command >> 51) & 0x7u;
    unsigned tile       = (command >> 48) & 0x7u;

    struct edge_coefs edge;
    struct shade_coefs shade;
    struct texture_coefs texture;
    struct zbuffer_coefs zbuffer;

    debugger::debug(Debugger::RDP, "  left: {}", left);
    debugger::debug(Debugger::RDP, "  level: {}", level);
    debugger::debug(Debugger::RDP, "  tile: {}", tile);

    read_edge_coefs(command, params, &edge);
    print_edge_coefs(&edge);
    params += 3;

    if (has_shade) {
        read_shade_coefs(params, &shade);
        print_shade_coefs(&shade);
        params += 8;
    }
    if (has_texture) {
        read_texture_coefs(params, &texture);
        print_texture_coefs(&texture);
        params += 8;
    }
    if (has_zbuffer) {
        read_zbuffer_coefs(params, &zbuffer);
        print_zbuffer_coefs(&zbuffer);
    }

    i32 y, xs, xe, dxsdy, dxedy;
    if (left) {
        xs = edge.xh; dxsdy = edge.dxhdy;
        xe = edge.xm; dxedy = edge.dxmdy;
    } else {
        xs = edge.xm; dxsdy = edge.dxmdy;
        xe = edge.xh; dxedy = edge.dxhdy;
    }

    for (y = edge.yh & ~3; y < edge.ym; y+=4) {
        Cycle1Mode::render_span(left, level, tile, y, xs, xe,
            has_shade ? &shade : NULL,
            has_texture ? &texture : NULL,
            has_zbuffer ? &zbuffer : NULL);

        xs += dxsdy;
        xe += dxedy;

        if (has_shade) {
            shade.r += shade.drde;
            shade.g += shade.dgde;
            shade.b += shade.dbde;
            shade.a += shade.dade;
        }
        if (has_texture) {
            texture.s += texture.dsde;
            texture.t += texture.dtde;
            texture.w += texture.dwde;
        }
        if (has_zbuffer) {
            zbuffer.z += zbuffer.dzde;
        }
    }

    if (left) {
        xe = edge.xl;
        dxedy = edge.dxldy;
    } else {
        xs = edge.xl;
        dxsdy = edge.dxldy;
    }

    for (           ; y < edge.yl; y+=4) {
        Cycle1Mode::render_span(left, level, tile, y, xs, xe,
            has_shade ? &shade : NULL,
            has_texture ? &texture : NULL,
            has_zbuffer ? &zbuffer : NULL);

        xs += dxsdy;
        xe += dxedy;

        if (has_shade) {
            shade.r += shade.drde;
            shade.g += shade.dgde;
            shade.b += shade.dbde;
            shade.a += shade.dade;
        }
        if (has_texture) {
            texture.s += texture.dsde;
            texture.t += texture.dtde;
            texture.w += texture.dwde;
        }
        if (has_zbuffer) {
            zbuffer.z += zbuffer.dzde;
        }
    }
}

void nonShadedTriangle(u64 command, u64 const *params) {
    render_triangle(command, params, false, false, false);
}

void shadeTriangle(u64 command, u64 const *params) {
    render_triangle(command, params, true, false, false);
}

void textureTriangle(u64 command, u64 const *params) {
    render_triangle(command, params, false, true, false);
}

void shadeTextureTriangle(u64 command, u64 const *params) {
    render_triangle(command, params, true, true, false);
}

void nonShadedZbuffTriangle(u64 command, u64 const *params) {
    render_triangle(command, params, false, false, true);
}

void shadeZbuffTriangle(u64 command, u64 const *params) {
    render_triangle(command, params, true, false, true);
}

void textureZbuffTriangle(u64 command, u64 const *params) {
    render_triangle(command, params, false, true, true);
}

void shadeTextureZbuffTriangle(u64 command, u64 const *params) {
    render_triangle(command, params, true, true, true);
}

void textureRectangle(u64 command, u64 const *params) {
    /* Input coordinates are in the 10.2 fixed point format. */
    unsigned xl = (command >> 44) & 0xfffu;
    unsigned yl = (command >> 32) & 0xfffu;
    unsigned tile = (command >> 24) & 0x7u;
    unsigned xh = (command >> 12) & 0xfffu;
    unsigned yh = (command >>  0) & 0xfffu;

    /* Texture coordinates are in signed 5.10 fixed point format. */
    i32 s    = (i16)(u16)((params[0] >> 48) & 0xffffu);
    i32 t    = (i16)(u16)((params[0] >> 32) & 0xffffu);
    i32 dsdx = (i16)(u16)((params[0] >> 16) & 0xffffu);
    i32 dtdy = (i16)(u16)((params[0] >>  0) & 0xffffu);

    debugger::debug(Debugger::RDP, "  xl: {}", (float)xl / 4.);
    debugger::debug(Debugger::RDP, "  yl: {}", (float)yl / 4.);
    debugger::debug(Debugger::RDP, "  tile: {}", tile);
    debugger::debug(Debugger::RDP, "  xh: {}", (float)xh / 4.);
    debugger::debug(Debugger::RDP, "  yh: {}", (float)yh / 4.);
    debugger::debug(Debugger::RDP, "  s: {}", (float)s / 32.);
    debugger::debug(Debugger::RDP, "  t: {}", (float)t / 32.);
    debugger::debug(Debugger::RDP, "  dsdx: {}", (float)dsdx / 1024.);
    debugger::debug(Debugger::RDP, "  dtdy: {}", (float)dtdy / 1024.);

    /* Convert texture coefficients from s10.5 or s5.10 to s10.21 */
    struct texture_coefs texture = {
        s << 16,    t << 16,    0,
        dsdx << 11, 0,          0,
        0,          0,          0,
        0,          dtdy << 11, 0,
    };

    /* Convert edge coefficients from 10.2 to s15.16 */
    xh <<= 14;
    xl <<= 14;

    for (unsigned y = yh; y < yl; y += 4) {
        Cycle1Mode::render_span(true, 0, tile, y, xh, xl, NULL, &texture, NULL);
        texture.t += texture.dtdy;
    }
}

void textureRectangleFlip(u64 command, u64 const *params) {
    /* Input coordinates are in the 10.2 fixed point format. */
    unsigned xl = (command >> 44) & 0xfffu;
    unsigned yl = (command >> 32) & 0xfffu;
    unsigned tile = (command >> 24) & 0x7u;
    unsigned xh = (command >> 12) & 0xfffu;
    unsigned yh = (command >>  0) & 0xfffu;

    /* Texture coordinates are in the signed 5.10 fixed point format. */
    i32 s    = (i16)(u16)((params[0] >> 48) & 0xffffu);
    i32 t    = (i16)(u16)((params[0] >> 32) & 0xffffu);
    i32 dsdx = (i16)(u16)((params[0] >> 16) & 0xffffu);
    i32 dtdy = (i16)(u16)((params[0] >>  0) & 0xffffu);

    debugger::debug(Debugger::RDP, "  xl: {}", (float)xl / 4.);
    debugger::debug(Debugger::RDP, "  yl: {}", (float)yl / 4.);
    debugger::debug(Debugger::RDP, "  tile: {}", tile);
    debugger::debug(Debugger::RDP, "  xh: {}", (float)xh / 4.);
    debugger::debug(Debugger::RDP, "  yh: {}", (float)yh / 4.);
    debugger::debug(Debugger::RDP, "  s: {}", (float)s / 32.);
    debugger::debug(Debugger::RDP, "  t: {}", (float)t / 32.);
    debugger::debug(Debugger::RDP, "  dsdx: {}", (float)dsdx / 1024.);
    debugger::debug(Debugger::RDP, "  dtdy: {}", (float)dtdy / 1024.);

    struct texture_coefs texture = {
        s << 16,    t << 16,    0,
        0,          dtdy << 11, 0,
        0,          0,          0,
        dsdx << 11, 0, 0,
    };

    /* Convert edge coefficients from 10.2 to s15.16 */
    xh <<= 14;
    xl <<= 14;

    for (unsigned y = yh; y < yl; y += 4) {
        Cycle1Mode::render_span(true, 0, tile, y, xh, xl, NULL, &texture, NULL);
        texture.t += texture.dtdy;
        texture.s += texture.dsdy;
    }
}

void syncLoad(u64 command, u64 const *params) {
}

void syncPipe(u64 command, u64 const *params) {
}

void syncTile(u64 command, u64 const *params) {
}

void syncFull(u64 command, u64 const *params) {
    set_MI_INTR_REG(MI_INTR_DP);
}

void setKeyGB(u64 command, u64 const *params) {
    debugger::halt("set_key_gb");
}

void setKeyR(u64 command, u64 const *params) {
    debugger::halt("set_key_r");
}

void setConvert(u64 command, u64 const *params) {
    debugger::halt("set_convert");
}

void setScissor(u64 command, u64 const *params) {
    scissor.xh = (command >> 44) & 0xfffu;
    scissor.yh = (command >> 32) & 0xfffu;
    scissor.xl = (command >> 12) & 0xfffu;
    scissor.yl = (command >>  0) & 0xfffu;

    debugger::debug(Debugger::RDP, "  xl: {}", (float)scissor.xl / 4.);
    debugger::debug(Debugger::RDP, "  yl: {}", (float)scissor.yl / 4.);
    debugger::debug(Debugger::RDP, "  xh: {}", (float)scissor.xh / 4.);
    debugger::debug(Debugger::RDP, "  yh: {}", (float)scissor.yh / 4.);

    bool scissorField = (command & (1lu << 25)) != 0;
    bool oddEven = (command & (1lu << 25)) != 0;

    scissor.skipOddLines = scissorField && !oddEven;
    scissor.skipEvenLines = scissorField && oddEven;

    if (scissor.xh > scissor.xl ||
        scissor.yh > scissor.yl) {
        debugger::warn(Debugger::RDP, "invalid scissor coordinates");
        debugger::halt("set_scissor: invalid coordinates");
    }
}

void setPrimDepth(u64 command, u64 const *params) {
    prim_z      = (u32)((command >> 16) & 0xffffu) << 3;
    prim_deltaz = ((command >>  0) & 0xffffu);

    debugger::debug(Debugger::RDP, "  z: {}", (float)prim_z / 8.);
    debugger::debug(Debugger::RDP, "  deltaz: {}", (i16)prim_deltaz);
}

void setOtherModes(u64 command, u64 const *params) {
    other_modes.atomic_prim = (command >> 55) & 0x1u;
    other_modes.cycle_type = (enum cycle_type)((command >> 52) & 0x3u);
    other_modes.persp_tex_en = (command >> 51) & 0x1u;
    other_modes.detail_tex_en = (command >> 50) & 0x1u;
    other_modes.sharpen_tex_en = (command >> 49) & 0x1u;
    other_modes.tex_lod_en = (command >> 48) & 0x1u;
    other_modes.tlut_en = (command >> 47) & 0x1u;
    other_modes.tlut_type = (enum tlut_type)((command >> 46) & 0x1u);
    other_modes.sample_type = (enum sample_type)((command >> 45) & 0x1u);
    other_modes.mid_texel = (command >> 44) & 0x1u;
    other_modes.bi_lerp_0 = (command >> 43) & 0x1u;
    other_modes.bi_lerp_1 = (command >> 42) & 0x1u;
    other_modes.convert_one = (command >> 41) & 0x1u;
    other_modes.key_en = (command >> 40) & 0x1u;
    other_modes.rgb_dither_sel = (enum rgb_dither_sel)((command >> 38) & 0x3u);
    other_modes.alpha_dither_sel = (enum alpha_dither_sel)((command >> 36) & 0x3u);
    other_modes.b_m1a_0 = (enum blender_src_sel)((command >> 30) & 0x3u);
    other_modes.b_m1a_1 = (enum blender_src_sel)((command >> 28) & 0x3u);
    other_modes.b_m1b_0 = (enum blender_src_sel)((command >> 26) & 0x3u);
    other_modes.b_m1b_1 = (enum blender_src_sel)((command >> 24) & 0x3u);
    other_modes.b_m2a_0 = (enum blender_src_sel)((command >> 22) & 0x3u);
    other_modes.b_m2a_1 = (enum blender_src_sel)((command >> 20) & 0x3u);
    other_modes.b_m2b_0 = (enum blender_src_sel)((command >> 18) & 0x3u);
    other_modes.b_m2b_1 = (enum blender_src_sel)((command >> 16) & 0x3u);
    other_modes.force_blend = (command >> 14) & 0x1u;
    other_modes.alpha_cvg_select = (command >> 13) & 0x1u;
    other_modes.cvg_times_alpha = (command >> 12) & 0x1u;
    other_modes.z_mode = (enum z_mode)((command >> 10) & 0x3u);
    other_modes.cvg_dest = (enum cvg_dest)((command >> 8) & 0x3u);
    other_modes.color_on_cvg = (command >> 7) & 0x1u;
    other_modes.image_read_en = (command >> 6) & 0x1u;
    other_modes.z_update_en = (command >> 5) & 0x1u;
    other_modes.z_compare_en = (command >> 4) & 0x1u;
    other_modes.antialias_en = (command >> 3) & 0x1u;
    other_modes.z_source_sel = (enum z_source_sel)((command >> 2) & 0x1u);
    other_modes.dither_alpha_en = (command >> 1) & 0x1u;
    other_modes.alpha_compare_en = (command >> 0) & 0x1u;

    debugger::debug(Debugger::RDP, "  atomic_prim: {}", other_modes.atomic_prim);
    debugger::info(Debugger::RDP, "  cycle_type: {}", other_modes.cycle_type);
    debugger::debug(Debugger::RDP, "  persp_tex_en: {}", other_modes.persp_tex_en);
    debugger::debug(Debugger::RDP, "  detail_tex_en: {}", other_modes.detail_tex_en);
    debugger::debug(Debugger::RDP, "  sharpen_tex_en: {}", other_modes.sharpen_tex_en);
    debugger::debug(Debugger::RDP, "  tex_lod_en: {}", other_modes.tex_lod_en);
    debugger::debug(Debugger::RDP, "  tlut_en: {}", other_modes.tlut_en);
    debugger::debug(Debugger::RDP, "  tlut_type: {}", other_modes.tlut_type);
    debugger::debug(Debugger::RDP, "  sample_type: {}", other_modes.sample_type);
    debugger::debug(Debugger::RDP, "  mid_texel: {}", other_modes.mid_texel);
    debugger::debug(Debugger::RDP, "  bi_lerp_0: {}", other_modes.bi_lerp_0);
    debugger::debug(Debugger::RDP, "  bi_lerp_1: {}", other_modes.bi_lerp_1);
    debugger::debug(Debugger::RDP, "  convert_one: {}", other_modes.convert_one);
    debugger::debug(Debugger::RDP, "  key_en: {}", other_modes.key_en);
    debugger::debug(Debugger::RDP, "  rgb_dither_sel: {}", other_modes.rgb_dither_sel);
    debugger::debug(Debugger::RDP, "  alpha_dither_sel: {}", other_modes.alpha_dither_sel);
    debugger::debug(Debugger::RDP, "  b_m1a_0: {}", other_modes.b_m1a_0);
    debugger::debug(Debugger::RDP, "  b_m1a_1: {}", other_modes.b_m1a_1);
    debugger::debug(Debugger::RDP, "  b_m1b_0: {}", other_modes.b_m1b_0);
    debugger::debug(Debugger::RDP, "  b_m1b_1: {}", other_modes.b_m1b_1);
    debugger::debug(Debugger::RDP, "  b_m2a_0: {}", other_modes.b_m2a_0);
    debugger::debug(Debugger::RDP, "  b_m2a_1: {}", other_modes.b_m2a_1);
    debugger::debug(Debugger::RDP, "  b_m2b_0: {}", other_modes.b_m2b_0);
    debugger::debug(Debugger::RDP, "  b_m2b_1: {}", other_modes.b_m2b_1);
    debugger::debug(Debugger::RDP, "  force_blend: {}", other_modes.force_blend);
    debugger::debug(Debugger::RDP, "  alpha_cvg_select: {}", other_modes.alpha_cvg_select);
    debugger::debug(Debugger::RDP, "  cvg_times_alpha: {}", other_modes.cvg_times_alpha);
    debugger::debug(Debugger::RDP, "  z_mode: {}", other_modes.z_mode);
    debugger::debug(Debugger::RDP, "  cvg_dest: {}", other_modes.cvg_dest);
    debugger::debug(Debugger::RDP, "  color_on_cvg: {}", other_modes.color_on_cvg);
    debugger::debug(Debugger::RDP, "  image_read_en: {}", other_modes.image_read_en);
    debugger::debug(Debugger::RDP, "  z_update_en: {}", other_modes.z_update_en);
    debugger::debug(Debugger::RDP, "  z_compare_en: {}", other_modes.z_compare_en);
    debugger::debug(Debugger::RDP, "  antialias_en: {}", other_modes.antialias_en);
    debugger::debug(Debugger::RDP, "  z_source_sel: {}", other_modes.z_source_sel);
    debugger::debug(Debugger::RDP, "  dither_alpha_en: {}", other_modes.dither_alpha_en);
    debugger::debug(Debugger::RDP, "  alpha_compare_en: {}", other_modes.alpha_compare_en);

    if (other_modes.cycle_type == CYCLE_TYPE_COPY)
        other_modes.sample_type = SAMPLE_TYPE_4X1;
}

void loadTlut(u64 command, u64 const *params) {
    unsigned sl = (command >> 44) & 0xfffu;
    unsigned tl = (command >> 32) & 0xfffu;
    unsigned tile = (command >> 24) & 0x7u;
    unsigned sh = (command >> 12) & 0xfffu;
    unsigned th = (command >>  0) & 0xfffu;

    tiles[tile].sl = sl;
    tiles[tile].tl = tl;
    tiles[tile].sh = sh;
    tiles[tile].th = th;

    debugger::debug(Debugger::RDP, "  sl: {}", (float)tiles[tile].sl / 4.);
    debugger::debug(Debugger::RDP, "  tl: {}", (float)tiles[tile].tl / 4.);
    debugger::debug(Debugger::RDP, "  tile: {}", tile);
    debugger::debug(Debugger::RDP, "  sh: {}", (float)tiles[tile].sh / 4.);
    debugger::debug(Debugger::RDP, "  th: {}", (float)tiles[tile].th / 4.);

    if (texture_image.size != PIXEL_SIZE_16B) {
        debugger::halt("load_tlut: invalid pixel size");
        return;
    }

    /* sl, sh are in 10.2 fixpoint format. */
    sl >>= 2;
    sh >>= 2;

    if (sl >= 256 || sh >= 256 || sl > sh) {
        debugger::halt("load_tlut: out-of-bounds palette index");
        return;
    }

    unsigned line_size = (sh - sl) << 1;
    u8 *src = &state.dram[texture_image.addr];
    u8 *dst = &state.tmem[0x800 + (sl << 1)];

    memcpy(dst,         src, line_size);
    memcpy(dst + 0x200, src, line_size);
    memcpy(dst + 0x400, src, line_size);
    memcpy(dst + 0x600, src, line_size);
}

void setTileSize(u64 command, u64 const *params) {
    unsigned sl = (command >> 44) & 0xfffu;
    unsigned tl = (command >> 32) & 0xfffu;
    unsigned tile = (command >> 24) & 0x7u;
    unsigned sh = (command >> 12) & 0xfffu;
    unsigned th = (command >>  0) & 0xfffu;

    tiles[tile].sl = sl;
    tiles[tile].tl = tl;
    tiles[tile].sh = sh;
    tiles[tile].th = th;

    debugger::debug(Debugger::RDP, "  sl: {}", (float)sl / 4.);
    debugger::debug(Debugger::RDP, "  tl: {}", (float)tl / 4.);
    debugger::debug(Debugger::RDP, "  tile: {}", tile);
    debugger::debug(Debugger::RDP, "  sh: {}", (float)sh / 4.);
    debugger::debug(Debugger::RDP, "  th: {}", (float)th / 4.);
}

void loadBlock(u64 command, u64 const *params) {
    unsigned sl = (command >> 44) & 0xfffu;
    unsigned tl = (command >> 32) & 0xfffu;
    unsigned tile = (command >> 24) & 0x7u;
    unsigned sh = (command >> 12) & 0xfffu;
    unsigned dxt = (command >>  0) & 0xfffu;

    tiles[tile].sl = sl << 2;
    tiles[tile].tl = tl << 2;
    tiles[tile].sh = sh << 2;
    tiles[tile].th = tl << 2;

    debugger::debug(Debugger::RDP, "  sl: {}", sl);
    debugger::debug(Debugger::RDP, "  tl: {}", tl);
    debugger::debug(Debugger::RDP, "  tile: {}", tile);
    debugger::debug(Debugger::RDP, "  sh: {}", sh);
    debugger::debug(Debugger::RDP, "  dxt: {}", i32_fixpoint_to_float((u32)dxt, 11));

    unsigned src_size = texture_image.size;
    if (src_size == PIXEL_SIZE_4B) {
        debugger::halt("Invalid texture format for loadBlock");
    }

    unsigned src_size_shift = src_size - 1;
    unsigned line_size = (sh - sl) << src_size_shift;
    unsigned src_stride = texture_image.width << src_size_shift;
    unsigned dst_stride = tiles[tile].line << 3;

    line_size = (line_size + 7u) & ~7u; /* Rounded-up to 64bit boundary. */
    u8 *src = &state.dram[texture_image.addr + (tl * src_stride) + (sl << src_size_shift)];
    u8 *dst = &state.tmem[tiles[tile].tmem_addr << 3];

    u32 t = 0;
    u32 t_int = 0;
    for (unsigned i = 0; i < line_size; i += 8, src += 8, dst += 8) {
        memcpy(dst, src, 8);
        t += dxt;
        if ((t >> 11) != t_int) {
            t_int = (t >> 11);
            dst += dst_stride;
        }
    }
    // TODO buffer overflow checks
}

void loadTile(u64 command, u64 const *params) {
    unsigned sl = (command >> 44) & 0xfffu;
    unsigned tl = (command >> 32) & 0xfffu;
    unsigned tile = (command >> 24) & 0x7u;
    unsigned sh = (command >> 12) & 0xfffu;
    unsigned th = (command >>  0) & 0xfffu;

    tiles[tile].sl = sl;
    tiles[tile].tl = tl;
    tiles[tile].sh = sh;
    tiles[tile].th = th;

    debugger::debug(Debugger::RDP, "  sl: {}", (float)tiles[tile].sl / 4.);
    debugger::debug(Debugger::RDP, "  tl: {}", (float)tiles[tile].tl / 4.);
    debugger::debug(Debugger::RDP, "  tile: {}", tile);
    debugger::debug(Debugger::RDP, "  sh: {}", (float)tiles[tile].sh / 4.);
    debugger::debug(Debugger::RDP, "  th: {}", (float)tiles[tile].th / 4.);

    unsigned src_size = texture_image.size;
    unsigned dst_size = tiles[tile].size;

    if (src_size != dst_size) {
        debugger::halt("Incompatible texture formats");
    }
    if (src_size == PIXEL_SIZE_4B) {
        debugger::halt("Invalid texture format for loadTile");
    }

    /* sl, tl, sh, th are in 10.2 fixpoint format. */
    sl >>= 2; tl >>= 2;
    sh >>= 2; th >>= 2;

    unsigned src_size_shift = src_size - 1;
    unsigned line_size = (sh - sl) << src_size_shift;
    unsigned src_stride = texture_image.width << src_size_shift;
    unsigned dst_stride = tiles[tile].line << 3;
    line_size = (line_size + 7u) & ~7u; /* Rounded-up to 64bit boundary. */
    u8 *src = &state.dram[texture_image.addr + (tl * src_stride) + (sl << src_size_shift)];
    u8 *dst = &state.tmem[tiles[tile].tmem_addr << 3];

    for (unsigned y = tl; y <= th; y++, src += src_stride, dst += dst_stride) {
        memcpy(dst, src, line_size);
    }
    // TODO buffer overflow checks
}

void setTile(u64 command, u64 const *params) {
    unsigned tile = (command >> 24) & 0x7u;
    tiles[tile].format = (enum image_data_format)((command >> 53) & 0x7u);
    tiles[tile].size = (enum pixel_size)((command >> 51) & 0x3u);
    tiles[tile].line = (command >> 41) & 0x1ffu;
    tiles[tile].tmem_addr = (command >> 32) & 0x1ffu;
    tiles[tile].palette = (command >> 20) & 0xfu;
    tiles[tile].clamp_t = (command >> 19) & 0x1u;
    tiles[tile].mirror_t = (command >> 18) & 0x1u;
    tiles[tile].mask_t = (command >> 14) & 0xfu;
    tiles[tile].shift_t = (command >> 10) & 0xfu;
    tiles[tile].clamp_s = (command >> 9) & 0x1u;
    tiles[tile].mirror_s = (command >> 8) & 0x1u;
    tiles[tile].mask_s = (command >> 4) & 0xfu;
    tiles[tile].shift_s = (command >> 0) & 0xfu;

    debugger::debug(Debugger::RDP, "  format: {}", tiles[tile].format);
    debugger::debug(Debugger::RDP, "  size: {}", tiles[tile].size);
    debugger::debug(Debugger::RDP, "  line: {}", tiles[tile].line);
    debugger::debug(Debugger::RDP, "  tmem_addr: {:x}", tiles[tile].tmem_addr);
    debugger::debug(Debugger::RDP, "  tile: {}", tile);
    debugger::debug(Debugger::RDP, "  palette: {}", tiles[tile].palette);
    debugger::debug(Debugger::RDP, "  clamp_t: {}", tiles[tile].clamp_t);
    debugger::debug(Debugger::RDP, "  mirror_t: {}", tiles[tile].mirror_t);
    debugger::debug(Debugger::RDP, "  mask_t: {}", tiles[tile].mask_t);
    debugger::debug(Debugger::RDP, "  shift_t: {}", tiles[tile].shift_t);
    debugger::debug(Debugger::RDP, "  clamp_s: {}", tiles[tile].clamp_s);
    debugger::debug(Debugger::RDP, "  mirror_s: {}", tiles[tile].mirror_s);
    debugger::debug(Debugger::RDP, "  mask_s: {}", tiles[tile].mask_s);
    debugger::debug(Debugger::RDP, "  shift_s: {}", tiles[tile].shift_s);

    tiles[tile].type = convert_image_data_format(tiles[tile].format, tiles[tile].size);
}

/** @brief Implement the fill rectangle command. */
void fillRectangle(u64 command, u64 const *params) {
    /* Input coordinates are in the 10.2 fixed point format. */
    unsigned xl = (command >> 44) & 0xfffu;
    unsigned yl = (command >> 32) & 0xfffu;
    unsigned xh = (command >> 12) & 0xfffu;
    unsigned yh = (command >>  0) & 0xfffu;

    debugger::debug(Debugger::RDP, "  xl: {}", (float)xl / 4.);
    debugger::debug(Debugger::RDP, "  yl: {}", (float)yl / 4.);
    debugger::debug(Debugger::RDP, "  xh: {}", (float)xh / 4.);
    debugger::debug(Debugger::RDP, "  yh: {}", (float)yh / 4.);

    /* Expect rasterizer to be in Fill cycle type. */
    if (other_modes.cycle_type != CYCLE_TYPE_FILL) {
        debugger::warn(Debugger::RDP, "fill_rectangle not in fill cycle type");
        debugger::halt("fill_rectangle: invalid cycle type");
        return;
    }

    if (xh > xl || yh > yl) {
        debugger::warn(Debugger::RDP, "invalid fill_rcetangle coordinates");
        debugger::halt("fill_rectangle: invalid coordinates");
        return;
    }

    /* TODO in fill mode rectangles are scissored to the neareast
     * 4 pixel boundary
     * TODO potential edge case : one-line scissorbox
     * */
    for (unsigned y = (yh >> 2); y <= (yl >> 2); y++)
        FillMode::renderLine(y << 2, xh, xl);
}

/**
 * MI includes a 32-bit FILL color register used for the FILL cycle type.
 * Normally, this fill color is programmed to a constant value and is used
 * to fill the background color or z-buffer. The FILL color register is 32
 * bits compared to 18 × 2 = 36 bits for two pixels in the frame buffer, so
 * only a few bits are used repeatedly.
 */
void setFillColor(u64 command, u64 const *params) {
    rdp.fill_color = command;
}

void setFogColor(u64 command, u64 const *params) {
    rdp.fog_color = {
        (u8)(command >> 24),
        (u8)(command >> 16),
        (u8)(command >> 8 ),
        (u8)(command      ),
    };
}

void setBlendColor(u64 command, u64 const *params) {
    rdp.blend_color = {
        (u8)(command >> 24),
        (u8)(command >> 16),
        (u8)(command >> 8 ),
        (u8)(command      ),
    };
}

void setPrimColor(u64 command, u64 const *params) {
    rdp.prim_color = {
        (u8)(command >> 24),
        (u8)(command >> 16),
        (u8)(command >> 8 ),
        (u8)(command      ),
    };
}

void setEnvColor(u64 command, u64 const *params) {
    rdp.env_color = {
        (u8)(command >> 24),
        (u8)(command >> 16),
        (u8)(command >> 8 ),
        (u8)(command      ),
    };
}

void setCombineMode(u64 command, u64 const *params) {
    combine_mode.sub_a_R_0 = (command >> 52) & 0xfu;
    combine_mode.mul_R_0   = (command >> 47) & 0x1fu;
    combine_mode.sub_a_A_0 = (command >> 44) & 0x7u;
    combine_mode.mul_A_0   = (command >> 41) & 0x7u;
    combine_mode.sub_a_R_1 = (command >> 37) & 0xfu;
    combine_mode.mul_R_1   = (command >> 32) & 0x1fu;
    combine_mode.sub_b_R_0 = (command >> 28) & 0xfu;
    combine_mode.sub_b_R_1 = (command >> 24) & 0xfu;
    combine_mode.sub_a_A_1 = (command >> 21) & 0x7u;
    combine_mode.mul_A_1   = (command >> 18) & 0x7u;
    combine_mode.add_R_0   = (command >> 15) & 0x7u;
    combine_mode.sub_b_A_0 = (command >> 12) & 0x7u;
    combine_mode.add_A_0   = (command >>  9) & 0x7u;
    combine_mode.add_R_1   = (command >>  6) & 0x7u;
    combine_mode.sub_b_A_1 = (command >>  3) & 0x7u;
    combine_mode.add_A_1   = (command >>  0) & 0x7u;

    debugger::debug(Debugger::RDP, "  sub_a_R_0: {}", combine_mode.sub_a_R_0);
    debugger::debug(Debugger::RDP, "  sub_b_R_0: {}", combine_mode.sub_b_R_0);
    debugger::debug(Debugger::RDP, "  mul_R_0: {}", combine_mode.mul_R_0);
    debugger::debug(Debugger::RDP, "  add_R_0: {}", combine_mode.add_R_0);
    debugger::debug(Debugger::RDP, "  sub_a_A_0: {}", combine_mode.sub_a_A_0);
    debugger::debug(Debugger::RDP, "  sub_b_A_0: {}", combine_mode.sub_b_A_0);
    debugger::debug(Debugger::RDP, "  mul_A_0: {}", combine_mode.mul_A_0);
    debugger::debug(Debugger::RDP, "  add_A_0: {}", combine_mode.add_A_0);
    debugger::debug(Debugger::RDP, "  sub_a_R_1: {}", combine_mode.sub_a_R_1);
    debugger::debug(Debugger::RDP, "  sub_b_R_1: {}", combine_mode.sub_b_R_1);
    debugger::debug(Debugger::RDP, "  mul_R_1: {}", combine_mode.mul_R_1);
    debugger::debug(Debugger::RDP, "  add_R_1: {}", combine_mode.add_R_1);
    debugger::debug(Debugger::RDP, "  sub_a_A_1: {}", combine_mode.sub_a_A_1);
    debugger::debug(Debugger::RDP, "  sub_b_A_1: {}", combine_mode.sub_b_A_1);
    debugger::debug(Debugger::RDP, "  mul_A_1: {}", combine_mode.mul_A_1);
    debugger::debug(Debugger::RDP, "  add_A_1: {}", combine_mode.add_A_1);
}

void setTextureImage(u64 command, u64 const *params) {
    texture_image.format = (enum image_data_format)((command >> 53) & 0x7u);
    texture_image.size = (enum pixel_size)((command >> 51) & 0x3u);
    texture_image.width = 1u + ((command >> 32) & 0x3ffu);
    texture_image.addr = command & 0x3fffffflu;

    debugger::debug(Debugger::RDP, "  format: {}", texture_image.format);
    debugger::debug(Debugger::RDP, "  size: {}", texture_image.size);
    debugger::debug(Debugger::RDP, "  width: {}", texture_image.width);
    debugger::info(Debugger::RDP,  "  addr: {:#x}", texture_image.addr);

    if ((texture_image.addr % 8u) != 0) {
        debugger::warn(Debugger::RDP, "set_texture_image: misaligned data address");
        debugger::halt("set_texture_image: invalid address");
        return;
    }

    texture_image.type = convert_image_data_format(texture_image.format,
                                                   texture_image.size);
}

void setZImage(u64 command, u64 const *params) {
    z_image.addr = command & 0x3fffffflu;

    debugger::info(Debugger::RDP, "  addr: {:#x}", z_image.addr);

    if ((z_image.addr % 8u) != 0) {
        debugger::warn(Debugger::RDP, "set_z_image: misaligned data address");
        debugger::halt("set_z_image: invalid address");
        return;
    }
}

void setColorImage(u64 command, u64 const *params) {
    color_image.format = (enum image_data_format)((command >> 53) & 0x7u);
    color_image.size = (enum pixel_size)((command >> 51) & 0x3u);
    color_image.width = 1u + ((command >> 32) & 0x3ffu);
    color_image.addr = command & 0x3fffffflu;

    debugger::debug(Debugger::RDP, "  format: {}", color_image.format);
    debugger::debug(Debugger::RDP, "  size: {}", color_image.size);
    debugger::debug(Debugger::RDP, "  width: {}", color_image.width);
    debugger::info(Debugger::RDP,  "  addr: {:#x}", color_image.addr);

    if ((color_image.addr % 8u) != 0) {
        debugger::warn(Debugger::RDP, "set_color_image: misaligned data address");
        debugger::halt("set_color_image: invalid address");
        return;
    }

    color_image.type = convert_image_data_format(color_image.format, color_image.size);
    if (color_image.type != IMAGE_DATA_FORMAT_RGBA_5_5_5_1 &&
        color_image.type != IMAGE_DATA_FORMAT_RGBA_8_8_8_8 &&
        color_image.type != IMAGE_DATA_FORMAT_CI_8) {
        debugger::warn(Debugger::RDP,
            "set_color_image: invalid image data format: {},{}",
            color_image.format, color_image.size);
        debugger::halt("set_color_image: invalid format");
        return;
    }
}

void noop(u64 command, u64 const *params) {
}

/**
 * @brief Write the DP Command register DPC_STATUS_REG.
 *  This function is used for both the CPU (DPC_STATUS_REG) and
 *  RSP (Coprocessor 0 register 11) view of the register.
 */
void write_DPC_STATUS_REG(u32 value) {
    debugger::info(Debugger::DPCommand, "DPC_STATUS_REG <- {:08x}", value);
    if (value & DPC_STATUS_CLR_XBUS_DMEM_DMA) {
        state.hwreg.DPC_STATUS_REG &= ~DPC_STATUS_XBUS_DMEM_DMA;
    }
    if (value & DPC_STATUS_SET_XBUS_DMEM_DMA) {
        state.hwreg.DPC_STATUS_REG |= DPC_STATUS_XBUS_DMEM_DMA;
    }
    if (value & DPC_STATUS_CLR_FREEZE) {
        state.hwreg.DPC_STATUS_REG &= ~DPC_STATUS_FREEZE;
    }
    if (value & DPC_STATUS_SET_FREEZE) {
        state.hwreg.DPC_STATUS_REG |= DPC_STATUS_FREEZE;
    }
    if (value & DPC_STATUS_CLR_FLUSH) {
        state.hwreg.DPC_STATUS_REG &= ~DPC_STATUS_FLUSH;
    }
    if (value & DPC_STATUS_SET_FLUSH) {
        state.hwreg.DPC_STATUS_REG |= DPC_STATUS_FLUSH;
    }
    if (value & DPC_STATUS_CLR_TMEM_CTR) {
        state.hwreg.DPC_TMEM_REG = 0;
    }
    if (value & DPC_STATUS_CLR_PIPE_CTR) {
        state.hwreg.DPC_PIPE_BUSY_REG = 0;
    }
    if (value & DPC_STATUS_CLR_CMD_CTR) {
        state.hwreg.DPC_BUF_BUSY_REG = 0;
    }
    if (value & DPC_STATUS_CLR_CLOCK_CTR) {
        state.hwreg.DPC_CLOCK_REG = 0;
    }
}

/**
 * @brief Write the DPC_START_REG register.
 * This action is emulated as writing to DPC_CURRENT_REG at the same time,
 * which is only an approximation.
 */
void write_DPC_START_REG(u32 value) {
    state.hwreg.DPC_START_REG = value & 0xfffffflu;
    state.hwreg.DPC_CURRENT_REG = value & 0xfffffflu;
    // state.hwreg.DPC_STATUS_REG |= DPC_STATUS_START_VALID;
}

static bool DPC_hasNext(unsigned count) {
    return state.hwreg.DPC_CURRENT_REG + (count * 8) <= state.hwreg.DPC_END_REG;
}

static void DPC_read(u64 *dwords, unsigned nr_dwords) {
    u32 start = state.hwreg.DPC_CURRENT_REG;
    u32 end = start + nr_dwords * 8;
    for (; start < end; start += 8, dwords++) {
        u64 value;
        if (state.hwreg.DPC_STATUS_REG & DPC_STATUS_XBUS_DMEM_DMA) {
            u64 offset = start & UINT64_C(0xfff);
            memcpy(&value, &state.dmem[offset], sizeof(value));
            value = __builtin_bswap64(value);
        } else {
            state.physmem.load(8, start, &value);
        }
        *dwords = value;
    }
}

typedef void (*RDPCommand)(u64, u64 const *);
struct {
    unsigned nrDoubleWords; /**< Number of double words composing the command */
    RDPCommand command;     /**< Pointer to the method implementing the command */
    std::string name;       /**< String command name */
} RDPCommands[64] = {
    { 1,  noop,                         "no_op" },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 4,  nonShadedTriangle,            "non_shaded_triangle" },
    { 6,  nonShadedZbuffTriangle,       "non_shaded_zbuff_triangle" },
    { 12, textureTriangle,              "texture_triangle" },
    { 14, textureZbuffTriangle,         "texture_zbuff_triangle" },
    { 12, shadeTriangle,                "shade_triangle" },
    { 14, shadeZbuffTriangle,           "shade_zbuff_triangle" },
    { 20, shadeTextureTriangle,         "shade_texture_triangle" },
    { 22, shadeTextureZbuffTriangle,    "shade_texture_zbuff_triangle" },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 0,  NULL },
    { 2,  textureRectangle,             "texture_rectangle" },
    { 2,  textureRectangleFlip,         "texture_rectangle_flip" },
    { 1,  syncLoad,                     "sync_load" },
    { 1,  syncPipe,                     "sync_pipe" },
    { 1,  syncTile,                     "sync_tile" },
    { 1,  syncFull,                     "sync_full" },
    { 1,  setKeyGB,                     "set_key_gb" },
    { 1,  setKeyR,                      "set_key_r" },
    { 1,  setConvert,                   "set_convert" },
    { 1,  setScissor,                   "set_scissor" },
    { 1,  setPrimDepth,                 "set_prim_depth" },
    { 1,  setOtherModes,                "set_other_modes" },
    { 1,  loadTlut,                     "load_tlut" },
    { 0,  NULL },
    { 1,  setTileSize,                  "set_tile_size" },
    { 1,  loadBlock,                    "load_block" },
    { 1,  loadTile,                     "load_tile" },
    { 1,  setTile,                      "set_tile" },
    { 1,  fillRectangle,                "fill_rectangle" },
    { 1,  setFillColor,                 "set_fill_color" },
    { 1,  setFogColor,                  "set_fog_color" },
    { 1,  setBlendColor,                "set_blend_color" },
    { 1,  setPrimColor,                 "set_prim_color" },
    { 1,  setEnvColor,                  "set_env_color" },
    { 1,  setCombineMode,               "set_combine_mode" },
    { 1,  setTextureImage,              "set_texture_image" },
    { 1,  setZImage,                    "set_z_image" },
    { 1,  setColorImage,                "set_color_image" },
};

/**
 * @brief Write the DPC_END_REG register, which kickstarts the process of
 * loading commands from memory.
 * Commands are read from the DPC_CURRENT_REG until the DPC_END_REG excluded,
 * updating DPC_CURRENT_REG at the same time.
 */
void write_DPC_END_REG(u32 value) {
    state.hwreg.DPC_END_REG = value & 0xfffffflu;
    while (DPC_hasNext(1)) {
        u64 command = 0;
        DPC_read(&command, 1);
        u64 opcode = (command >> 56) & 0x3flu;

        if (RDPCommands[opcode].command == NULL) {
            debugger::warn(Debugger::RDP, "unknown command {:02x} [{:016x}]",
                opcode, command);
            debugger::halt("DPC unknown command");
            break;
        }

        unsigned nr_dwords = RDPCommands[opcode].nrDoubleWords;
        if (!DPC_hasNext(nr_dwords)) {
            debugger::info(Debugger::RDP, "incomplete command {} [{:016x}]",
                RDPCommands[opcode].name, command);
            break;
        }

        // Read the command parameters.
        u64 params[nr_dwords] = { 0 };
        DPC_read(params, nr_dwords);

        debugger::info(Debugger::RDP, "{} [{:016x}]",
            RDPCommands[opcode].name, command);

        RDPCommands[opcode].command(command, params + 1);
        state.hwreg.DPC_CURRENT_REG += 8 * nr_dwords;
    }
}

}; /* namespace R4300 */
