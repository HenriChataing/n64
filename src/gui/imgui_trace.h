
#pragma once
#include <stdio.h>      // sprintf, scanf
#include <stdint.h>     // uint8_t, etc.

#include <debugger.h>
#include <assembly/disassembler.h>

struct Trace
{
    /* Settings */

    circular_buffer<Debugger::TraceEntry> const *TraceBuffer;
    int             Cols;                                   // = 4      // number of columns to display.
    bool            OptUpperCaseHex;                        // = true   // display hexadecimal values as "FF" instead of "ff".
    int             OptAddrDigitsCount;                     // = 16     // number of addr digits to display (default calculated based on maximum displayed addr).
    int             ExportCounter;

    Trace(circular_buffer<Debugger::TraceEntry> const *trace) : TraceBuffer(trace)
    {
        Cols = 4;
        OptUpperCaseHex = true;
        OptAddrDigitsCount = 16;
        ExportCounter = 0;
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

    void CalcSizes(Sizes &s)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        s.AddrDigitsCount = OptAddrDigitsCount;
        s.LineHeight = ImGui::GetTextLineHeight();
        s.GlyphWidth = ImGui::CalcTextSize("F").x + 1;                  // We assume the font is mono-space
        s.HexCellWidth = (float)(int)(s.GlyphWidth * 2.5f);             // "FF " we include trailing space in the width to easily catch clicks everywhere
        s.PosHexStart = (s.AddrDigitsCount + 2) * s.GlyphWidth;
        s.PosHexEnd = s.PosHexStart + (s.HexCellWidth * Cols);
        s.PosInstrStart = s.PosHexEnd + s.GlyphWidth * 1;
        s.PosInstrEnd = s.PosInstrStart + Cols * s.GlyphWidth;
        s.WindowWidth = s.PosInstrEnd + style.ScrollbarSize + style.WindowPadding.x * 2 + s.GlyphWidth;
    }

    // Trace contents only
    void DrawContents(std::string name, std::string (*disas)(u64 pc, u32 instr))
    {
        Sizes s;
        CalcSizes(s);

        const float height_separator = ImGui::GetStyle().ItemSpacing.y;
        float footer_height = height_separator + ImGui::GetFrameHeightWithSpacing();

        // We begin into our scrolling region with the 'ImGuiWindowFlags_NoMove'
        // in order to prevent click from moving the window.
        // This is used as a facility since our main click detection code
        // doesn't assign an ActiveId so the click would normally be caught as
        // a window-move.
        ImGui::BeginChild("##scrolling", ImVec2(0, -footer_height), false,
            ImGuiWindowFlags_NoMove);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        const int line_total_count = TraceBuffer->length();
        ImGuiListClipper clipper(line_total_count, s.LineHeight);

        // Draw vertical separator
        ImVec2 window_pos = ImGui::GetWindowPos();
        draw_list->AddLine(
            ImVec2(window_pos.x + s.PosInstrStart - s.GlyphWidth, window_pos.y),
            ImVec2(window_pos.x + s.PosInstrStart - s.GlyphWidth, window_pos.y + 9999),
            ImGui::GetColorU32(ImGuiCol_Border));

        const char* format_address = OptUpperCaseHex ? "%0*zX: " : "%0*zx: ";
        const char* format_byte_space = OptUpperCaseHex ? "%02X " : "%02x ";

        // Display visible lines
        for (int line_i = clipper.DisplayStart; line_i < clipper.DisplayEnd; line_i++)
        {
            Debugger::TraceEntry entry = TraceBuffer->peek_front(line_i);
            ImGui::Text(format_address, s.AddrDigitsCount, entry.first);

            // Draw Hexadecimal
            for (int n = 0; n < 4; n++)
            {
                float byte_pos_x = s.PosHexStart + s.HexCellWidth * n;
                ImGui::SameLine(byte_pos_x);

                // NB: The trailing space is not visible but ensure there's
                // no gap that the mouse cannot click on.
                u8 b = (entry.second >> (8 * (4 - n - 1))) & 0xffu;
                if (b == 0)
                    ImGui::TextDisabled("00 ");
                else
                    ImGui::Text(format_byte_space, b);
            }

            // Draw disassembled instruction
            ImGui::SameLine(s.PosInstrStart);
            std::string instr_str = disas(entry.first, entry.second);
            ImGui::Text("%s", instr_str.c_str());
        }

        clipper.End();
        ImGui::PopStyleVar(2);
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("Export")) {
            std::ofstream os;
            os.open(name + "_trace_" + std::to_string(ExportCounter) + ".txt");
            ExportCounter++;
            for (size_t i = 0; i < TraceBuffer->length(); i++)
            {
                Debugger::TraceEntry entry = TraceBuffer->peek_front(i);
                os << std::hex << std::setfill(' ') << std::right;
                os << std::setw(16) << entry.first << "    ";
                os << std::hex << std::setfill('0');
                os << std::setw(8) << entry.second << "    ";
                os << std::setfill(' ');
                os << disas(entry.first, entry.second);
                os << std::endl;
            }
            os.close();
        }

        // Notify the main window of our ideal child content size
        // (FIXME: we are missing an API to get the contents size from the child)
        ImGui::SetCursorPosX(s.WindowWidth);
    }
};
