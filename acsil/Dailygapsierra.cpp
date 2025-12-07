#include "sierrachart.h"

SCDLLName("Daily Opening Gap Highlighter")

SCSFExport scsf_DailyOpeningGapHighlighter(SCStudyInterfaceRef sc) {
    // Inputs
    SCInputRef Input_HideWhenFilled = sc.Input[0];
    SCInputRef Input_SessionStartTime = sc.Input[1];

    // Persistent variable to track if we've seen today's open
    int& LastOpenDate = sc.GetPersistentInt(1);

    if (sc.SetDefaults) {
        sc.GraphName = "Daily Opening Gap Highlighter";
        sc.StudyDescription = "Identifies daily opening gap from prior session close to current session open and highlights it with a transparent rectangle.";
        sc.AutoLoop = 1;
        sc.GraphRegion = 0;

        sc.Subgraph[0].Name = "Gap Up";
        sc.Subgraph[0].DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_RECTANGLE_TOP;
        sc.Subgraph[0].PrimaryColor = RGB(0, 103, 105);  // Dark Turquoise

        sc.Subgraph[1].Name = "Gap Down";
        sc.Subgraph[1].DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_RECTANGLE_BOTTOM;
        sc.Subgraph[1].PrimaryColor = RGB(125, 10, 75);  // Deep Pink

        Input_HideWhenFilled.Name = "Hide Gaps When Filled";
        Input_HideWhenFilled.SetYesNo(1);

        Input_SessionStartTime.Name = "Session Start Time (EST)";
        Input_SessionStartTime.SetTime(HMS_TIME(9, 30, 0));  // Default 9:30 AM

        return;
    }

    // Clear subgraph values by default
    sc.Subgraph[0][sc.Index] = 0;
    sc.Subgraph[1][sc.Index] = 0;

    // Need at least 2 bars
    if (sc.Index < 1)
        return;

    // Get bar date and time
    int BarDate = sc.BaseDateTimeIn.DateAt(sc.Index);
    int BarTime = sc.BaseDateTimeIn.TimeAt(sc.Index);
    
    int PriorBarDate = sc.BaseDateTimeIn.DateAt(sc.Index - 1);
    int PriorBarTime = sc.BaseDateTimeIn.TimeAt(sc.Index - 1);

    // Get session start time as integer (HHMMSS format)
    int SessionStartTime = Input_SessionStartTime.GetTime();

    // Check if this is the first bar at or after session start time
    // Condition: Current bar is at/after session start AND prior bar was before session start
    // OR it's a new date AND current bar is at/after session start
    bool IsSessionOpen = false;

    if (BarTime >= SessionStartTime)
    {
        // Either prior bar was before session start time, or it's a new date
        if (PriorBarTime < SessionStartTime || BarDate != PriorBarDate)
        {
            // Make sure we haven't already processed this day's open
            if (LastOpenDate != BarDate)
            {
                IsSessionOpen = true;
                LastOpenDate = BarDate;
            }
        }
    }

    if (!IsSessionOpen)
        return;

    // Get the prior bar's close (end of prior session) and current bar's open
    float priorClose = sc.Close[sc.Index - 1];
    float currentOpen = sc.Open[sc.Index];

    // No gap if prices are equal
    if (priorClose == currentOpen)
        return;

    bool isGapUp = currentOpen > priorClose;
    bool gapFilled = false;

    // Check if the gap is filled on this bar
    if (isGapUp && sc.Low[sc.Index] <= priorClose) {
        gapFilled = true;
    }
    else if (!isGapUp && sc.High[sc.Index] >= priorClose) {
        gapFilled = true;
    }

    // Only plot the gap if it's not filled or user wants to show filled gaps
    if (!gapFilled || !Input_HideWhenFilled.GetYesNo()) {
        if (isGapUp) {
            // Gap Up: rectangle from prior close (bottom) to current open (top)
            sc.Subgraph[0][sc.Index] = currentOpen;  // Top
            sc.Subgraph[1][sc.Index] = priorClose;   // Bottom
        }
        else {
            // Gap Down: rectangle from current open (bottom) to prior close (top)
            sc.Subgraph[0][sc.Index] = priorClose;   // Top
            sc.Subgraph[1][sc.Index] = currentOpen;  // Bottom
        }
    }
}
