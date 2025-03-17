#include "sierrachart.h"

SCDLLName("Daily Opening Gap Highlighter")

SCSFExport scsf_DailyOpeningGapHighlighter(SCStudyInterfaceRef sc) {
    if (sc.SetDefaults) {
        sc.GraphName = "Daily Opening Gap Highlighter";
	sc.StudyDescription = "Identifies daily opening gap from prior close to current open and highlights it with a transparent rectangle.";
        sc.AutoLoop = 1;
	sc.GraphRegion = 0;

        sc.Subgraph[0].Name = "Gap Up";
        sc.Subgraph[0].DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_RECTANGLE_TOP;
	sc.Subgraph[0].PrimaryColor = RGB(0,103,105);  //COLOR_DARKTURQUOISE 
        sc.Subgraph[1].Name = "Gap Down";
        sc.Subgraph[1].DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_RECTANGLE_BOTTOM;
	sc.Subgraph[1].PrimaryColor = RGB(125,10,75);  //COLOR_DEEPPINK 

        sc.Input[0].Name = "Delete Gaps When Cleared";
        sc.Input[0].SetYesNo(1); // Default value is True

        return;
    }

    // Get the prior close and current open prices
    float priorClose = sc.Index > 0 ? sc.Close[sc.Index - 1] : 0;
    float currentOpen = sc.Open[sc.Index];

    // Check if there is a gap
    if (priorClose != 0 && priorClose != currentOpen) {
        bool gapCleared = false;

        // Check if the gap is cleared
        if (currentOpen > priorClose && sc.Low[sc.Index] <= priorClose) {
            gapCleared = true; // Gap up day and low crosses bottom of gap
        }
        else if (currentOpen < priorClose && sc.High[sc.Index] >= priorClose) {
            gapCleared = true; // Gap down day and high crosses top of gap
        }

        // Only plot the gap if it's not cleared or if the user has chosen not to delete gaps when cleared
        if (!gapCleared || !sc.Input[0].GetYesNo()) {
            sc.Subgraph[0][sc.Index] = currentOpen; // Top of the rectangle (current open)
            sc.Subgraph[1][sc.Index] = priorClose;  // Bottom of the rectangle (prior close)
        }
    }
}
