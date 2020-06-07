
#ifndef _R4300_RDP_H_INCLUDED_
#define _R4300_RDP_H_INCLUDED_

#include <types.h>

namespace R4300 {

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

/** Cycle type set by the SetOtherModes DPC command */
enum cycle_type {
    CYCLE_TYPE_1CYCLE = 0,
    CYCLE_TYPE_2CYCLE = 1,
    CYCLE_TYPE_COPY = 2,
    CYCLE_TYPE_FILL = 3,
};

/** TLUT type set by the SetOtherModes DPC command */
enum tlut_type {
    TLUT_TYPE_RGBA = 0,
    TLUT_TYPE_IA = 1,
};

/** Sample type set by the SetOtherModes DPC command */
enum sample_type {
    SAMPLE_TYPE_1X1 = 0,
    SAMPLE_TYPE_2X2 = 1,
    SAMPLE_TYPE_4X1 = 2, /* When cycle_type == COPY */
};

/** RGB dithering mode set by the SetOtherModes DPC command */
enum rgb_dither_sel {
    RGB_DITHER_SEL_MAGIC_SQUARE = 0,
    RGB_DITHER_SEL_BAYER_MATRIX = 1,
    RGB_DITHER_SEL_NOISE = 2,
    RGB_DITHER_SEL_NONE = 3,
};

/** Alpha dithering mode set by the SetOtherModes DPC command */
enum alpha_dither_sel {
    ALPHA_DITHER_SEL_PATTERN = 0,
    ALPHA_DITHER_SEL_NEG_PATTERN = 1,
    ALPHA_DITHER_SEL_NOISE = 2,
    ALPHA_DITHER_SEL_NONE = 3,
};

/** Blender mode set by the SetOtherModes DPC command */
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

/** Z mode set by the SetOtherModes DPC command */
enum z_mode {
    Z_MODE_OPAQUE = 0,
    Z_MODE_INTERPENETRATING = 1,
    Z_MODE_TRANSPARENT = 2,
    Z_MODE_DECAL = 3,
};

/** Z mode set by the SetOtherModes DPC command */
enum cvg_dest {
    CVG_DEST_CLAMP = 0,
    CVG_DEST_WRAP = 1,
    CVG_DEST_ZAP = 2,
    CVG_DEST_SAVE = 3,
};

/** Z source selection set by the SetOtherModes DPC command */
enum z_source_sel {
    Z_SOURCE_SEL_PIXEL = 0,
    Z_SOURCE_SEL_PRIMITIVE = 1,
};

/**
 * Internal color representation. The RDP graphics pipeline performs most
 * operations at 8 bits per component RGBA pixel. After searching the texels,
 * the texture unit will convert them to 32bit RGBA format.
 */
typedef struct color {
    u8 r, g, b, a;
} color_t;

/** Tile information. */
struct tile {
    enum image_data_type type;
    enum image_data_format format;
    enum pixel_size size;
    unsigned line;          /**< Size of tile line in 64b words, max of 4KB */
    unsigned tmem_addr;     /**< Starting Tmem address for this tile in 64b
                                 words, 4KB range */
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
};

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

    /* Primitive depth. */
    u32         prim_z;
    u16         prim_deltaz;

    /* Current scissor box. */
    struct {
        unsigned xh;
        unsigned yh;
        unsigned xl;
        unsigned yl;
        bool skipOddLines;
        bool skipEvenLines;
    } scissor;

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

    /* Current texture image configuration. */
    struct {
        enum image_data_format  format;
        enum pixel_size         size;
        enum image_data_type    type;
        unsigned width;         /**< Width of image in pixels. */
        u32      addr;
    } texture_image;

    /* Current color image configuration. */
    struct {
        enum image_data_format  format;
        enum pixel_size         size;
        enum image_data_type    type;
        unsigned width;         /**< Width of image in pixels. */
        u32      addr;
    } color_image;

    /* Current Z image configuration. */
    struct {
        u32      addr;
    } z_image;

    /* Tile information. */
    struct tile tiles[8];

    /* Color combiner configuration. */
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

    /* General configuration. */
    struct {
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
};

extern struct rdp rdp;

void write_DPC_STATUS_REG(u32 value);
void write_DPC_START_REG(u32 value);
void write_DPC_END_REG(u32 value);

};

#endif /* _R4300_RDP_H_INCLUDED_ */
