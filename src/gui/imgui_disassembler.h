
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

#include <mips/asm.h>

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

    enum DataType
    {
        DataType_S8,
        DataType_U8,
        DataType_S16,
        DataType_U16,
        DataType_S32,
        DataType_U32,
        DataType_S64,
        DataType_U64,
        DataType_Float,
        DataType_Double,
        DataType_COUNT
    };

    enum DataFormat
    {
        DataFormat_Bin = 0,
        DataFormat_Dec = 1,
        DataFormat_Hex = 2,
        DataFormat_COUNT
    };

    /* Settings */

    unsigned        AddrSize;
    bool            Open;                                   // = true   // set to false when DrawWindow() was closed. ignore if not using DrawWindow().
    bool            ReadOnly;                               // = false  // disable any editing.
    int             Cols;                                   // = 16     // number of columns to display.
    bool            OptShowOptions;                         // = true   // display options button/context menu. when disabled, options will be locked unless you provide your own UI for them.
    bool            OptShowDataPreview;                     // = false  // display a footer previewing the decimal/binary/hex/float representation of the currently selected bytes.
    bool            OptGreyOutZeroes;                       // = true   // display null/zero bytes using the TextDisabled color.
    bool            OptUpperCaseHex;                        // = true   // display hexadecimal values as "FF" instead of "ff".
    int             OptAddrDigitsCount;                     // = 0      // number of addr digits to display (default calculated based on maximum displayed addr).
    ImU32           HighlightColor;                         //          // background color of highlighted bytes.
    u8              (*ReadFn)(const u8* data, size_t off);  // = NULL   // optional handler to read bytes.
    void            (*WriteFn)(u8* data, size_t off, u8 d); // = NULL   // optional handler to write bytes.
    bool            (*HighlightFn)(const u8* data, size_t off);//NULL   // optional handler to return Highlight property (to support non-contiguous highlighting).

    /* Internal state */

    bool            ContentsWidthChanged;
    size_t          DataPreviewAddr;
    size_t          DataEditingAddr;
    bool            DataEditingTakeFocus;
    char            DataInputBuf[32];
    char            AddrInputBuf[32];
    size_t          GotoAddr;
    size_t          HighlightMin, HighlightMax;
    int             PreviewEndianess;
    DataType        PreviewDataType;

    Disassembler(unsigned addr_size) : AddrSize(addr_size)
    {
        // Settings
        Open = true;
        ReadOnly = false;
        Cols = 4;
        OptShowOptions = true;
        OptShowDataPreview = false;
        OptGreyOutZeroes = true;
        OptUpperCaseHex = true;
        OptAddrDigitsCount = 0;
        HighlightColor = IM_COL32(255, 255, 255, 50);
        ReadFn = NULL;
        WriteFn = NULL;
        HighlightFn = NULL;

        // State/Internals
        ContentsWidthChanged = false;
        DataPreviewAddr = DataEditingAddr = (size_t)-1;
        DataEditingTakeFocus = false;
        memset(DataInputBuf, 0, sizeof(DataInputBuf));
        memset(AddrInputBuf, 0, sizeof(AddrInputBuf));
        GotoAddr = (size_t)-1;
        HighlightMin = HighlightMax = (size_t)-1;
        PreviewEndianess = 0;
        PreviewDataType = DataType_S32;
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
        s.PosInstrStart = s.PosHexEnd + s.GlyphWidth * 1;
        s.PosInstrEnd = s.PosInstrStart + Cols * s.GlyphWidth;
        s.WindowWidth = s.PosInstrEnd + style.ScrollbarSize + style.WindowPadding.x * 2 + s.GlyphWidth;
    }

    // Standalone Memory Editor window
    void DrawWindow(const char* title, void* mem_data, size_t mem_size, size_t base_display_addr = 0x0000)
    {
        Sizes s;
        CalcSizes(s, mem_size, base_display_addr);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(0.0f, 0.0f), ImVec2(s.WindowWidth, FLT_MAX));

        Open = true;
        if (ImGui::Begin(title, &Open, ImGuiWindowFlags_NoScrollbar))
        {
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
                ImGui::IsMouseClicked(1))
                ImGui::OpenPopup("context");
            DrawContents(mem_data, mem_size, base_display_addr);
            if (ContentsWidthChanged)
            {
                CalcSizes(s, mem_size, base_display_addr);
                ImGui::SetWindowSize(ImVec2(s.WindowWidth, ImGui::GetWindowSize().y));
            }
        }
        ImGui::End();
    }

    // Memory Editor contents only
    void DrawContents(void* mem_data_void_ptr, size_t mem_size,
        uint64_t program_counter,
        size_t base_display_addr = 0x0000
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
        float footer_height = 0;
        if (OptShowOptions)
            footer_height += height_separator + ImGui::GetFrameHeightWithSpacing() * 1;
        if (OptShowDataPreview)
            footer_height += height_separator
                          + ImGui::GetFrameHeightWithSpacing() * 1
                          + ImGui::GetTextLineHeightWithSpacing() * 3;
        ImGui::BeginChild("##scrolling", ImVec2(0, -footer_height), false, ImGuiWindowFlags_NoMove);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        const int line_total_count = (int)((mem_size + Cols - 1) / Cols);
        ImGuiListClipper clipper(line_total_count, s.LineHeight);
        const size_t visible_start_addr = clipper.DisplayStart * Cols;
        const size_t visible_end_addr = clipper.DisplayEnd * Cols;

        bool data_next = false;

        if (ReadOnly || DataEditingAddr >= mem_size)
            DataEditingAddr = (size_t)-1;
        if (DataPreviewAddr >= mem_size)
            DataPreviewAddr = (size_t)-1;

        size_t preview_data_type_size = OptShowDataPreview ? DataTypeGetSize(PreviewDataType) : 0;

        size_t data_editing_addr_backup = DataEditingAddr;
        size_t data_editing_addr_next = (size_t)-1;
        if (DataEditingAddr != (size_t)-1)
        {
            // Move cursor but only apply on next frame so scrolling with be synchronized (because currently we can't change the scrolling while the window is being rendered)
            if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_UpArrow)) && DataEditingAddr >= (size_t)Cols)          {
                data_editing_addr_next = DataEditingAddr - Cols; DataEditingTakeFocus = true;
            }
            else if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_DownArrow)) && DataEditingAddr < mem_size - Cols) {
                data_editing_addr_next = DataEditingAddr + Cols; DataEditingTakeFocus = true;
            }
            else if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_LeftArrow)) && DataEditingAddr > 0)               {
                data_editing_addr_next = DataEditingAddr - 1; DataEditingTakeFocus = true;
            }
            else if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_RightArrow)) && DataEditingAddr < mem_size - 1)   {
                data_editing_addr_next = DataEditingAddr + 1; DataEditingTakeFocus = true;
            }
        }
        if (data_editing_addr_next != (size_t)-1 && (data_editing_addr_next / Cols) != (data_editing_addr_backup / Cols))
        {
            // Track cursor movements
            const int scroll_offset = ((int)(data_editing_addr_next / Cols) - (int)(data_editing_addr_backup / Cols));
            const bool scroll_desired =
                (scroll_offset < 0 && data_editing_addr_next < visible_start_addr + Cols * 2) ||
                (scroll_offset > 0 && data_editing_addr_next > visible_end_addr - Cols * 2);
            if (scroll_desired)
                ImGui::SetScrollY(ImGui::GetScrollY() + scroll_offset * s.LineHeight);
        }

        // Draw vertical separator
        ImVec2 window_pos = ImGui::GetWindowPos();
        draw_list->AddLine(ImVec2(window_pos.x + s.PosInstrStart - s.GlyphWidth, window_pos.y),
                           ImVec2(window_pos.x + s.PosInstrStart - s.GlyphWidth, window_pos.y + 9999),
                           ImGui::GetColorU32(ImGuiCol_Border));

        const char* format_address = OptUpperCaseHex ? "%0*" _PRISizeT "X: " : "%0*" _PRISizeT "x: ";
        const char* format_data = OptUpperCaseHex ? "%0*" _PRISizeT "X" : "%0*" _PRISizeT "x";
        const char* format_range = OptUpperCaseHex ? "Range %0*" _PRISizeT "X..%0*" _PRISizeT "X" : "Range %0*" _PRISizeT "x..%0*" _PRISizeT "x";
        const char* format_byte = OptUpperCaseHex ? "%02X" : "%02x";
        const char* format_byte_space = OptUpperCaseHex ? "%02X " : "%02x ";

        for (int line_i = clipper.DisplayStart; line_i < clipper.DisplayEnd; line_i++) // display only visible lines
        {
            size_t addr = (size_t)(line_i * Cols);
            ImGui::Text(format_address, s.AddrDigitsCount, base_display_addr + addr);

            // Draw hightlight if corresponds to current pc.
            size_t addr_mask = (1u << AddrSize) - 1u;
            bool is_program_counter =
                (program_counter & addr_mask) == (addr & addr_mask);
            if (is_program_counter) {
                ImGui::SameLine(0);
                ImVec2 pos = ImGui::GetCursorScreenPos();
                draw_list->AddRectFilled(
                    pos,
                    ImVec2(pos.x + ImGui::GetWindowWidth(), pos.y + s.LineHeight),
                    HighlightColor);
            }

            // Draw Hexadecimal
            for (int n = 0; n < Cols && addr < mem_size; n++, addr++)
            {
                float byte_pos_x = s.PosHexStart + s.HexCellWidth * n;
                ImGui::SameLine(byte_pos_x);

                // Draw highlight
                bool is_highlight_from_user_range = (addr >= HighlightMin && addr < HighlightMax);
                bool is_highlight_from_user_func = (HighlightFn && HighlightFn(mem_data, addr));
                bool is_highlight_from_preview = (addr >= DataPreviewAddr && addr < DataPreviewAddr + preview_data_type_size);
                if (is_highlight_from_user_range || is_highlight_from_user_func || is_highlight_from_preview)
                {
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    float highlight_width = s.GlyphWidth * 2;
                    bool is_next_byte_highlighted =  (addr + 1 < mem_size) && ((HighlightMax != (size_t)-1 && addr + 1 < HighlightMax) || (HighlightFn && HighlightFn(mem_data, addr + 1)));
                    if (is_next_byte_highlighted || (n + 1 == Cols))
                    {
                        highlight_width = s.HexCellWidth;
                    }
                    draw_list->AddRectFilled(pos, ImVec2(pos.x + highlight_width, pos.y + s.LineHeight), HighlightColor);
                }

                if (DataEditingAddr == addr)
                {
                    // Display text input on current byte
                    bool data_write = false;
                    ImGui::PushID((void*)addr);
                    if (DataEditingTakeFocus)
                    {
                        ImGui::SetKeyboardFocusHere();
                        ImGui::CaptureKeyboardFromApp(true);
                        sprintf(AddrInputBuf, format_data, s.AddrDigitsCount, base_display_addr + addr);
                        sprintf(DataInputBuf, format_byte, ReadFn ? ReadFn(mem_data, addr) : mem_data[addr]);
                    }
                    ImGui::PushItemWidth(s.GlyphWidth * 2);
                    struct UserData
                    {
                        // FIXME: We should have a way to retrieve the text edit
                        // cursor position more easily in the API, this is
                        // rather tedious. This is such a ugly mess we may be
                        // better off not using InputText() at all here.
                        static int Callback(ImGuiInputTextCallbackData* data)
                        {
                            UserData* user_data = (UserData*)data->UserData;
                            if (!data->HasSelection())
                                user_data->CursorPos = data->CursorPos;
                            if (data->SelectionStart == 0 && data->SelectionEnd == data->BufTextLen)
                            {
                                // When not editing a byte, always rewrite its content
                                // (this is a bit tricky, since InputText technically
                                // "owns" the master copy of the buffer we edit it in there)
                                data->DeleteChars(0, data->BufTextLen);
                                data->InsertChars(0, user_data->CurrentBufOverwrite);
                                data->SelectionStart = 0;
                                data->SelectionEnd = data->CursorPos = 2;
                            }
                            return 0;
                        }
                        char   CurrentBufOverwrite[3];  // Input
                        int    CursorPos;               // Output
                    };
                    UserData user_data;
                    user_data.CursorPos = -1;
                    sprintf(user_data.CurrentBufOverwrite, format_byte, ReadFn ? ReadFn(mem_data, addr) : mem_data[addr]);
                    ImGuiInputTextFlags flags =
                        ImGuiInputTextFlags_CharsHexadecimal |
                        ImGuiInputTextFlags_EnterReturnsTrue |
                        ImGuiInputTextFlags_AutoSelectAll |
                        ImGuiInputTextFlags_NoHorizontalScroll |
                        ImGuiInputTextFlags_AlwaysInsertMode |
                        ImGuiInputTextFlags_CallbackAlways;
                    if (ImGui::InputText("##data", DataInputBuf, 32, flags, UserData::Callback, &user_data))
                        data_write = data_next = true;
                    else if (!DataEditingTakeFocus && !ImGui::IsItemActive())
                        DataEditingAddr = data_editing_addr_next = (size_t)-1;
                    DataEditingTakeFocus = false;
                    ImGui::PopItemWidth();
                    if (user_data.CursorPos >= 2)
                        data_write = data_next = true;
                    if (data_editing_addr_next != (size_t)-1)
                        data_write = data_next = false;
                    unsigned int data_input_value = 0;
                    if (data_write && sscanf(DataInputBuf, "%X", &data_input_value) == 1)
                    {
                        if (WriteFn)
                            WriteFn(mem_data, addr, (u8)data_input_value);
                        else
                            mem_data[addr] = (u8)data_input_value;
                    }
                    ImGui::PopID();
                }
                else
                {
                    // NB: The trailing space is not visible but ensure there's no gap that the mouse cannot click on.
                    u8 b = ReadFn ? ReadFn(mem_data, addr) : mem_data[addr];

                    if (b == 0 && OptGreyOutZeroes)
                        ImGui::TextDisabled("00 ");
                    else
                        ImGui::Text(format_byte_space, b);

                    if (!ReadOnly && ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
                    {
                        DataEditingTakeFocus = true;
                        data_editing_addr_next = addr;
                    }
                }
            }

            // Draw disassembled instruction
            ImGui::SameLine(s.PosInstrStart);
            addr = line_i * Cols;
            u32 instr = __builtin_bswap32(*(u32 *)&mem_data[addr]);
            std::string instr_str = Mips::disas(base_display_addr + addr, instr);
            ImGui::Text("%s", instr_str.c_str());
        }
        clipper.End();
        ImGui::PopStyleVar(2);
        ImGui::EndChild();

        if (data_next && DataEditingAddr < mem_size)
        {
            DataEditingAddr = DataPreviewAddr = DataEditingAddr + 1;
            DataEditingTakeFocus = true;
        }
        else if (data_editing_addr_next != (size_t)-1)
        {
            DataEditingAddr = DataPreviewAddr = data_editing_addr_next;
        }

        bool next_show_data_preview = OptShowDataPreview;
        if (OptShowOptions)
        {
            ImGui::Separator();

            // Options menu

            if (ImGui::Button("Options"))
                ImGui::OpenPopup("context");
            if (ImGui::BeginPopup("context"))
            {
                ImGui::PushItemWidth(56);
                if (ImGui::DragInt("##cols", &Cols, 0.2f, 4, 32, "%d cols")) { ContentsWidthChanged = true; }
                ImGui::PopItemWidth();
                ImGui::Checkbox("Show Data Preview", &next_show_data_preview);
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
                HighlightMin = HighlightMax = (size_t)-1;
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
                    os << Mips::disas(a, instr);
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
                    DataEditingAddr = DataPreviewAddr = GotoAddr;
                    DataEditingTakeFocus = true;
                }
                GotoAddr = (size_t)-1;
            }
        }

        if (OptShowDataPreview)
        {
            ImGui::Separator();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Preview as:");
            ImGui::SameLine();
            ImGui::PushItemWidth((s.GlyphWidth * 10.0f) + style.FramePadding.x * 2.0f + style.ItemInnerSpacing.x);
            if (ImGui::BeginCombo("##combo_type", DataTypeGetDesc(PreviewDataType), ImGuiComboFlags_HeightLargest))
            {
                for (int n = 0; n < DataType_COUNT; n++)
                    if (ImGui::Selectable(DataTypeGetDesc((DataType)n), PreviewDataType == n))
                        PreviewDataType = (DataType)n;
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushItemWidth((s.GlyphWidth * 6.0f) + style.FramePadding.x * 2.0f + style.ItemInnerSpacing.x);
            ImGui::Combo("##combo_endianess", &PreviewEndianess, "LE\0BE\0\0");
            ImGui::PopItemWidth();

            char buf[128];
            float x = s.GlyphWidth * 6.0f;
            bool has_value = DataPreviewAddr != (size_t)-1;
            if (has_value)
                DisplayPreviewData(DataPreviewAddr, mem_data, mem_size, PreviewDataType, DataFormat_Dec, buf, (size_t)IM_ARRAYSIZE(buf));
            ImGui::Text("Dec"); ImGui::SameLine(x); ImGui::TextUnformatted(has_value ? buf : "N/A");
            if (has_value)
                DisplayPreviewData(DataPreviewAddr, mem_data, mem_size, PreviewDataType, DataFormat_Hex, buf, (size_t)IM_ARRAYSIZE(buf));
            ImGui::Text("Hex"); ImGui::SameLine(x); ImGui::TextUnformatted(has_value ? buf : "N/A");
            if (has_value)
                DisplayPreviewData(DataPreviewAddr, mem_data, mem_size, PreviewDataType, DataFormat_Bin, buf, (size_t)IM_ARRAYSIZE(buf));
            ImGui::Text("Bin"); ImGui::SameLine(x); ImGui::TextUnformatted(has_value ? buf : "N/A");
        }

        OptShowDataPreview = next_show_data_preview;

        // Notify the main window of our ideal child content size (FIXME: we are missing an API to get the contents size from the child)
        ImGui::SetCursorPosX(s.WindowWidth);
    }

    // Utilities for Data Preview
    const char* DataTypeGetDesc(DataType data_type) const
    {
        const char* descs[] = { "Int8", "Uint8", "Int16", "Uint16", "Int32", "Uint32", "Int64", "Uint64", "Float", "Double" };
        IM_ASSERT(data_type >= 0 && data_type < DataType_COUNT);
        return descs[data_type];
    }

    size_t DataTypeGetSize(DataType data_type) const
    {
        const size_t sizes[] = { 1, 1, 2, 2, 4, 4, 8, 8, 4, 8 };
        IM_ASSERT(data_type >= 0 && data_type < DataType_COUNT);
        return sizes[data_type];
    }

    const char* DataFormatGetDesc(DataFormat data_format) const
    {
        const char* descs[] = { "Bin", "Dec", "Hex" };
        IM_ASSERT(data_format >= 0 && data_format < DataFormat_COUNT);
        return descs[data_format];
    }

    bool IsBigEndian() const
    {
        uint16_t x = 1;
        char c[2];
        memcpy(c, &x, 2);
        return c[0] != 0;
    }

    static void* EndianessCopyBigEndian(void* _dst, void* _src, size_t s, int is_little_endian)
    {
        if (is_little_endian)
        {
            uint8_t* dst = (uint8_t*)_dst;
            uint8_t* src = (uint8_t*)_src + s - 1;
            for (int i = 0, n = (int)s; i < n; ++i)
                memcpy(dst++, src--, 1);
            return _dst;
        }
        else
        {
            return memcpy(_dst, _src, s);
        }
    }

    static void* EndianessCopyLittleEndian(void* _dst, void* _src, size_t s, int is_little_endian)
    {
        if (is_little_endian)
        {
            return memcpy(_dst, _src, s);
        }
        else
        {
            uint8_t* dst = (uint8_t*)_dst;
            uint8_t* src = (uint8_t*)_src + s - 1;
            for (int i = 0, n = (int)s; i < n; ++i)
                memcpy(dst++, src--, 1);
            return _dst;
        }
    }

    void* EndianessCopy(void *dst, void *src, size_t size) const
    {
        static void *(*fp)(void *, void *, size_t, int) = NULL;
        if (fp == NULL)
            fp = IsBigEndian() ? EndianessCopyBigEndian : EndianessCopyLittleEndian;
        return fp(dst, src, size, PreviewEndianess);
    }

    const char* FormatBinary(const uint8_t* buf, int width) const
    {
        IM_ASSERT(width <= 64);
        size_t out_n = 0;
        static char out_buf[64 + 8 + 1];
        int n = width / 8;
        for (int j = n - 1; j >= 0; --j)
        {
            for (int i = 0; i < 8; ++i)
                out_buf[out_n++] = (buf[j] & (1 << (7 - i))) ? '1' : '0';
            out_buf[out_n++] = ' ';
        }
        IM_ASSERT(out_n < IM_ARRAYSIZE(out_buf));
        out_buf[out_n] = 0;
        return out_buf;
    }

    void DisplayPreviewData(size_t addr, const u8* mem_data, size_t mem_size, DataType data_type, DataFormat data_format, char* out_buf, size_t out_buf_size) const
    {
        uint8_t buf[8];
        size_t elem_size = DataTypeGetSize(data_type);
        size_t size = addr + elem_size > mem_size ? mem_size - addr : elem_size;
        if (ReadFn)
            for (int i = 0, n = (int)size; i < n; ++i)
                buf[i] = ReadFn(mem_data, addr + i);
        else
            memcpy(buf, mem_data + addr, size);

        if (data_format == DataFormat_Bin)
        {
            uint8_t binbuf[8];
            EndianessCopy(binbuf, buf, size);
            ImSnprintf(out_buf, out_buf_size, "%s", FormatBinary(binbuf, (int)size * 8));
            return;
        }

        out_buf[0] = 0;
        switch (data_type)
        {
        case DataType_S8:
        {
            int8_t int8 = 0;
            EndianessCopy(&int8, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%hhd", int8); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "0x%02x", int8 & 0xFF); return; }
            break;
        }
        case DataType_U8:
        {
            uint8_t uint8 = 0;
            EndianessCopy(&uint8, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%hhu", uint8); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "0x%02x", uint8 & 0XFF); return; }
            break;
        }
        case DataType_S16:
        {
            int16_t int16 = 0;
            EndianessCopy(&int16, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%hd", int16); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "0x%04x", int16 & 0xFFFF); return; }
            break;
        }
        case DataType_U16:
        {
            uint16_t uint16 = 0;
            EndianessCopy(&uint16, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%hu", uint16); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "0x%04x", uint16 & 0xFFFF); return; }
            break;
        }
        case DataType_S32:
        {
            int32_t int32 = 0;
            EndianessCopy(&int32, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%d", int32); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "0x%08x", int32); return; }
            break;
        }
        case DataType_U32:
        {
            uint32_t uint32 = 0;
            EndianessCopy(&uint32, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%u", uint32); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "0x%08x", uint32); return; }
            break;
        }
        case DataType_S64:
        {
            int64_t int64 = 0;
            EndianessCopy(&int64, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%lld", (long long)int64); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "0x%016llx", (long long)int64); return; }
            break;
        }
        case DataType_U64:
        {
            uint64_t uint64 = 0;
            EndianessCopy(&uint64, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%llu", (long long)uint64); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "0x%016llx", (long long)uint64); return; }
            break;
        }
        case DataType_Float:
        {
            float float32 = 0.0f;
            EndianessCopy(&float32, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%f", float32); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "%a", float32); return; }
            break;
        }
        case DataType_Double:
        {
            double float64 = 0.0;
            EndianessCopy(&float64, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%f", float64); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "%a", float64); return; }
            break;
        }
        case DataType_COUNT:
            break;
        } // Switch
        IM_ASSERT(0); // Shouldn't reach
    }
};

#undef _PRISizeT
#undef ImSnprintf
