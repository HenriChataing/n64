
#include <cinttypes>
#include <cstdio>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_disassembler.h"
#include "imgui_trace.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <debugger.h>
#include <r4300/state.h>
#include <r4300/eval.h>
#include <graphics.h>

static Disassembler imemDisassembler(12);
static Disassembler dramDisassembler(22);
static Disassembler romDisassembler(12);
static Trace cpuTrace(&debugger.cpuTrace);
static Trace rspTrace(&debugger.rspTrace);


static void glfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

static void displayCpuCp0Registers(void) {
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

static void displayCpuCp1Registers(void) {
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

static void displayCpuTLB(void) {
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

static void displayRspCp2Registers(void) {
    ImGui::Text("vco     %04" PRIx16, R4300::state.rspreg.vco);
    ImGui::Text("vcc     %04" PRIx16, R4300::state.rspreg.vcc);
    ImGui::Text("vce     %02" PRIx8,  R4300::state.rspreg.vce);
    ImGui::Text("vacc   ");
    for (unsigned e = 0; e < 8; e++) {
        ImGui::SameLine();
        ImGui::Text("%04" PRIx64,
            (R4300::state.rspreg.vacc[e] >> 32) & UINT64_C(0xffff));
    }
    ImGui::Text("       ");
    for (unsigned e = 0; e < 8; e++) {
        ImGui::SameLine();
        ImGui::Text("%04" PRIx64,
            (R4300::state.rspreg.vacc[e] >> 16) & UINT64_C(0xffff));
    }
    ImGui::Text("       ");
    for (unsigned e = 0; e < 8; e++) {
        ImGui::SameLine();
        ImGui::Text("%04" PRIx64,
            R4300::state.rspreg.vacc[e] & UINT64_C(0xffff));
    }
    for (unsigned int nr = 0; nr < 32; nr++) {
        ImGui::Text("vr%-2u   ", nr);
        for (unsigned e = 0; e < 8; e++) {
            ImGui::SameLine();
            ImGui::Text("%04x", R4300::state.rspreg.vr[nr].h[e]);
        }
    }
}

static void displayHWRegisters(void) {
    if (ImGui::BeginTabBar("HWRegisters", 0))
    {
        if (ImGui::BeginTabItem("RdRam")) {
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
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("SP")) {
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
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("DPCommand")) {
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
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("DPSpan")) {
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("MI")) {
            ImGui::Text("MI_MODE_REG            %08" PRIx32 "\n",
                R4300::state.hwreg.MI_MODE_REG);
            ImGui::Text("MI_VERSION_REG         %08" PRIx32 "\n",
                R4300::state.hwreg.MI_VERSION_REG);
            ImGui::Text("MI_INTR_REG            %08" PRIx32 "\n",
                R4300::state.hwreg.MI_INTR_REG);
            ImGui::Text("MI_INTR_MASK_REG       %08" PRIx32 "\n",
                R4300::state.hwreg.MI_INTR_MASK_REG);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("VI")) {
            ImGui::Text("VI_CONTROL_REG         %08" PRIx32 "\n",
                R4300::state.hwreg.VI_CONTROL_REG);
            ImGui::Text("VI_DRAM_ADDR_REG       %08" PRIx32 "\n",
                R4300::state.hwreg.VI_DRAM_ADDR_REG);
            ImGui::Text("VI_WIDTH_REG           %08" PRIx32 "\n",
                R4300::state.hwreg.VI_WIDTH_REG);
            ImGui::Text("VI_INTR_REG            %08" PRIx32 "\n",
                R4300::state.hwreg.VI_INTR_REG);
            ImGui::Text("VI_CURRENT_REG         %08" PRIx32 "\n",
                R4300::state.hwreg.VI_CURRENT_REG);
            ImGui::Text("VI_BURST_REG           %08" PRIx32 "\n",
                R4300::state.hwreg.VI_BURST_REG);
            ImGui::Text("VI_V_SYNC_REG          %08" PRIx32 "\n",
                R4300::state.hwreg.VI_V_SYNC_REG);
            ImGui::Text("VI_H_SYNC_REG          %08" PRIx32 "\n",
                R4300::state.hwreg.VI_H_SYNC_REG);
            ImGui::Text("VI_LEAP_REG            %08" PRIx32 "\n",
                R4300::state.hwreg.VI_LEAP_REG);
            ImGui::Text("VI_H_START_REG         %08" PRIx32 "\n",
                R4300::state.hwreg.VI_H_START_REG);
            ImGui::Text("VI_V_START_REG         %08" PRIx32 "\n",
                R4300::state.hwreg.VI_V_START_REG);
            ImGui::Text("VI_V_BURST_REG         %08" PRIx32 "\n",
                R4300::state.hwreg.VI_V_BURST_REG);
            ImGui::Text("VI_X_SCALE_REG         %08" PRIx32 "\n",
                R4300::state.hwreg.VI_X_SCALE_REG);
            ImGui::Text("VI_Y_SCALE_REG         %08" PRIx32 "\n",
                R4300::state.hwreg.VI_Y_SCALE_REG);
            ImGui::Text("vi_NextIntr            %lu\n",
                R4300::state.hwreg.vi_NextIntr);
            ImGui::Text("vi_IntrInterval        %lu\n",
                R4300::state.hwreg.vi_IntrInterval);
            ImGui::Text("vi_LastCycleCount      %lu\n",
                R4300::state.hwreg.vi_LastCycleCount);
            ImGui::Text("vi_CyclesPerLine       %lu\n",
                R4300::state.hwreg.vi_CyclesPerLine);

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
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("AI")) {
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
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("PI")) {
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
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("RI")) {
            ImGui::Text("RI_MODE_REG            %08" PRIx32 "\n",
                R4300::state.hwreg.RI_MODE_REG);
            ImGui::Text("RI_CONFIG_REG          %08" PRIx32 "\n",
                R4300::state.hwreg.RI_CONFIG_REG);
            ImGui::Text("RI_CURRENT_LOAD_REG    %08" PRIx32 "\n",
                R4300::state.hwreg.RI_CURRENT_LOAD_REG);
            ImGui::Text("RI_SELECT_REG          %08" PRIx32 "\n",
                R4300::state.hwreg.RI_SELECT_REG);
            ImGui::Text("RI_REFRESH_REG         %08" PRIx32 "\n",
                R4300::state.hwreg.RI_REFRESH_REG);
            ImGui::Text("RI_LATENCY_REG         %08" PRIx32 "\n",
                R4300::state.hwreg.RI_LATENCY_REG);
            ImGui::Text("RI_RERROR_REG          %08" PRIx32 "\n",
                R4300::state.hwreg.RI_RERROR_REG);
            ImGui::Text("RI_WERROR_REG          %08" PRIx32 "\n",
                R4300::state.hwreg.RI_WERROR_REG);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("SI")) {
            ImGui::Text("SI_DRAM_ADDR_REG       %08" PRIx32 "\n",
                R4300::state.hwreg.SI_DRAM_ADDR_REG);
            ImGui::Text("SI_STATUS_REG          %08" PRIx32 "\n",
                R4300::state.hwreg.SI_STATUS_REG);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
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

    std::cerr << "key: " << key;
    std::cerr << (keyval ? " down" : " up") << std::endl;

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
    R4300::state.boot();
    // Start interpreter thread.
    debugger.startInterpreter();

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
    ImGuiIO& io = ImGui::GetIO(); (void)io;
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
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != NULL);

    // Our state
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // mem_edit.DrawWindow("Memory Editor", R4300::state.dmem, sizeof(R4300::state.dmem));

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
        {
            ImGui::Begin("Debugger");
            // CPU freq is 93.75 MHz
            ImGui::Text("Real time: %lums\n", R4300::state.cycles / 93750lu);
            if (debugger.halted) {
                ImGui::Text("Machine halt reason: '%s'\n",
                    debugger.haltedReason.c_str());
                if (ImGui::Button("Continue")) {
                    debugger.resume();
                }
                ImGui::SameLine();
                if (ImGui::Button("Step")) {
                    debugger.step();
                }
                if (ImGui::Button("Save Screen")) {
                    exportAsPNG("screen.png");
                }
            } else {
                if (ImGui::Button("Halt")) {
                    debugger.halt("Interrupted by user");
                }
            }
            if (ImGui::BeginTabBar("Registers", 0))
            {
                if (ImGui::BeginTabItem("Cpu")) {
                    ImGui::Text("pc       %016" PRIx64 "\n", R4300::state.reg.pc);
                    for (unsigned int i = 0; i < 32; i+=2) {
                        ImGui::Text("%-8.8s %016" PRIx64 "  %-8.8s %016" PRIx64 "\n",
                            Mips::CPU::getRegisterName(i), R4300::state.reg.gpr[i],
                            Mips::CPU::getRegisterName(i + 1), R4300::state.reg.gpr[i + 1]);
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Cpu::Cp0")) {
                    displayCpuCp0Registers();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Cpu::Cp1")) {
                    displayCpuCp1Registers();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Cpu::TLB")) {
                    displayCpuTLB();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Rsp")) {
                    ImGui::Text("pc       %016" PRIx64 "\n", R4300::state.rspreg.pc);
                    for (unsigned int i = 0; i < 32; i+=2) {
                        ImGui::Text("%-8.8s %016" PRIx64 "  %-8.8s %016" PRIx64 "\n",
                            Mips::CPU::getRegisterName(i), R4300::state.rspreg.gpr[i],
                            Mips::CPU::getRegisterName(i + 1), R4300::state.rspreg.gpr[i + 1]);
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Rsp::Cp2")) {
                    displayRspCp2Registers();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("HW")) {
                    displayHWRegisters();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();
        }

        // Show the screen rendering.
        // ImGuiCond_FirstUseEver
        {
            size_t width;
            size_t height;
            GLuint texture;

            if (getVideoImage(&width, &height, &texture)) {
                ImGui::SetNextWindowSize(ImVec2(width + 15, height + 35));
                ImGui::Begin("Screen");
                ImVec2 pos = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddImage(
                        (void *)(unsigned long)texture, pos,
                        ImVec2(pos.x + width, pos.y + height),
                        ImVec2(0, 0), ImVec2(1, 1));
                ImGui::End();
            } else {
                ImGui::Begin("Screen");
                ImGui::Text("Framebuffer invalid");
                ImGui::End();
            }
        }

        // Show the memory disassembler window.
        // Displays disassembly of main RdRam and IMEM.
        {
            ImGui::Begin("Disassembler");
            if (ImGui::BeginTabBar("Memory", 0)) {
                if (ImGui::BeginTabItem("DRAM")) {
                    dramDisassembler.DrawContents(
                        Mips::CPU::disas,
                        R4300::state.dram, sizeof(R4300::state.dram),
                        R4300::state.reg.pc);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("IMEM")) {
                    imemDisassembler.DrawContents(
                        Mips::RSP::disas,
                        R4300::state.imem, sizeof(R4300::state.imem),
                        R4300::state.rspreg.pc);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("ROM")) {
                    romDisassembler.DrawContents(
                        Mips::CPU::disas,
                        R4300::state.rom, 0x1000,
                        R4300::state.reg.pc);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::End();
        }

        // Show the execution trace.
        // Displays traces for the CPU and the RSP.
        {
            ImGui::Begin("Trace");
            if (ImGui::Button("Clear traces")) {
                debugger.cpuTrace.reset();
                debugger.rspTrace.reset();
            }
            if (ImGui::BeginTabBar("Trace", 0)) {
                if (ImGui::BeginTabItem("Cpu")) {
                    if (debugger.halted) {
                        cpuTrace.DrawContents("cpu", Mips::CPU::disas);
                    } else {
                        ImGui::Text("Cpu is running...");
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Rsp")) {
                    if (debugger.halted) {
                        rspTrace.DrawContents("rsp", Mips::RSP::disas);
                    } else {
                        ImGui::Text("Rsp is running...");
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::End();
        }

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
    std::cout << "exiting main loop" << std::endl;

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    debugger.stopInterpreter();
    return 0;
}
