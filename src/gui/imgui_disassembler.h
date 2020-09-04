
// Mini memory editor for Dear ImGui (to embed in your game/tools)
// Animated GIF: https://twitter.com/ocornut/status/894242704317530112
// Get latest version at http://www.github.com/ocornut/imgui_club
//
// Right-click anywhere to access the Options menu!
// You can adjust the keyboard repeat delay/rate in ImGuiIO.
// The code assume a mono-space font for simplicity! If you don't use the default font, use ImGui::PushFont()/PopFont() to switch to a mono-space font before caling this.
//
// Usage:
//   static MemoryEditor mem_edit_1;                                            // store your state somewhere
//   mem_edit_1.DrawWindow("Memory Editor", mem_block, mem_block_size, 0x0000); // create a window and draw memory editor (if you already have a window, use DrawContents())
//
// Usage:
//   static MemoryEditor mem_edit_2;
//   ImGui::Begin("MyWindow")
//   mem_edit_2.DrawContents(this, sizeof(*this), (size_t)this);
//   ImGui::End();
//
// Changelog:
// - v0.10: initial version
// - v0.11: always refresh active text input with the latest byte from source memory if it's not being edited.
// - v0.12: added OptMidRowsCount to allow extra spacing every XX rows.
// - v0.13: added optional ReadFn/WriteFn handlers to access memory via a function. various warning fixes for 64-bits.
// - v0.14: added GotoAddr member, added GotoAddrAndHighlight() and highlighting. fixed minor scrollbar glitch when resizing.
// - v0.15: added maximum window width. minor optimization.
// - v0.16: added OptGreyOutZeroes option. various sizing fixes when resizing using the "Rows" drag.
// - v0.17: added HighlightFn handler for optional non-contiguous highlighting.
// - v0.18: fixes for displaying 64-bits addresses, fixed mouse click gaps introduced in recent changes, cursor tracking scrolling fixes.
// - v0.19: fixed auto-focus of next byte leaving WantCaptureKeyboard=false for one frame. we now capture the keyboard during that transition.
// - v0.20: added options menu. added OptShowAscii checkbox. added optional HexII display. split Draw() in DrawWindow()/DrawContents(). fixing glyph width. refactoring/cleaning code.
// - v0.21: fixes for using DrawContents() in our own window. fixed HexII to actually be useful and not on the wrong side.
// - v0.22: clicking Ascii view select the byte in the Hex view. Ascii view highlight selection.
// - v0.23: fixed right-arrow triggering a byte write.
// - v0.24: changed DragInt("Rows" to use a %d data format (which is desirable since imgui 1.61).
// - v0.25: fixed wording: all occurrences of "Rows" renamed to "Columns".
// - v0.26: fixed clicking on hex region
// - v0.30: added data preview for common data types
// - v0.31: added OptUpperCaseHex option to select lower/upper casing display [@samhocevar]
// - v0.32: changed signatures to use void* instead of unsigned char*
// - v0.33: added OptShowOptions option to hide all the interactive option setting.
// - v0.34: binary preview now applies endianess setting [@nicolasnoble]
//
// Todo/Bugs:
// - Arrows are being sent to the InputText() about to disappear which for LeftArrow makes the text cursor appear at position 1 for one frame.
// - Using InputText() is awkward and maybe overkill here, consider implementing something custom.

#pragma once
#include <iomanip>
#include <iostream>
#include <fstream>
#include <stdio.h>      // sprintf, scanf
#include <stdint.h>     // uint8_t, etc.

#include <assembly/disassembler.h>
#include <debugger.h>

#ifdef _MSC_VER
#define _PRISizeT   "I"
#define ImSnprintf  _snprintf
#else
#define _PRISizeT   "z"
#define ImSnprintf  snprintf
#endif

struct Disassembler
{
    typedef unsigned char u8;

    /* Settings */

    unsigned        AddrSize;
    bool            Open;                                   // = true   // set to false when DrawWindow() was closed. ignore if not using DrawWindow().
    int             Cols;                                   // = 16     // number of columns to display.
    bool            OptGreyOutZeroes;                       // = true   // display null/zero bytes using the TextDisabled color.
    bool            OptUpperCaseHex;                        // = true   // display hexadecimal values as "FF" instead of "ff".
    int             OptAddrDigitsCount;                     // = 0      // number of addr digits to display (default calculated based on maximum displayed addr).
    ImU32           HighlightColor;                         //          // background color of highlighted bytes.
    ImU32           ProgramCounterColor;                    //          // background color of program counter.
    u8              (*ReadFn)(const u8* data, size_t off);  // = NULL   // optional handler to read bytes.
    void            (*WriteFn)(u8* data, size_t off, u8 d); // = NULL   // optional handler to write bytes.

    /* Internal state */

    char            AddrInputBuf[32];
    size_t          GotoAddr;
    size_t          HighlightMin, HighlightMax;
    u64             BreakpointAddr;

    Disassembler(unsigned addr_size) : AddrSize(addr_size)
    {
        // Settings
        Open = true;
        Cols = 4;
        OptGreyOutZeroes = true;
        OptUpperCaseHex = true;
        OptAddrDigitsCount = 0;
        HighlightColor      = IM_COL32(255, 255, 255, 50);
        ProgramCounterColor = IM_COL32(255, 0, 0, 50);
        ReadFn = NULL;
        WriteFn = NULL;

        // State/Internals
        memset(AddrInputBuf, 0, sizeof(AddrInputBuf));
        GotoAddr = (size_t)-1;
        HighlightMin = HighlightMax = (size_t)-1;
        BreakpointAddr = 0;
    }

    void GotoAddrAndHighlight(size_t addr_min, size_t addr_max)
    {
        GotoAddr = addr_min;
        HighlightMin = addr_min;
        HighlightMax = addr_max;
    }

    struct Sizes
    {
        int     AddrDigitsCount;
        float   LineHeight;
        float   GlyphWidth;
        float   HexCellWidth;
        float   PosHexStart;
        float   PosHexEnd;
        float   PosInstrStart;
        float   PosInstrEnd;
        float   WindowWidth;
    };

    void CalcSizes(Sizes &s, size_t mem_size, size_t base_display_addr)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        s.AddrDigitsCount = OptAddrDigitsCount;
        if (s.AddrDigitsCount == 0)
            for (size_t n = base_display_addr + mem_size - 1; n > 0; n >>= 4)
                s.AddrDigitsCount++;
        s.LineHeight = ImGui::GetTextLineHeight();
        s.GlyphWidth = ImGui::CalcTextSize("F").x + 1;                  // We assume the font is mono-space
        s.HexCellWidth = (float)(int)(s.GlyphWidth * 2.5f);             // "FF " we include trailing space in the width to easily catch clicks everywhere
        s.PosHexStart = (s.AddrDigitsCount + 2) * s.GlyphWidth;
        s.PosHexEnd = s.PosHexStart + (s.HexCellWidth * Cols);
        s.PosInstrStart = s.PosHexEnd + s.GlyphWidth * 3;
        s.PosInstrEnd = s.PosInstrStart + Cols * s.GlyphWidth;
        s.WindowWidth = s.PosInstrEnd + style.ScrollbarSize +
                        style.WindowPadding.x * 2 + s.GlyphWidth;
    }

    // Memory Editor contents only
    void DrawContents(
        std::string (*disas)(u64 pc, u32 instr),
        void* mem_data_void_ptr,
        size_t mem_size,
        uint64_t program_counter,
        size_t base_display_addr = 0x0000,
        bool enable_breakpoints = false
        )
    {
        u8* mem_data = (u8*)mem_data_void_ptr;
        Sizes s;
        CalcSizes(s, mem_size, base_display_addr);
        ImGuiStyle& style = ImGui::GetStyle();

        // We begin into our scrolling region with the 'ImGuiWindowFlags_NoMove'
        // in order to prevent click from moving the window.
        // This is used as a facility since our main click detection code
        // doesn't assign an ActiveId so the click would normally be caught as
        // a window-move.
        const float height_separator = style.ItemSpacing.y;
        float footer_height = height_separator + ImGui::GetFrameHeightWithSpacing() * 1;
        ImGui::BeginChild("##scrolling", ImVec2(0, -footer_height), false, ImGuiWindowFlags_NoMove);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        const int line_total_count = (int)((mem_size + Cols - 1) / Cols);
        ImGuiListClipper clipper(line_total_count, s.LineHeight);
        // const size_t visible_start_addr = clipper.DisplayStart * Cols;
        // const size_t visible_end_addr = clipper.DisplayEnd * Cols;

        // Key navigation.
        size_t next_highlight_min = HighlightMin;
        size_t next_highlight_max = HighlightMax;
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_UpArrow))) {
            next_highlight_min = next_highlight_max = HighlightMin - Cols;
        }
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_DownArrow))) {
            next_highlight_min = next_highlight_max = HighlightMax + Cols;
        }

        // Draw vertical separator
        ImVec2 window_pos = ImGui::GetWindowPos();
        draw_list->AddLine(ImVec2(window_pos.x + s.PosHexEnd, window_pos.y),
                           ImVec2(window_pos.x + s.PosHexEnd, window_pos.y + 9999),
                           ImGui::GetColorU32(ImGuiCol_Border));

        const char* format_address = OptUpperCaseHex ? "%0*" _PRISizeT "X " : "%0*" _PRISizeT "x ";
        const char* format_range = OptUpperCaseHex ? "Range %0*" _PRISizeT "X..%0*" _PRISizeT "X" : "Range %0*" _PRISizeT "x..%0*" _PRISizeT "x";
        const char* format_byte_space = OptUpperCaseHex ? "%02X " : "%02x ";

        for (int line_i = clipper.DisplayStart; line_i < clipper.DisplayEnd; line_i++) // display only visible lines
        {
            ImGui::BeginGroup();

            // ImVec2 pos = ImGui::GetCursorScreenPos();
            size_t addr = (size_t)(line_i * Cols);

            // Draw hightlight if corresponds to current pc or current highlight.
            size_t addr_mask = (1u << AddrSize) - 1u;
            bool is_program_counter =
                (program_counter & addr_mask) == (addr & addr_mask);
            bool is_highlighted = HighlightMin <= addr && addr <= HighlightMax;
            if (is_program_counter || is_highlighted) {
                ImU32 color = is_program_counter ? ProgramCounterColor
                                                 : HighlightColor;
                ImVec2 pos = ImGui::GetCursorScreenPos();
                draw_list->AddRectFilled(pos,
                    ImVec2(pos.x + ImGui::GetWindowWidth(), pos.y + s.LineHeight),
                    color);
            }

            // Draw address.
            ImGui::Text(format_address, s.AddrDigitsCount,
                        base_display_addr + addr);

            // Draw Hexadecimal
            for (int n = 0; n < Cols && addr < mem_size; n++, addr++)
            {
                float byte_pos_x = s.PosHexStart + s.HexCellWidth * n;
                ImGui::SameLine(byte_pos_x);

                // NB: The trailing space is not visible but ensure there's no gap that the mouse cannot click on.
                u8 b = ReadFn ? ReadFn(mem_data, addr) : mem_data[addr];
                if (b == 0 && OptGreyOutZeroes)
                    ImGui::TextDisabled("00 ");
                else
                    ImGui::Text(format_byte_space, b);
            }
            addr = line_i * Cols;

            // Draw breakpoint flag.
            if (debugger::debugger.checkBreakpoint(base_display_addr + addr) &&
                enable_breakpoints) {
                ImGui::SameLine();
                ImGui::Text(" *");
            }

            // Draw disassembled instruction
            ImGui::SameLine(s.PosInstrStart);
            u32 instr = __builtin_bswap32(*(u32 *)&mem_data[addr]);
            std::string instr_str = disas(base_display_addr + addr, instr);
            ImGui::Text("%s", instr_str.c_str());

            ImGui::EndGroup();

            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
                next_highlight_min = next_highlight_max = addr;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1) &&
                enable_breakpoints) {
                BreakpointAddr = base_display_addr + addr;
                ImGui::OpenPopup("breakpoint_popup");
            }
        }
        if (ImGui::BeginPopup("breakpoint_popup")) {
            bool has_breakpoint = debugger::debugger.checkBreakpoint(BreakpointAddr);
            if (!has_breakpoint && ImGui::MenuItem("Add breakpoint")) {
                debugger::debugger.setBreakpoint(BreakpointAddr);
                ImGui::CloseCurrentPopup();
            } else if (has_breakpoint && ImGui::MenuItem("Remove breakpoint")) {
                debugger::debugger.unsetBreakpoint(BreakpointAddr);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        clipper.End();
        ImGui::PopStyleVar(2);
        ImGui::EndChild();

        ImGui::Separator();

        if (ImGui::Button("Options"))
            ImGui::OpenPopup("context");
        if (ImGui::BeginPopup("context"))
        {
            ImGui::PushItemWidth(56);
            ImGui::PopItemWidth();
            ImGui::Checkbox("Grey out zeroes", &OptGreyOutZeroes);
            ImGui::Checkbox("Uppercase Hex", &OptUpperCaseHex);
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::Text(format_range, s.AddrDigitsCount, base_display_addr, s.AddrDigitsCount, base_display_addr + mem_size - 1);
        ImGui::SameLine();
        ImGui::PushItemWidth((s.AddrDigitsCount + 1) * s.GlyphWidth + style.FramePadding.x * 2.0f);
        if (ImGui::InputText("##addr", AddrInputBuf, 32, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue))
        {
            size_t goto_addr;
            if (sscanf(AddrInputBuf, "%" _PRISizeT "X", &goto_addr) == 1)
            {
                GotoAddr = goto_addr - base_display_addr;
                HighlightMin = HighlightMax = (size_t)-1;
            }
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Jump to pc"))
        {
            size_t addr_mask = (1u << AddrSize) - 1u;
            GotoAddr = program_counter & addr_mask;
            next_highlight_min = next_highlight_max = (size_t)-1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Export"))
        {
            std::ofstream os;
            u32 *mem_data_u32_ptr = (u32 *)mem_data_void_ptr;
            os.open("a.S");
            for (size_t a = 0; a < mem_size; a += 4) {
                u32 instr = __builtin_bswap32(mem_data_u32_ptr[a / 4]);
                os << std::hex << std::setfill(' ') << std::right;
                os << std::setw(16) << a << "    ";
                os << std::hex << std::setfill('0');
                os << std::setw(8) << instr << "    ";
                os << std::setfill(' ');
                os << disas(a, instr);
                os << std::endl;
            }
            os.close();
        }

        if (GotoAddr != (size_t)-1)
        {
            if (GotoAddr < mem_size)
            {
                ImGui::BeginChild("##scrolling");
                ImGui::SetScrollFromPosY(ImGui::GetCursorStartPos().y + (GotoAddr / Cols) * ImGui::GetTextLineHeight());
                ImGui::EndChild();
            }
            GotoAddr = (size_t)-1;
        }

        // Notify the main window of our ideal child content size (FIXME: we are missing an API to get the contents size from the child)
        ImGui::SetCursorPosX(s.WindowWidth);
        HighlightMin = next_highlight_min;
        HighlightMax = next_highlight_max;
    }
};

#undef _PRISizeT
#undef ImSnprintf
