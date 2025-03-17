#include "sierrachart.h"
#include <string>
SCDLLName("Frozen Tundra - Lines To Clipboard Exporter")

/*
    Written by Frozen Tundra

    This study allows you to export lines and rectangles drawn
    onto your charts into a google docs spreadsheet.

    The format of the Google Spreadsheet is as follows:
        (float)  Price
        (float)  Price 2 (For Rectangles only)
        (string) Note
        (string) Color
        (int)    Line Type
        (int)    Line Width
        (int)    Text Alignment
*/

void toClipboard(HWND hwnd, const std::string &s);

SCSFExport scsf_LinesToClipboardExporter(SCStudyInterfaceRef sc)
{
    // logging object
    SCString msg;

    // Set configuration variables
    if (sc.SetDefaults)
    {
        sc.GraphName = "Clipboard Exporter";
        sc.GraphRegion = 0;

        return;
    }

    // 4493		high test	green			
    if (sc.Index == 0) {
        SCString CsvStr = "";

        // grab drawings
        s_UseTool ChartDrawing;
        int DrawingIdx = 0;
        while (sc.GetUserDrawnChartDrawing(sc.ChartNumber, DRAWING_UNKNOWN, ChartDrawing, DrawingIdx) > 0) {
            if (ChartDrawing.DrawingType != DRAWING_HORIZONTALLINE
            && ChartDrawing.DrawingType != DRAWING_HORIZONTAL_RAY
            && ChartDrawing.DrawingType != DRAWING_HORIZONTAL_LINE_NON_EXTENDED
            && ChartDrawing.DrawingType != DRAWING_RECTANGLEHIGHLIGHT
            && ChartDrawing.DrawingType != DRAWING_RECTANGLE_EXT_HIGHLIGHT
            ) continue;

            SCString color;
            if (ChartDrawing.Color == COLOR_RED) color = "red";
            else if (ChartDrawing.Color == COLOR_GREEN) color = "green";
            else if (ChartDrawing.Color == COLOR_BLUE) color = "blue";
            else if (ChartDrawing.Color == COLOR_WHITE) color = "white";
            else if (ChartDrawing.Color == COLOR_BLACK) color = "black";
            else if (ChartDrawing.Color == COLOR_PURPLE) color = "purple";
            else if (ChartDrawing.Color == COLOR_PINK) color = "pink";
            else if (ChartDrawing.Color == COLOR_YELLOW) color = "yellow";
            else if (ChartDrawing.Color == COLOR_GOLD) color = "gold";
            else if (ChartDrawing.Color == COLOR_BROWN) color = "brown";
            else if (ChartDrawing.Color == COLOR_CYAN) color = "cyan";
            else if (ChartDrawing.Color == COLOR_GRAY) color = "gray";
            else color = "white";

            SCString RectangleEnd;
            if (ChartDrawing.DrawingType == DRAWING_RECTANGLE_EXT_HIGHLIGHT || ChartDrawing.DrawingType == DRAWING_RECTANGLEHIGHLIGHT) {
                // store the endvalue
                RectangleEnd.Format("%.2f", ChartDrawing.EndValue);
            }
            else {
                RectangleEnd.Format(" ");
            }
            int TextAlignment = 2;
            if (ChartDrawing.TextAlignment == DT_LEFT) TextAlignment = 1;

            SCString NewLine;
            NewLine.Format("%.2f\t%s\t%s\t%s\t%d\t%d\t%d\n", ChartDrawing.BeginValue, RectangleEnd.GetChars(), ChartDrawing.Text.GetChars(), color.GetChars(), (int)ChartDrawing.LineStyle, (int)ChartDrawing.LineWidth, TextAlignment);
            CsvStr += NewLine;

            DrawingIdx++;

            ChartDrawing.Clear();
        }

        HWND hwnd = GetDesktopWindow();
        toClipboard(hwnd, CsvStr.GetChars());
    }

}

// gratefully borrowed from "Andy" @
// http://www.cplusplus.com/forum/general/48837/#msg266980
void toClipboard(HWND hwnd, const std::string &s){
    OpenClipboard(hwnd);
    EmptyClipboard();
    HGLOBAL hg=GlobalAlloc(GMEM_MOVEABLE,s.size()+1);
    if (!hg){
        CloseClipboard();
        return;
    }
    memcpy(GlobalLock(hg),s.c_str(),s.size()+1);
    GlobalUnlock(hg);
    SetClipboardData(CF_TEXT,hg);
    CloseClipboard();
    GlobalFree(hg);
}
