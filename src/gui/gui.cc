
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <ctime>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_disassembler.h>
#include <imgui_trace.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <core.h>
#include <assembly/registers.h>
#include <debugger.h>
#include <r4300/state.h>
#include <r4300/rdp.h>
#include <graphics.h>

using namespace n64;

static Disassembler imemDisassembler(12);
static Disassembler dramDisassembler(22);
static Disassembler romDisassembler(12);
static Trace cpuTrace(&debugger::debugger.cpuTrace);
static Trace rspTrace(&debugger::debugger.rspTrace);

static std::chrono::time_point<std::chrono::steady_clock> startTime;
static unsigned long startCycles;
static unsigned long startRecompilerCycles;
static unsigned long startRecompilerRequests;
static unsigned long startRecompilerCacheClears;

static void glfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

static void ShowAnalytics(void) {
    // CPU freq is 93.75 MHz
    static float timeRatio[5 * 60] = { 0 };
    static float recompilerUsage[5 * 60] = { 0 };
    static float recompilerRequests[5 * 60] = { 0 };
    static float recompilerCacheClears[5 * 60] = { 0 };
    static float recompilerCache[5 * 60] = { 0 };
    static float recompilerBuffer[5 * 60] = { 0 };
    static unsigned plotOffset = 0;
    unsigned plotLength = 5 * 60;
    unsigned plotUpdateInterval = 200;
    float plotWidth = ImGui::GetContentRegionAvail().x;
    float plotHeight = 40.0f;
    ImVec2 plotDimensions = ImVec2(plotWidth, plotHeight);

    auto updateTime = std::chrono::steady_clock::now();;
    std::chrono::duration<double> diffTime = updateTime - startTime;
    unsigned long updateCycles = R4300::state.cycles;
    unsigned long updateRecompilerCycles = core::recompiler_cycles;
    unsigned long updateRecompilerRequests = core::recompiler_requests;
    unsigned long updateRecompilerCacheClears = core::recompiler_clears;

    float elapsedMilliseconds = diffTime.count() * 1000.0;
    float machineMilliseconds = (updateCycles - startCycles) / 93750.0;

    if (elapsedMilliseconds >= plotUpdateInterval) {
        timeRatio[plotOffset] =
            machineMilliseconds * 100.0 / elapsedMilliseconds;

        recompilerUsage[plotOffset] =
            (updateCycles == startCycles) ? 0 :
                (updateRecompilerCycles - startRecompilerCycles) * 100.0 /
                (updateCycles - startCycles);

        recompilerRequests[plotOffset] =
            updateRecompilerRequests - startRecompilerRequests;

        recompilerCacheClears[plotOffset] =
            updateRecompilerCacheClears - startRecompilerCacheClears;

        core::get_recompiler_cache_stats(
            recompilerCache + plotOffset,
            recompilerBuffer + plotOffset);

        recompilerCache[plotOffset] *= 100.0;
        recompilerBuffer[plotOffset] *= 100.0;

        plotOffset = (plotOffset + 1) % plotLength;
        startTime = updateTime;
        startCycles = updateCycles;
        startRecompilerCycles = updateRecompilerCycles;
        startRecompilerRequests = updateRecompilerRequests;
        startRecompilerCacheClears = updateRecompilerCacheClears;
    }

    ImGui::PlotLines("", timeRatio, plotLength, plotOffset,
        "time ratio", 0.0f, 100.0f, plotDimensions);
    ImGui::PlotLines("", recompilerUsage, plotLength, plotOffset,
        "recompiler usage", 0.0f, 100.0f, plotDimensions);
    ImGui::PlotLines("", recompilerRequests, plotLength, plotOffset,
        "recompiler requests", 0.0f, 500.0f, plotDimensions);
    ImGui::PlotLines("", recompilerCacheClears, plotLength, plotOffset,
        "recompiler cache clears", 0.0f, 500.0f, plotDimensions);
    ImGui::PlotLines("", recompilerCache, plotLength, plotOffset,
        "recompiler cache", 0.0f, 100.0f, plotDimensions);
    ImGui::PlotLines("", recompilerBuffer, plotLength, plotOffset,
        "recompiler buffer", 0.0f, 100.0f, plotDimensions);
}

static void ShowCpuRegisters(void) {
    ImGui::Text("pc       %016" PRIx64 "\n", R4300::state.reg.pc);
    for (unsigned int i = 0; i < 32; i+=2) {
        ImGui::Text("%-8.8s %016" PRIx64 "  %-8.8s %016" PRIx64 "\n",
            assembly::cpu::getRegisterName(i), R4300::state.reg.gpr[i],
            assembly::cpu::getRegisterName(i + 1), R4300::state.reg.gpr[i + 1]);
    }
}

static void ShowCpuCop0Registers(void) {
    #define Print2Regs(fmt0, name0, fmt1, name1) \
        ImGui::Text("%-8.8s %016" fmt0 "  %-8.8s %016" fmt1 "\n", \
            #name0, R4300::state.cp0reg.name0, \
            #name1, R4300::state.cp0reg.name1)

    Print2Regs(PRIx32, index,    PRIx32, random);
    Print2Regs(PRIx64, entrylo0, PRIx64, entrylo1);
    Print2Regs(PRIx64, context,  PRIx32, pagemask);
    Print2Regs(PRIx32, wired,    PRIx32, c7);
    Print2Regs(PRIx64, badvaddr, PRIx32, count);
    Print2Regs(PRIx64, entryhi,  PRIx32, compare);
    Print2Regs(PRIx32, sr,       PRIx32, cause);
    Print2Regs(PRIx64, epc,      PRIx32, prid);
    Print2Regs(PRIx32, config,   PRIx32, lladdr);
    Print2Regs(PRIx32, watchlo,  PRIx32, watchhi);
    Print2Regs(PRIx64, xcontext, PRIx32, c21);
    Print2Regs(PRIx32, c22,      PRIx32, c23);
    Print2Regs(PRIx32, c24,      PRIx32, c25);
    Print2Regs(PRIx32, perr,     PRIx32, cacheerr);
    Print2Regs(PRIx32, taglo,    PRIx32, taghi);
    Print2Regs(PRIx64, errorepc, PRIx32, c31);
    #undef Print2Regs
}

static void ShowCpuCop1Registers(void) {
    ImGui::Text("fcr0     %08" PRIx32 "  fcr31    %08" PRIx32 "\n",
                R4300::state.cp1reg.fcr0, R4300::state.cp1reg.fcr31);

    if (R4300::state.cp0reg.FR()) {
        for (unsigned int nr = 0; nr < 32; nr+=2) {
            ImGui::Text("fgr%-2u    %016" PRIx64 "  fgr%-2u    %016" PRIx64 "\n",
                        nr, R4300::state.cp1reg.fpr_d[nr]->l,
                        nr + 1, R4300::state.cp1reg.fpr_d[nr + 1]->l);
        }
        for (unsigned int nr = 0; nr < 32; nr++) {
            ImGui::Text("fpr%-2u    d:%f s:%f\n",
                        nr, R4300::state.cp1reg.fpr_d[nr]->d,
                        R4300::state.cp1reg.fpr_s[nr]->s);
        }
    } else {
        for (unsigned int nr = 0; nr < 32; nr+=2) {
            ImGui::Text("fgr%-2u    %08" PRIx32 "  fgr%-2u    %08" PRIx32
                        "    s:%f    d:%f\n",
                        nr, R4300::state.cp1reg.fpr_s[nr]->w,
                        nr + 1, R4300::state.cp1reg.fpr_s[nr + 1]->w,
                        R4300::state.cp1reg.fpr_s[nr]->s,
                        R4300::state.cp1reg.fpr_d[nr]->d);
        }
    }
}

static void ShowCpuTLB(void) {
    for (unsigned int nr = 0; nr < R4300::tlbEntryCount; nr++) {
        R4300::tlbEntry *entry = &R4300::state.tlb[nr];
        u64 vpn2 = entry->entryHi & ~0x1fffflu;
        unsigned asid = entry->entryHi & 0xff;
        ImGui::Text("[%2.u]  VPN2:%016lx ASID:%u", nr, vpn2, asid);
        u64 pfn = entry->entryLo0 & ~0x3fu;
        unsigned c = (entry->entryLo0 >> 3) & 0x3u;
        unsigned d = (entry->entryLo0 >> 2) & 0x1u;
        unsigned v = (entry->entryLo0 >> 1) & 0x1u;
        unsigned g = (entry->entryLo0 >> 0) & 0x1u;
        ImGui::Text("      PFN:%06lx C:%x D:%u V:%u G:%u", pfn, c, d, v, g);
        c = (entry->entryLo1 >> 3) & 0x3u;
        d = (entry->entryLo1 >> 2) & 0x1u;
        v = (entry->entryLo1 >> 1) & 0x1u;
        g = (entry->entryLo1 >> 0) & 0x1u;
        ImGui::Text("      PFN:%06lx C:%x D:%u V:%u G:%u", pfn, c, d, v, g);
    }
}

static void ShowRspRegisters(void) {
    ImGui::Text("pc       %016" PRIx64 "\n", R4300::state.rspreg.pc);
    for (unsigned int i = 0; i < 32; i+=2) {
        ImGui::Text("%-8.8s %016" PRIx64 "  %-8.8s %016" PRIx64 "\n",
            assembly::cpu::getRegisterName(i), R4300::state.rspreg.gpr[i],
            assembly::cpu::getRegisterName(i + 1), R4300::state.rspreg.gpr[i + 1]);
    }
}


static void ShowRspCop2Registers(void) {
    ImGui::Text("vco     %04" PRIx16, R4300::state.rspreg.vco);
    ImGui::Text("vcc     %04" PRIx16, R4300::state.rspreg.vcc);
    ImGui::Text("vce     %02" PRIx8,  R4300::state.rspreg.vce);
    ImGui::Text("vacc   ");
    for (unsigned e = 0; e < 8; e++) {
        ImGui::SameLine();
        ImGui::Text("%04" PRIx64,
            (R4300::state.rspreg.vacc[e].acc >> 32) & UINT64_C(0xffff));
    }
    ImGui::Text("       ");
    for (unsigned e = 0; e < 8; e++) {
        ImGui::SameLine();
        ImGui::Text("%04" PRIx64,
            (R4300::state.rspreg.vacc[e].acc >> 16) & UINT64_C(0xffff));
    }
    ImGui::Text("       ");
    for (unsigned e = 0; e < 8; e++) {
        ImGui::SameLine();
        ImGui::Text("%04" PRIx64,
            R4300::state.rspreg.vacc[e].acc & UINT64_C(0xffff));
    }
    for (unsigned int nr = 0; nr < 32; nr++) {
        ImGui::Text("vr%-2u   ", nr);
        for (unsigned e = 0; e < 8; e++) {
            ImGui::SameLine();
            ImGui::Text("%04x", R4300::state.rspreg.vr[nr].h[e]);
        }
    }
}

static void ShowRdpColorConfig(char const *label, R4300::color_t *c) {
    ImGui::SetCursorPosX(ImGui::GetTreeNodeToLabelSpacing());
    ImGui::Text("%s", label);
    ImGui::SameLine(150);
    ImGui::Text("%02x %02x %02x %02x", c->r, c->g, c->b, c->a);

    float cf[3] = {
        (float)c->r / 256.f,
        (float)c->g / 256.f,
        (float)c->b / 256.f, };

    ImGui::SameLine();
    ImGui::ColorEdit3(label, cf,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
}

static inline float i32_fixpoint_to_float(i32 val, int radix) {
    unsigned long div = 1lu << radix;
    double fval = (i64)val;
    return fval / (float)div;
}

static inline float u32_fixpoint_to_float(u32 val, int radix) {
    unsigned long div = 1lu << radix;
    double fval = val;
    return fval / (float)div;
}

static char const *bool_to_string(bool val) {
    return val ? "true" : "false";
}

static void ShowRdpInformation(void) {
    /* Print fill color */
    ImGui::SetCursorPosX(ImGui::GetTreeNodeToLabelSpacing());
    ImGui::Text("fill_color");
    ImGui::SameLine(150);
    u32 fill_color = R4300::rdp.fill_color;
    ImGui::Text("%02x %02x %02x %02x",
        (fill_color >> 24) & 0xffu,
        (fill_color >> 16) & 0xffu,
        (fill_color >>  8) & 0xffu,
        (fill_color >>  0) & 0xffu);

    float col_32[3] = {
        (float)((fill_color >> 24) & 0xffu) / 256.f,
        (float)((fill_color >> 16) & 0xffu) / 256.f,
        (float)((fill_color >>  8) & 0xffu) / 256.f, };
    // TODO
    float col_16_a[3] = {
        (float)((fill_color >> 24) & 0xffu) / 256.f,
        (float)((fill_color >> 16) & 0xffu) / 256.f,
        (float)((fill_color >>  8) & 0xffu) / 256.f, };
    float col_16_b[3] = {
        (float)((fill_color >> 24) & 0xffu) / 256.f,
        (float)((fill_color >> 16) & 0xffu) / 256.f,
        (float)((fill_color >>  8) & 0xffu) / 256.f, };

    ImGui::SameLine();
    ImGui::ColorEdit3("fill_color_32", col_32,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::SameLine();
    ImGui::ColorEdit3("fill_color_16_a", col_16_a,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::SameLine();
    ImGui::ColorEdit3("fill_color_16_b", col_16_b,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

    /* Print other color registers */
    ShowRdpColorConfig("fog_color", &R4300::rdp.fog_color);
    ShowRdpColorConfig("blend_color", &R4300::rdp.blend_color);
    ShowRdpColorConfig("prim_color", &R4300::rdp.prim_color);
    ShowRdpColorConfig("env_color", &R4300::rdp.env_color);

    /* Primitive depth. */
    ImGui::SetCursorPosX(ImGui::GetTreeNodeToLabelSpacing());
    ImGui::Text("prim_z: %.3f", u32_fixpoint_to_float(R4300::rdp.prim_z, 3));
    ImGui::SetCursorPosX(ImGui::GetTreeNodeToLabelSpacing());
    ImGui::Text("prim_deltaz: %u", R4300::rdp.prim_deltaz);

    /* Scissor box. */
    if (ImGui::TreeNode("scissor")) {
        ImGui::Text("xh: %.2f", u32_fixpoint_to_float(R4300::rdp.scissor.xh, 2));
        ImGui::Text("yh: %.2f", u32_fixpoint_to_float(R4300::rdp.scissor.yh, 2));
        ImGui::Text("xl: %.2f", u32_fixpoint_to_float(R4300::rdp.scissor.xl, 2));
        ImGui::Text("yl: %.2f", u32_fixpoint_to_float(R4300::rdp.scissor.yl, 2));
        ImGui::Text("skip_odd: %s",
            bool_to_string(R4300::rdp.scissor.skip_odd));
        ImGui::Text("skip_even: %s",
            bool_to_string(R4300::rdp.scissor.skip_even));
        ImGui::TreePop();
    }
    ImGui::Separator();

    /* Convert */
    // TODO

    /* Key */
    // TODO

    static char const *image_data_format_names[] = {
        "I_4",
        "IA_3_1",
        "CI_4",
        "I_8",
        "IA_4_4",
        "CI_8",
        "RGBA_5_5_5_1",
        "IA_8_8",
        "YUV_16",
        "RGBA_8_8_8_8",
    };

    /* Texture image */
    if (ImGui::TreeNode("texture_image")) {
        ImGui::Text("type: %s",
            image_data_format_names[R4300::rdp.texture_image.type]);
        ImGui::Text("width: %u", R4300::rdp.texture_image.width);
        ImGui::Text("addr: 0x%06x", R4300::rdp.texture_image.addr);
        ImGui::TreePop();
    }
    /* Color image */
    if (ImGui::TreeNode("color_image")) {
        ImGui::Text("type: %s",
            image_data_format_names[R4300::rdp.color_image.type]);
        ImGui::Text("width: %u", R4300::rdp.color_image.width);
        ImGui::Text("addr: 0x%06x", R4300::rdp.color_image.addr);
        ImGui::TreePop();
    }
    /* Z image */
    if (ImGui::TreeNode("z_image")) {
        ImGui::Text("addr: 0x%06x", R4300::rdp.z_image.addr);
        ImGui::TreePop();
    }

    /* Tiles */
    for (unsigned i = 0; i < 8; i++) {
        ImGui::PushID(i);
        ImGui::Separator();
        if (ImGui::TreeNode("tile", "tile[%u]", i)) {
            ImGui::Text("type: %s",
                image_data_format_names[R4300::rdp.tiles[i].type]);
            ImGui::Text("line: %u", R4300::rdp.tiles[i].line);
            ImGui::Text("tmem_addr: 0x%03x",
                R4300::rdp.tiles[i].tmem_addr << 3);
            ImGui::Text("palette: %u", R4300::rdp.tiles[i].palette);
            ImGui::Text("clamp_t: %s", bool_to_string(R4300::rdp.tiles[i].clamp_t));
            ImGui::Text("mirror_t: %s", bool_to_string(R4300::rdp.tiles[i].mirror_t));
            ImGui::Text("mask_t: %u", R4300::rdp.tiles[i].mask_t);
            ImGui::Text("shift_t: %u", R4300::rdp.tiles[i].shift_t);
            ImGui::Text("clamp_s: %s", bool_to_string(R4300::rdp.tiles[i].clamp_s));
            ImGui::Text("mirror_s: %s", bool_to_string(R4300::rdp.tiles[i].mirror_s));
            ImGui::Text("mask_s: %u", R4300::rdp.tiles[i].mask_s);
            ImGui::Text("shift_s: %u", R4300::rdp.tiles[i].shift_s);
            ImGui::Text("sl: %.2f", u32_fixpoint_to_float(R4300::rdp.tiles[i].sl, 2));
            ImGui::Text("tl: %.2f", u32_fixpoint_to_float(R4300::rdp.tiles[i].tl, 2));
            ImGui::Text("sh: %.2f", u32_fixpoint_to_float(R4300::rdp.tiles[i].sh, 2));
            ImGui::Text("th: %.2f", u32_fixpoint_to_float(R4300::rdp.tiles[i].th, 2));
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    /* Combine mode */
    ImGui::Separator();
    if (ImGui::TreeNode("combine_mode")) {
        static char const *sub_a_R_sels[] = {
            "COMBINED", "TEXEL0", "TEXEL1", "PRIMITIVE", "SHADE", "ENVIRONMENT",
            "1", "NOISE", "0", "0", "0", "0", "0", "0", "0", "0",
        };
        static char const *sub_b_R_sels[] = {
            "COMBINED", "TEXEL0", "TEXEL1", "PRIMITIVE", "SHADE", "ENVIRONMENT",
            "CENTER", "K4", "0", "0", "0", "0", "0", "0", "0", "0",
        };
        static char const *mul_R_sels[] = {
            "COMBINED", "TEXEL0", "TEXEL1", "PRIMITIVE", "SHADE", "ENVIRONMENT",
            "SCALE", "COMBINED A", "TEXEL0 A", "TEXEL1 A", "PRIMITIVE A",
            "SHADE A", "ENVIRONMENT A", "LOD FRACTION", "PRIM LOD FRAC", "K5",
            "0", "0", "0", "0", "0", "0", "0", "0",
            "0", "0", "0", "0", "0", "0", "0", "0",
        };
        static char const *add_R_sels[] = {
            "COMBINED", "TEXEL0", "TEXEL1", "PRIMITIVE", "SHADE", "ENVIRONMENT",
            "1", "0", "0", "0", "0", "0", "0", "0", "0", "0",
        };

        static char const *sub_A_sels[] = {
            "COMBINED A", "TEXEL0 A", "TEXEL1 A", "PRIMITIVE A", "SHADE A",
            "ENVIRONMENT A", "1", "0",
        };
        static char const *mul_A_sels[] = {
            "LOD FRACTION", "TEXEL0 A", "TEXEL1 A", "PRIMITIVE A", "SHADE A",
            "ENVIRONMENT A", "PRIM LOD FRAC", "0",
        };
        static char const *add_A_sels[] = {
            "COMBINED A", "TEXEL0 A", "TEXEL1 A", "PRIMITIVE A", "SHADE A",
            "ENVIRONMENT A", "1", "0",
        };

        ImGui::Text("sub_a_R_0: %s", sub_a_R_sels[R4300::rdp.combine_mode.sub_a_R_0]);
        ImGui::Text("sub_b_R_0: %s", sub_b_R_sels[R4300::rdp.combine_mode.sub_b_R_0]);
        ImGui::Text("mul_R_0: %s", mul_R_sels[R4300::rdp.combine_mode.mul_R_0]);
        ImGui::Text("add_R_0: %s", add_R_sels[R4300::rdp.combine_mode.add_R_0]);
        ImGui::Text("sub_a_A_0: %s", sub_A_sels[R4300::rdp.combine_mode.sub_a_A_0]);
        ImGui::Text("sub_b_A_0: %s", sub_A_sels[R4300::rdp.combine_mode.sub_b_A_0]);
        ImGui::Text("mul_A_0: %s", mul_A_sels[R4300::rdp.combine_mode.mul_A_0]);
        ImGui::Text("add_A_0: %s", add_A_sels[R4300::rdp.combine_mode.add_A_0]);
        ImGui::Separator();
        ImGui::Text("sub_a_R_1: %s", sub_a_R_sels[R4300::rdp.combine_mode.sub_a_R_1]);
        ImGui::Text("sub_b_R_1: %s", sub_b_R_sels[R4300::rdp.combine_mode.sub_b_R_1]);
        ImGui::Text("mul_R_1: %s", mul_R_sels[R4300::rdp.combine_mode.mul_R_1]);
        ImGui::Text("add_R_1: %s", add_R_sels[R4300::rdp.combine_mode.add_R_1]);
        ImGui::Text("sub_a_A_1: %s", sub_A_sels[R4300::rdp.combine_mode.sub_a_A_1]);
        ImGui::Text("sub_b_A_1: %s", sub_A_sels[R4300::rdp.combine_mode.sub_b_A_1]);
        ImGui::Text("mul_A_1: %s", mul_A_sels[R4300::rdp.combine_mode.mul_A_1]);
        ImGui::Text("add_A_1: %s", add_A_sels[R4300::rdp.combine_mode.add_A_1]);
        ImGui::TreePop();
    }

    /* Other modes */
    ImGui::Separator();
    if (ImGui::TreeNode("other_modes")) {
        static char const *cycle_types[] = { "1CYCLE", "2CYCLE", "COPY", "FILL", };
        static char const *tlut_types[] = { "RGBA_5_5_5_1", "IA_8_8", };
        static char const *sample_types[] = { "1x1", "2x2", "4x1", };
        static char const *rgb_dither_sels[] = {
            "MAGIC SQUARE", "BAYER MATRIX", "NOISE", "NONE", };
        static char const *alpha_dither_sels[] = {
            "PATTERN", "NEG PATTERN", "NOISE", "NONE", };

        ImGui::Text("cycle_type: %s", cycle_types[R4300::rdp.other_modes.cycle_type]);
        ImGui::Text("persp_tex_en: %s", bool_to_string(R4300::rdp.other_modes.persp_tex_en));
        ImGui::Text("detail_tex_en: %s", bool_to_string(R4300::rdp.other_modes.detail_tex_en));
        ImGui::Text("sharpen_tex_en: %s", bool_to_string(R4300::rdp.other_modes.sharpen_tex_en));
        ImGui::Text("tex_lod_en: %s", bool_to_string(R4300::rdp.other_modes.tex_lod_en));
        ImGui::Text("tlut_en: %s", bool_to_string(R4300::rdp.other_modes.tlut_en));
        ImGui::Text("tlut_type: %s", tlut_types[R4300::rdp.other_modes.tlut_type]);
        ImGui::Text("sample_type: %s", sample_types[R4300::rdp.other_modes.sample_type]);
        ImGui::Text("mid_texel: %s", bool_to_string(R4300::rdp.other_modes.mid_texel));
        ImGui::Text("bi_lerp_0: %s", bool_to_string(R4300::rdp.other_modes.bi_lerp_0));
        ImGui::Text("bi_lerp_1: %s", bool_to_string(R4300::rdp.other_modes.bi_lerp_1));
        ImGui::Text("convert_one: %s", bool_to_string(R4300::rdp.other_modes.convert_one));
        ImGui::Text("key_en: %s", bool_to_string(R4300::rdp.other_modes.key_en));
        ImGui::Text("rgb_dither_sel: %s", rgb_dither_sels[R4300::rdp.other_modes.rgb_dither_sel]);
        ImGui::Text("alpha_dither_sel: %s", alpha_dither_sels[R4300::rdp.other_modes.alpha_dither_sel]);

        static char const *b_ma_sels[] = {
            "PIXEL", "MEMORY", "BLEND", "FOG", };
        static char const *b_m1b_sels[] = {
            "PIXEL A", "PRIMITIVE A", "SHADE A", "0", };
        static char const *b_m2b_sels[] = {
            "1 - Amux", "MEMORY A", "1", "0", };

        ImGui::Separator();
        ImGui::Text("b_m1a_0: %s", b_ma_sels[R4300::rdp.other_modes.b_m1a_0]);
        ImGui::Text("b_m1b_0: %s", b_m1b_sels[R4300::rdp.other_modes.b_m1b_0]);
        ImGui::Text("b_m2a_0: %s", b_ma_sels[R4300::rdp.other_modes.b_m2a_0]);
        ImGui::Text("b_m2b_0: %s", b_m2b_sels[R4300::rdp.other_modes.b_m2b_0]);
        ImGui::Text("b_m1a_1: %s", b_ma_sels[R4300::rdp.other_modes.b_m1a_1]);
        ImGui::Text("b_m1b_1: %s", b_m1b_sels[R4300::rdp.other_modes.b_m1b_1]);
        ImGui::Text("b_m2a_1: %s", b_ma_sels[R4300::rdp.other_modes.b_m2a_1]);
        ImGui::Text("b_m2b_1: %s", b_m2b_sels[R4300::rdp.other_modes.b_m2b_1]);
        ImGui::Text("force_blend: %s", bool_to_string(R4300::rdp.other_modes.force_blend));

        static char const *z_modes[] = {
            "OPAQUE", "INTERPENETRATING", "TRANSPARENT", "DECAL", };
        static char const *cvg_dests[] = {
            "CLAMP", "WRAP", "ZAP", "SAVE", };
        static char const *z_source_sels[] = { "PIXEL", "PRIMITIVE", };

        ImGui::Separator();
        ImGui::Text("alpha_cvg_select: %s", bool_to_string(R4300::rdp.other_modes.alpha_cvg_select));
        ImGui::Text("cvg_times_alpha: %s", bool_to_string(R4300::rdp.other_modes.cvg_times_alpha));
        ImGui::Text("z_mode: %s", z_modes[R4300::rdp.other_modes.z_mode]);
        ImGui::Text("cvg_dest: %s", cvg_dests[R4300::rdp.other_modes.cvg_dest]);
        ImGui::Text("color_on_cvg: %s", bool_to_string(R4300::rdp.other_modes.color_on_cvg));
        ImGui::Text("image_read_en: %s", bool_to_string(R4300::rdp.other_modes.image_read_en));
        ImGui::Text("z_update_en: %s", bool_to_string(R4300::rdp.other_modes.z_update_en));
        ImGui::Text("z_compare_en: %s", bool_to_string(R4300::rdp.other_modes.z_compare_en));
        ImGui::Text("antialias_en: %s", bool_to_string(R4300::rdp.other_modes.antialias_en));
        ImGui::Text("z_source_sel: %s", z_source_sels[R4300::rdp.other_modes.z_source_sel]);
        ImGui::Text("dither_alpha_en: %s", bool_to_string(R4300::rdp.other_modes.dither_alpha_en));
        ImGui::Text("alpha_compare_en: %s", bool_to_string(R4300::rdp.other_modes.alpha_compare_en));
        ImGui::TreePop();
    }
}

static void ShowRdRamRegisters(void) {
    ImGui::Text("RDRAM_DEVICE_TYPE_REG  %08" PRIx32 "\n",
        R4300::state.hwreg.RDRAM_DEVICE_TYPE_REG);
    ImGui::Text("RDRAM_DEVICE_ID_REG    %08" PRIx32 "\n",
        R4300::state.hwreg.RDRAM_DEVICE_ID_REG);
    ImGui::Text("RDRAM_DELAY_REG        %08" PRIx32 "\n",
        R4300::state.hwreg.RDRAM_DELAY_REG);
    ImGui::Text("RDRAM_MODE_REG         %08" PRIx32 "\n",
        R4300::state.hwreg.RDRAM_MODE_REG);
    ImGui::Text("RDRAM_REF_INTERVAL_REG %08" PRIx32 "\n",
        R4300::state.hwreg.RDRAM_REF_INTERVAL_REG);
    ImGui::Text("RDRAM_REF_ROW_REG      %08" PRIx32 "\n",
        R4300::state.hwreg.RDRAM_REF_ROW_REG);
    ImGui::Text("RDRAM_RAS_INTERVAL_REG %08" PRIx32 "\n",
        R4300::state.hwreg.RDRAM_RAS_INTERVAL_REG);
    ImGui::Text("RDRAM_MIN_INTERVAL_REG %08" PRIx32 "\n",
        R4300::state.hwreg.RDRAM_MIN_INTERVAL_REG);
    ImGui::Text("RDRAM_ADDR_SELECT_REG  %08" PRIx32 "\n",
        R4300::state.hwreg.RDRAM_ADDR_SELECT_REG);
    ImGui::Text("RDRAM_DEVICE_MANUF_REG %08" PRIx32 "\n",
        R4300::state.hwreg.RDRAM_DEVICE_MANUF_REG);
}

static void ShowSPRegisters(void) {
    ImGui::Text("SP_MEM_ADDR_REG        %08" PRIx32 "\n",
        R4300::state.hwreg.SP_MEM_ADDR_REG);
    ImGui::Text("SP_DRAM_ADDR_REG       %08" PRIx32 "\n",
        R4300::state.hwreg.SP_DRAM_ADDR_REG);
    ImGui::Text("SP_RD_LEN_REG          %08" PRIx32 "\n",
        R4300::state.hwreg.SP_RD_LEN_REG);
    ImGui::Text("SP_WR_LEN_REG          %08" PRIx32 "\n",
        R4300::state.hwreg.SP_WR_LEN_REG);
    ImGui::Text("SP_STATUS_REG          %08" PRIx32 "\n",
        R4300::state.hwreg.SP_STATUS_REG);
    ImGui::Text("SP_SEMAPHORE_REG       %08" PRIx32 "\n",
        R4300::state.hwreg.SP_SEMAPHORE_REG);
    ImGui::Text("SP_PC_REG              %08" PRIx64 "\n",
        R4300::state.rspreg.pc);
    ImGui::Text("SP_IBIST_REG           %08" PRIx32 "\n",
        R4300::state.hwreg.SP_IBIST_REG);
}

static void ShowDPCommandRegisters(void) {
    ImGui::Text("DPC_START_REG          %08" PRIx32 "\n",
        R4300::state.hwreg.DPC_START_REG);
    ImGui::Text("DPC_END_REG            %08" PRIx32 "\n",
        R4300::state.hwreg.DPC_END_REG);
    ImGui::Text("DPC_CURRENT_REG        %08" PRIx32 "\n",
        R4300::state.hwreg.DPC_CURRENT_REG);
    ImGui::Text("DPC_STATUS_REG         %08" PRIx32 "\n",
        R4300::state.hwreg.DPC_STATUS_REG);
    ImGui::Text("DPC_CLOCK_REG          %08" PRIx32 "\n",
        R4300::state.hwreg.DPC_CLOCK_REG);
    ImGui::Text("DPC_BUF_BUSY_REG       %08" PRIx32 "\n",
        R4300::state.hwreg.DPC_BUF_BUSY_REG);
    ImGui::Text("DPC_PIPE_BUSY_REG      %08" PRIx32 "\n",
        R4300::state.hwreg.DPC_PIPE_BUSY_REG);
    ImGui::Text("DPC_TMEM_REG           %08" PRIx32 "\n",
        R4300::state.hwreg.DPC_TMEM_REG);
}

static void ShowDPSpanRegisters(void) {
}

static void ShowMIRegisters(void) {
    ImGui::Text("MI_MODE_REG            %08" PRIx32 "\n",
        R4300::state.hwreg.MI_MODE_REG);
    ImGui::Text("MI_VERSION_REG         %08" PRIx32 "\n",
        R4300::state.hwreg.MI_VERSION_REG);
    ImGui::Text("MI_INTR_REG            %08" PRIx32 "\n",
        R4300::state.hwreg.MI_INTR_REG);
    ImGui::Text("MI_INTR_MASK_REG       %08" PRIx32 "\n",
        R4300::state.hwreg.MI_INTR_MASK_REG);
}

static void ShowVIRegisters(void) {
    if (ImGui::TreeNode(
            "VI_CONTROL_REG", "VI_CONTROL_REG         %08" PRIx32 "\n",
            R4300::state.hwreg.VI_CONTROL_REG)) {
        bool serrate = (R4300::state.hwreg.VI_CONTROL_REG & VI_CONTROL_SERRATE) != 0;
        u32 color_depth =
            (R4300::state.hwreg.VI_CONTROL_REG >> VI_CONTROL_COLOR_DEPTH_SHIFT) &
            VI_CONTROL_COLOR_DEPTH_MASK;
        ImGui::Text("serrate: %s", serrate ? "on" : "off");
        ImGui::Text("color depth: %s",
            color_depth == VI_CONTROL_COLOR_DEPTH_BLANK ? "blank" :
            color_depth == VI_CONTROL_COLOR_DEPTH_16BIT ? "16bit" :
            color_depth == VI_CONTROL_COLOR_DEPTH_32BIT ? "32bit" : "invalid");
        ImGui::TreePop();
    }

    ImGui::SetCursorPosX(ImGui::GetTreeNodeToLabelSpacing());
    ImGui::Text("VI_DRAM_ADDR_REG       %08" PRIx32 "\n",
        R4300::state.hwreg.VI_DRAM_ADDR_REG);
    ImGui::SetCursorPosX(ImGui::GetTreeNodeToLabelSpacing());
    ImGui::Text("VI_WIDTH_REG           %08" PRIx32 "\n",
        R4300::state.hwreg.VI_WIDTH_REG);
    ImGui::SetCursorPosX(ImGui::GetTreeNodeToLabelSpacing());
    ImGui::Text("VI_INTR_REG            %08" PRIx32 "\n",
        R4300::state.hwreg.VI_INTR_REG);
    ImGui::SetCursorPosX(ImGui::GetTreeNodeToLabelSpacing());
    ImGui::Text("VI_CURRENT_REG         %08" PRIx32 "\n",
        R4300::state.hwreg.VI_CURRENT_REG);
    ImGui::SetCursorPosX(ImGui::GetTreeNodeToLabelSpacing());
    ImGui::Text("VI_BURST_REG           %08" PRIx32 "\n",
        R4300::state.hwreg.VI_BURST_REG);
    if (ImGui::TreeNode(
            "VI_V_SYNC_REG", "VI_V_SYNC_REG          %08" PRIx32 "\n",
            R4300::state.hwreg.VI_V_SYNC_REG)) {
        ImGui::Text("lines per frame:  %" PRIu32 "\n",
            R4300::state.hwreg.VI_V_SYNC_REG);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode(
            "VI_H_SYNC_REG", "VI_H_SYNC_REG          %08" PRIx32 "\n",
            R4300::state.hwreg.VI_H_SYNC_REG)) {
        ImGui::Text("line duration:    %f\n",
            (float)(R4300::state.hwreg.VI_H_SYNC_REG & 0xfffu) / 4.);
        ImGui::TreePop();
    }
    ImGui::SetCursorPosX(ImGui::GetTreeNodeToLabelSpacing());
    ImGui::Text("VI_LEAP_REG            %08" PRIx32 "\n",
        R4300::state.hwreg.VI_LEAP_REG);
    if (ImGui::TreeNode(
            "VI_H_START_REG", "VI_H_START_REG         %08" PRIx32 "\n",
            R4300::state.hwreg.VI_H_START_REG)) {
        ImGui::Text("horizontal start: %" PRIu32 "\n",
            (R4300::state.hwreg.VI_H_START_REG >> 16) & 0x3ffu);
        ImGui::Text("horizontal end:   %" PRIu32 "\n",
            R4300::state.hwreg.VI_H_START_REG & 0x3ffu);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode(
            "VI_V_START_REG", "VI_V_START_REG         %08" PRIx32 "\n",
            R4300::state.hwreg.VI_V_START_REG)) {
        ImGui::Text("vertical start:   %" PRIu32 "\n",
            (R4300::state.hwreg.VI_V_START_REG >> 16) & 0x3ffu);
        ImGui::Text("vertical end:     %" PRIu32 "\n",
            R4300::state.hwreg.VI_V_START_REG & 0x3ffu);
        ImGui::TreePop();
    }
    ImGui::SetCursorPosX(ImGui::GetTreeNodeToLabelSpacing());
    ImGui::Text("VI_V_BURST_REG         %08" PRIx32 "\n",
        R4300::state.hwreg.VI_V_BURST_REG);
    if (ImGui::TreeNode(
            "VI_X_SCALE_REG", "VI_X_SCALE_REG         %08" PRIx32 "\n",
            R4300::state.hwreg.VI_X_SCALE_REG)) {
        ImGui::Text("horizontal scale: %f\n",
            (float)(R4300::state.hwreg.VI_X_SCALE_REG & 0xfffu) / 1024.);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode(
            "VI_Y_SCALE_REG", "VI_Y_SCALE_REG         %08" PRIx32 "\n",
            R4300::state.hwreg.VI_Y_SCALE_REG)) {
        ImGui::Text("vertical scale:   %f\n",
            (float)(R4300::state.hwreg.VI_Y_SCALE_REG & 0xfffu) / 1024.);
        ImGui::TreePop();
    }

    ImGui::Separator();
    ImGui::Text("vi_NextIntr            %lu\n",
        R4300::state.hwreg.vi_NextIntr);
    ImGui::Text("vi_IntrInterval        %lu\n",
        R4300::state.hwreg.vi_IntrInterval);
    ImGui::Text("vi_LastCycleCount      %lu\n",
        R4300::state.hwreg.vi_LastCycleCount);
    ImGui::Text("vi_CyclesPerLine       %lu\n",
        R4300::state.hwreg.vi_CyclesPerLine);

    ImGui::Separator();
    ImGui::Text("lines per frame:  %" PRIu32 "\n",
        R4300::state.hwreg.VI_V_SYNC_REG);
    ImGui::Text("line duration:    %f\n",
        (float)(R4300::state.hwreg.VI_H_SYNC_REG & 0xfffu) / 4.);
    ImGui::Text("horizontal start: %" PRIu32 "\n",
        (R4300::state.hwreg.VI_H_START_REG >> 16) & 0x3ffu);
    ImGui::Text("horizontal end:   %" PRIu32 "\n",
        R4300::state.hwreg.VI_H_START_REG & 0x3ffu);
    ImGui::Text("vertical start:   %" PRIu32 "\n",
        (R4300::state.hwreg.VI_V_START_REG >> 16) & 0x3ffu);
    ImGui::Text("vertical end:     %" PRIu32 "\n",
        R4300::state.hwreg.VI_V_START_REG & 0x3ffu);
    ImGui::Text("horizontal scale: %f\n",
        (float)(R4300::state.hwreg.VI_X_SCALE_REG & 0xfffu) / 1024.);
    ImGui::Text("vertical scale:   %f\n",
        (float)(R4300::state.hwreg.VI_Y_SCALE_REG & 0xfffu) / 1024.);
}

static void ShowAIRegisters(void) {
    ImGui::Text("AI_DRAM_ADDR_REG       %08" PRIx32 "\n",
        R4300::state.hwreg.AI_DRAM_ADDR_REG);
    ImGui::Text("AI_LEN_REG             %08" PRIx32 "\n",
        R4300::state.hwreg.AI_LEN_REG);
    ImGui::Text("AI_CONTROL_REG         %08" PRIx32 "\n",
        R4300::state.hwreg.AI_CONTROL_REG);
    ImGui::Text("AI_STATUS_REG          %08" PRIx32 "\n",
        R4300::state.hwreg.AI_STATUS_REG);
    ImGui::Text("AI_DACRATE_REG         %08" PRIx32 "\n",
        R4300::state.hwreg.AI_DACRATE_REG);
    ImGui::Text("AI_BITRATE_REG         %08" PRIx32 "\n",
        R4300::state.hwreg.AI_BITRATE_REG);
}

static void ShowPIRegisters(void) {
    ImGui::Text("PI_DRAM_ADDR_REG       %08" PRIx32 "\n",
        R4300::state.hwreg.PI_DRAM_ADDR_REG);
    ImGui::Text("PI_CART_ADDR_REG       %08" PRIx32 "\n",
        R4300::state.hwreg.PI_CART_ADDR_REG);
    ImGui::Text("PI_RD_LEN_REG          %08" PRIx32 "\n",
        R4300::state.hwreg.PI_RD_LEN_REG);
    ImGui::Text("PI_WR_LEN_REG          %08" PRIx32 "\n",
        R4300::state.hwreg.PI_WR_LEN_REG);
    ImGui::Text("PI_STATUS_REG          %08" PRIx32 "\n",
        R4300::state.hwreg.PI_STATUS_REG);
    ImGui::Text("PI_BSD_DOM1_LAT_REG    %08" PRIx32 "\n",
        R4300::state.hwreg.PI_BSD_DOM1_LAT_REG);
    ImGui::Text("PI_BSD_DOM1_PWD_REG    %08" PRIx32 "\n",
        R4300::state.hwreg.PI_BSD_DOM1_PWD_REG);
    ImGui::Text("PI_BSD_DOM1_PGS_REG    %08" PRIx32 "\n",
        R4300::state.hwreg.PI_BSD_DOM1_PGS_REG);
    ImGui::Text("PI_BSD_DOM1_RLS_REG    %08" PRIx32 "\n",
        R4300::state.hwreg.PI_BSD_DOM1_RLS_REG);
    ImGui::Text("PI_BSD_DOM2_LAT_REG    %08" PRIx32 "\n",
        R4300::state.hwreg.PI_BSD_DOM2_LAT_REG);
    ImGui::Text("PI_BSD_DOM2_PWD_REG    %08" PRIx32 "\n",
        R4300::state.hwreg.PI_BSD_DOM2_PWD_REG);
    ImGui::Text("PI_BSD_DOM2_PGS_REG    %08" PRIx32 "\n",
        R4300::state.hwreg.PI_BSD_DOM2_PGS_REG);
    ImGui::Text("PI_BSD_DOM2_RLS_REG    %08" PRIx32 "\n",
        R4300::state.hwreg.PI_BSD_DOM2_RLS_REG);
}

static void ShowRIRegisters(void) {
    ImGui::Text("RI_MODE_REG            %08" PRIx32 "\n",
        R4300::state.hwreg.RI_MODE_REG);
    ImGui::Text("RI_CONFIG_REG          %08" PRIx32 "\n",
        R4300::state.hwreg.RI_CONFIG_REG);
    ImGui::Text("RI_SELECT_REG          %08" PRIx32 "\n",
        R4300::state.hwreg.RI_SELECT_REG);
    ImGui::Text("RI_REFRESH_REG         %08" PRIx32 "\n",
        R4300::state.hwreg.RI_REFRESH_REG);
    ImGui::Text("RI_LATENCY_REG         %08" PRIx32 "\n",
        R4300::state.hwreg.RI_LATENCY_REG);
    ImGui::Text("RI_RERROR_REG          %08" PRIx32 "\n",
        R4300::state.hwreg.RI_RERROR_REG);
}

static void ShowSIRegisters(void) {
    ImGui::Text("SI_DRAM_ADDR_REG       %08" PRIx32 "\n",
        R4300::state.hwreg.SI_DRAM_ADDR_REG);
    ImGui::Text("SI_STATUS_REG          %08" PRIx32 "\n",
        R4300::state.hwreg.SI_STATUS_REG);
}

static void ShowPIFInformation(void) {
}

static void ShowCartInformation(void) {
}

struct Module {
    char const *Name;
    int Label;
    void (*ShowInformation)();
};

static Module Modules[] = {
    { "Analytics",      -1,                     ShowAnalytics },
    { "CPU",            Debugger::CPU,          ShowCpuRegisters },
    { "CPU::COP0",      Debugger::COP0,         ShowCpuCop0Registers },
    { "CPU::COP1",      Debugger::COP1,         ShowCpuCop1Registers },
    { "CPU::TLB",       Debugger::TLB,          ShowCpuTLB },
    { "RSP",            Debugger::RSP,          ShowRspRegisters },
    { "RSP::COP2",      Debugger::RSP,          ShowRspCop2Registers },
    { "RDP",            Debugger::RDP,          ShowRdpInformation },
    { "HW::RdRam",      Debugger::RdRam,        ShowRdRamRegisters },
    { "HW::SP",         Debugger::SP,           ShowSPRegisters },
    { "HW::DPCommand",  Debugger::DPCommand,    ShowDPCommandRegisters },
    { "HW::DPSpan",     Debugger::DPSpan,       ShowDPSpanRegisters },
    { "HW::MI",         Debugger::MI,           ShowMIRegisters },
    { "HW::VI",         Debugger::VI,           ShowVIRegisters },
    { "HW::AI",         Debugger::AI,           ShowAIRegisters },
    { "HW::PI",         Debugger::PI,           ShowPIRegisters },
    { "HW::RI",         Debugger::RI,           ShowRIRegisters },
    { "HW::SI",         Debugger::SI,           ShowSIRegisters },
    { "HW::PIF",        Debugger::PIF,          ShowPIFInformation },
    { "HW::Cart"    ,   Debugger::Cart,         ShowCartInformation },
};

static void ShowScreen(bool *show_screen) {
    size_t width;
    size_t height;
    GLuint texture;

    if (getVideoImage(&width, &height, &texture)) {
        ImGui::SetNextWindowSize(ImVec2(width + 15, height + 35));
        ImGui::Begin("Screen", show_screen);
        ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddImage(
                (void *)(unsigned long)texture, pos,
                ImVec2(pos.x + width, pos.y + height),
                ImVec2(0, 0), ImVec2(1, 1));
        ImGui::End();
    } else {
        ImGui::Begin("Screen", show_screen);
        ImGui::Text("Framebuffer invalid");
        ImGui::End();
    }
}

static void ShowLogConfig(bool *show_log_config) {
    ImGui::Begin("Log Config", show_log_config);
    for (int label = 0; label < Debugger::LabelCount; label++) {
        int verb = debugger::debugger.verbosity[label];
        float col[3] = {
            (float)debugger::debugger.color[label].r / 256.f,
            (float)debugger::debugger.color[label].g / 256.f,
            (float)debugger::debugger.color[label].b / 256.f, };
        ImGui::ColorEdit3("Log color", col,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::Combo(debugger::LabelName[label],
            &verb, "none\0error\0warn\0info\0debug\0\0");
        debugger::debugger.verbosity[label] = (Debugger::Verbosity)verb;
        debugger::debugger.color[label].r = (u8)(col[0] * 256.f);
        debugger::debugger.color[label].g = (u8)(col[1] * 256.f);
        debugger::debugger.color[label].b = (u8)(col[2] * 256.f);
    }
    ImGui::End();
}

static void ShowDisassembler(bool *show_disassembler) {
    ImGui::Begin("Disassembler", show_disassembler);
    if (ImGui::BeginTabBar("Memory", 0)) {
        if (ImGui::BeginTabItem("DRAM")) {
            dramDisassembler.DrawContents(
                assembly::cpu::disassemble,
                R4300::state.dram, sizeof(R4300::state.dram),
                R4300::state.reg.pc,
                0x0,
                true);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("IMEM")) {
            imemDisassembler.DrawContents(
                assembly::rsp::disassemble,
                R4300::state.imem, sizeof(R4300::state.imem),
                R4300::state.rspreg.pc,
                0x04001000lu);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("ROM")) {
            romDisassembler.DrawContents(
                assembly::cpu::disassemble,
                R4300::state.rom, 0x1000,
                R4300::state.reg.pc,
                0x10000000lu,
                true);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

static void ShowTrace(bool *show_trace) {
    ImGui::Begin("Trace", show_trace);
    if (ImGui::Button("Clear traces")) {
        debugger::debugger.cpuTrace.reset();
        debugger::debugger.rspTrace.reset();
    }
    if (ImGui::BeginTabBar("Trace", 0)) {
        if (ImGui::BeginTabItem("Cpu")) {
            if (core::halted()) {
                cpuTrace.DrawContents("cpu", assembly::cpu::disassemble);
            } else {
                ImGui::Text("Cpu is running...");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Rsp")) {
            if (core::halted()) {
                rspTrace.DrawContents("rsp", assembly::rsp::disassemble);
            } else {
                ImGui::Text("Rsp is running...");
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

static void ShowBreakpoints(bool *show_breakpoints) {
    static char addr_input_buf[32];
    bool added = false;
    bool removed = false;
    u64 removed_addr = 0;

    ImGui::Begin("Breakpoints", show_breakpoints);
    added |= ImGui::InputText("##addr", addr_input_buf, 32,
        ImGuiInputTextFlags_CharsHexadecimal |
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    added |= ImGui::Button("Add");

    if (added) {
        u64 addr;
        if (sscanf(addr_input_buf, "%" PRIx64 "X", &addr) == 1) {
            debugger::debugger.setBreakpoint(addr);
        }
    }

    ImGui::BeginChild("BreakpointList");
    auto it = debugger::debugger.breakpointsBegin();
    for (; it != debugger::debugger.breakpointsEnd(); it++) {
        ImGui::PushID(it->first);
        ImGui::Checkbox("", &it->second->enabled);
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            removed = true;
            removed_addr = it->first;
        }
        ImGui::SameLine();
        ImGui::Text("%08lx", it->first);
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();

    if (removed) {
        debugger::debugger.unsetBreakpoint(removed_addr);
    }
}

static void ShowDebuggerWindow(void) {
    static bool show_screen = true;
    static bool show_log_config = false;
    static bool show_disassembler = true;
    static bool show_trace = false;
    static bool show_breakpoints = false;

    if (show_screen) ShowScreen(&show_screen);
    if (show_log_config) ShowLogConfig(&show_log_config);
    if (show_disassembler) ShowDisassembler(&show_disassembler);
    if (show_trace) ShowTrace(&show_trace);
    if (show_breakpoints) ShowBreakpoints(&show_breakpoints);

    if (ImGui::Begin("Debugger", NULL, ImGuiWindowFlags_MenuBar)) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Load")) {}
                if (ImGui::BeginMenu("Export")) {
                    if (ImGui::MenuItem("cpu trace")) {}
                    if (ImGui::MenuItem("rsp trace")) {}
                    if (ImGui::MenuItem("dram disassembly")) {}
                    if (ImGui::MenuItem("imem disassembly")) {}
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Save screen")) {
                    exportAsPNG("screen.png");
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Screen", NULL, &show_screen)) {}
                if (ImGui::MenuItem("Disassembler", NULL, &show_disassembler)) {}
                if (ImGui::MenuItem("Trace", NULL, &show_trace)) {}
                if (ImGui::MenuItem("Breakpoints", NULL, &show_breakpoints)) {}
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Options")) {
                if (ImGui::MenuItem("Log", NULL, &show_log_config)) {}
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        ImGui::Text("Real time: %lums (%lu)\n",
            R4300::state.cycles / 93750lu, R4300::state.cycles);

        if (core::halted()) {
            ImGui::Text("Machine halt reason: '%s'\n",
                core::halted_reason().c_str());
            if (ImGui::Button("Reset")) { core::reset(); }
            ImGui::SameLine();
            if (ImGui::Button("Continue")) { core::resume(); }
            ImGui::SameLine();
            if (ImGui::Button("Step")) { core::step(); }
        } else {
            if (ImGui::Button("Halt")) {
                core::halt("Interrupted by user");
            }
        }

        static int selected = 0;
        ImGui::Separator();
        ImGui::BeginChild("module select", ImVec2(150, 0), true);
        for (int i = 0; i < IM_ARRAYSIZE(Modules); i++) {
            if (ImGui::Selectable(Modules[i].Name, selected == i)) {
                selected = i;
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginChild("module view",
            ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
        ImGui::Text("%s", Modules[selected].Name);
        if (Modules[selected].Label < Debugger::LabelCount &&
            Modules[selected].Label >= 0) {
            Debugger::Label label = (Debugger::Label)Modules[selected].Label;
            int verb = debugger::debugger.verbosity[label];
            float col[3] = {
                (float)debugger::debugger.color[label].r / 256.f,
                (float)debugger::debugger.color[label].g / 256.f,
                (float)debugger::debugger.color[label].b / 256.f, };
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
            ImGui::ColorEdit3("Log color", col,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 125);
            ImGui::SetNextItemWidth(100);
            ImGui::Combo("", &verb, "none\0error\0warn\0info\0debug\0\0");
            debugger::debugger.verbosity[label] = (Debugger::Verbosity)verb;
            debugger::debugger.color[label].r = (u8)(col[0] * 256.f);
            debugger::debugger.color[label].g = (u8)(col[1] * 256.f);
            debugger::debugger.color[label].b = (u8)(col[2] * 256.f);
        }
        ImGui::Separator();
        ImGui::BeginChild("module info");
        Modules[selected].ShowInformation();
        ImGui::EndChild();
        ImGui::EndChild();

        ImGui::End();
    }
}

/**
 * Capture key callbacks, interpret them as game inputs.
 */
void joyKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    unsigned keyval;
    switch (action) {
    case GLFW_PRESS: keyval = 1; break;
    case GLFW_RELEASE: keyval = 0; break;
    default: return;
    }

    switch (key) {
    case GLFW_KEY_A:     R4300::state.hwreg.buttons.A       = keyval; break;
    case GLFW_KEY_B:     R4300::state.hwreg.buttons.B       = keyval; break;
    case GLFW_KEY_Z:     R4300::state.hwreg.buttons.Z       = keyval; break;
    case GLFW_KEY_SPACE: R4300::state.hwreg.buttons.start   = keyval; break;
    case GLFW_KEY_UP:    R4300::state.hwreg.buttons.up      = keyval; break;
    case GLFW_KEY_DOWN:  R4300::state.hwreg.buttons.down    = keyval; break;
    case GLFW_KEY_LEFT:  R4300::state.hwreg.buttons.left    = keyval; break;
    case GLFW_KEY_RIGHT: R4300::state.hwreg.buttons.right   = keyval; break;
    case GLFW_KEY_L:     R4300::state.hwreg.buttons.L       = keyval; break;
    case GLFW_KEY_R:     R4300::state.hwreg.buttons.R       = keyval; break;
    // case : R4300::state.hwreg.buttons.C_up    = keyval; break;
    // case : R4300::state.hwreg.buttons.C_down  = keyval; break;
    // case : R4300::state.hwreg.buttons.C_left  = keyval; break;
    // case : R4300::state.hwreg.buttons.C_right = keyval; break;
    default: break;
    }

    // R4300::state.hwreg.buttons.x = 0;
    // R4300::state.hwreg.buttons.y = 0;
}

int startGui()
{
    // Initialize the machine state.
    R4300::state.reset();
    startTime = std::chrono::steady_clock::now();
    startCycles = 0;
    startRecompilerCycles = 0;
    startRecompilerRequests = 0;
    startRecompilerCacheClears = 0;

    // Start interpreter thread.
    core::start();

    // Setup window
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit())
        return 1;

    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    // Create window with graphics context
    GLFWwindow* window = glfwCreateWindow(
        1280, 720, "Nintendo 64 Emulation", NULL, NULL);
    if (window == NULL)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Initialize OpenGL loader
    bool err = glewInit() != GLEW_OK;
    if (err) {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return 1;
    }

    glfwSetKeyCallback(window, joyKeyCallback);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer bindings
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'misc/fonts/README.txt' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    ImFont *font = io.Fonts->AddFontFromFileTTF("src/gui/VeraMono.ttf", 13);
    if (!font)
        return 1;
    // io.Fonts->GetTexDataAsRGBA32();

    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != NULL);

    // Our state
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Show the main debugger control window.
        // Displays Continue, Halt commands and register values.
        ShowDebuggerWindow();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    core::stop();
    return 0;
}
