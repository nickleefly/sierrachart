#include "sierrachart.h"

SCDLLName("RelativeVolume_TimeBased")

// Helper function to find the index for a given DateTime using Binary Search
int GetBestMatchIndex(SCStudyInterfaceRef sc, SCDateTime TargetDateTime)
{
    int Left = 0; 
    int Right = sc.ArraySize - 1;

    // Standard Binary Search to find insertion point (Lower Bound)
    while (Left <= Right)
    {
        int Mid = Left + (Right - Left) / 2;
        if (sc.BaseDateTimeIn[Mid] < TargetDateTime)
            Left = Mid + 1;
        else if (sc.BaseDateTimeIn[Mid] > TargetDateTime)
            Right = Mid - 1;
        else
            return Mid; // Exact match found
    }
    
    // Left is now the index of the first element >= TargetDateTime
    if (Left >= sc.ArraySize) return sc.ArraySize - 1;
    if (Left < 0) return 0;
    
    return Left; 
}

SCSFExport scsf_RelativeVolume_TimeBased(SCStudyInterfaceRef sc)
{
    // Inputs
    SCInputRef Input_StartTime = sc.Input[0];
    SCInputRef Input_LookbackDays = sc.Input[1];
    
    // Subgraphs
    // 1. Cumulative RVol (Original)
    SCSubgraphRef Subgraph_CumRVol = sc.Subgraph[0];
    
    // 2. Single Bar RVol (Original)
    SCSubgraphRef Subgraph_BarRVol = sc.Subgraph[1];
    
    // 3. Data Visualization Lines
    SCSubgraphRef Subgraph_CurrentCumVol = sc.Subgraph[2];
    SCSubgraphRef Subgraph_AvgHistCumVol = sc.Subgraph[3];
    
    SCSubgraphRef Subgraph_CurrentBarVol = sc.Subgraph[4];
    SCSubgraphRef Subgraph_AvgHistBarVol = sc.Subgraph[5];
    
    SCSubgraphRef Subgraph_ZeroLine = sc.Subgraph[6];

    // 4. NEW: Full Session Ratio
    SCSubgraphRef Subgraph_FullSessionRatio = sc.Subgraph[7];
    SCSubgraphRef Subgraph_AvgFullSessionVol = sc.Subgraph[8];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Relative Volume (Time Based & Full Session)";
        sc.StudyDescription = "Calculates Cumulative RVol, Single Bar RVol, and Ratio vs Avg Full Session Volume.";

        sc.AutoLoop = 1; 
        sc.GraphRegion = 1;
        sc.ValueFormat = 2; 

        // Input Defaults
        Input_StartTime.Name = "Session Start Time (HH:MM:SS)";
        Input_StartTime.SetTime(HMS_TIME(18, 0, 0)); 

        Input_LookbackDays.Name = "Lookback Days";
        Input_LookbackDays.SetInt(20);

        // -- Subgraph Config --
        
        // 1. Cumulative RVol
        Subgraph_CumRVol.Name = "Cumulative RVol Ratio";
        Subgraph_CumRVol.DrawStyle = DRAWSTYLE_LINE;
        Subgraph_CumRVol.PrimaryColor = RGB(0, 255, 0); // Green
        Subgraph_CumRVol.LineWidth = 2;

        // 2. Bar RVol
        Subgraph_BarRVol.Name = "Current Bar RVol Ratio";
        Subgraph_BarRVol.DrawStyle = DRAWSTYLE_BAR;
        Subgraph_BarRVol.PrimaryColor = RGB(255, 165, 0); // Orange
        Subgraph_BarRVol.LineWidth = 3;

        // 3. Cumulative Data (Visuals)
        Subgraph_CurrentCumVol.Name = "Current Cumulative Vol";
        Subgraph_CurrentCumVol.DrawStyle = DRAWSTYLE_IGNORE; 
        Subgraph_CurrentCumVol.PrimaryColor = RGB(0, 255, 255); 
        
        Subgraph_AvgHistCumVol.Name = "Avg Historical Cumulative Vol";
        Subgraph_AvgHistCumVol.DrawStyle = DRAWSTYLE_IGNORE; 
        Subgraph_AvgHistCumVol.PrimaryColor = RGB(255, 0, 255); 
        
        // 4. Bar Data (Visuals)
        Subgraph_CurrentBarVol.Name = "Current Single Bar Vol";
        Subgraph_CurrentBarVol.DrawStyle = DRAWSTYLE_IGNORE; 
        Subgraph_CurrentBarVol.PrimaryColor = RGB(100, 100, 255); 
        
        Subgraph_AvgHistBarVol.Name = "Avg Historical Single Bar Vol";
        Subgraph_AvgHistBarVol.DrawStyle = DRAWSTYLE_IGNORE; 
        Subgraph_AvgHistBarVol.PrimaryColor = RGB(255, 100, 100); 
        
        // 5. Baseline
        Subgraph_ZeroLine.Name = "Baseline (1.0)";
        Subgraph_ZeroLine.DrawStyle = DRAWSTYLE_LINE;
        Subgraph_ZeroLine.PrimaryColor = RGB(128, 128, 128);
        Subgraph_ZeroLine.LineWidth = 1;
        Subgraph_ZeroLine.DrawZeros = 1;

        // 6. NEW Full Session Subgraphs
        Subgraph_FullSessionRatio.Name = "Ratio: Current Cum / Avg Full Session";
        Subgraph_FullSessionRatio.DrawStyle = DRAWSTYLE_LINE;
        Subgraph_FullSessionRatio.PrimaryColor = RGB(255, 255, 0); // Yellow
        Subgraph_FullSessionRatio.LineWidth = 2;

        Subgraph_AvgFullSessionVol.Name = "Avg Full Session Vol (20 Days)";
        Subgraph_AvgFullSessionVol.DrawStyle = DRAWSTYLE_IGNORE;
        Subgraph_AvgFullSessionVol.PrimaryColor = RGB(200, 200, 200);

        return;
    }

    // -- PROCESSING --

    // Persistent Variables for Caching Full Session Average
    // Index 0: Last Calculated Session Date (int YYYYMMDD)
    // Index 1: Cached Average Full Session Volume (float)
    int& LastCalcSessionDate = sc.GetPersistentInt(0);
    float& CachedAvgFullVol = sc.GetPersistentFloat(1);

    // Reset cache on full recalc
    if (sc.Index == 0)
    {
        LastCalcSessionDate = 0;
        CachedAvgFullVol = 0;
    }

    // 1. Get Current Time and Start Time
    SCDateTime CurrentDateTime = sc.BaseDateTimeIn[sc.Index];
    double StartTimeAsDouble = Input_StartTime.GetTime();

    // 2. ROBUST DATE DETERMINATION
    SCDateTime SessionStartDateTime;
    SessionStartDateTime.SetDate(CurrentDateTime.GetDate());
    SessionStartDateTime.SetTime(StartTimeAsDouble);

    // If "Today's Start Time" is in the FUTURE relative to the current bar,
    // then the session must have actually started Yesterday.
    if (SessionStartDateTime > CurrentDateTime)
    {
        SessionStartDateTime.SetDate(CurrentDateTime.GetDate() - 1);
        SessionStartDateTime.SetTime(StartTimeAsDouble);
    }

    int CurrentSessionDateInt = SessionStartDateTime.GetDate();

    // ---------------------------------------------------------
    // 3. CACHE LOGIC: Calculate AVG FULL SESSION VOLUME (Expensive)
    // Only run this if we have moved to a new session day
    // ---------------------------------------------------------
    if (CurrentSessionDateInt != LastCalcSessionDate)
    {
        double TotalFullSessionVol = 0;
        int ValidFullDaysCount = 0;
        int DaysToLookBack = Input_LookbackDays.GetInt();

        for (int day = 1; day <= DaysToLookBack + 10; day++) 
        {
            if (ValidFullDaysCount >= DaysToLookBack) break;

            // Hist Start: 18:00 on Day - X
            SCDateTime HistStartDateTime = SessionStartDateTime;
            HistStartDateTime.SetDate(SessionStartDateTime.GetDate() - day);
            
            // Hist End: 18:00 on Day - X + 1 Day (Full 24h cycle)
            SCDateTime HistEndDateTime = HistStartDateTime;
            HistEndDateTime.SetDate(HistStartDateTime.GetDate() + 1);

            // Find Indices
            int HistStartIndex = GetBestMatchIndex(sc, HistStartDateTime);
            int HistEndIndex = GetBestMatchIndex(sc, HistEndDateTime);

            // Validate
            if (HistStartIndex != -1 && HistEndIndex != -1 && HistEndIndex > HistStartIndex)
            {
                // Strict check: Start date must match exactly
                if (sc.BaseDateTimeIn[HistStartIndex].GetDate() == HistStartDateTime.GetDate())
                {
                    float DailyVol = 0;
                    for (int k = HistStartIndex; k < HistEndIndex; k++) // < to avoid double counting the closing bar if it overlaps
                    {
                        DailyVol += sc.Volume[k];
                    }

                    if (DailyVol > 0)
                    {
                        TotalFullSessionVol += DailyVol;
                        ValidFullDaysCount++;
                    }
                }
            }
        }

        if (ValidFullDaysCount > 0)
            CachedAvgFullVol = (float)(TotalFullSessionVol / ValidFullDaysCount);
        else
            CachedAvgFullVol = 0;

        // Update Cache Key
        LastCalcSessionDate = CurrentSessionDateInt;
    }

    // ---------------------------------------------------------
    // 4. Calculate CURRENT Volume (Cumulative & Single Bar)
    // ---------------------------------------------------------
    int CurrentStartIndex = GetBestMatchIndex(sc, SessionStartDateTime);
    
    float CurrentCumVolume = 0;
    float CurrentBarVolume = sc.Volume[sc.Index];
    
    if (CurrentStartIndex != -1 && CurrentStartIndex <= sc.Index)
    {
        for (int i = CurrentStartIndex; i <= sc.Index; i++)
        {
            CurrentCumVolume += sc.Volume[i];
        }
    }

    // ---------------------------------------------------------
    // 5. Calculate HISTORICAL Average Volume (Partial / Cumulative)
    // This still needs to run every bar because the "End Time" changes
    // ---------------------------------------------------------
    double TotalHistoricalCumVol = 0;
    double TotalHistoricalBarVol = 0;
    
    int ValidCumDaysCount = 0;
    int ValidBarDaysCount = 0;
    int DaysToLookBack = Input_LookbackDays.GetInt();
    
    for (int day = 1; day <= DaysToLookBack + 10; day++) 
    {
        if (ValidCumDaysCount >= DaysToLookBack) break;

        SCDateTime HistStartDateTime = SessionStartDateTime;
        HistStartDateTime.SetDate(SessionStartDateTime.GetDate() - day);
        
        // End time moves with current time
        SCDateTime HistEndDateTime = HistStartDateTime;
        HistEndDateTime += (CurrentDateTime - SessionStartDateTime);

        int HistStartIndex = GetBestMatchIndex(sc, HistStartDateTime);
        int HistEndIndex = GetBestMatchIndex(sc, HistEndDateTime);

        if (HistStartIndex != -1 && HistEndIndex != -1 && HistEndIndex >= HistStartIndex)
        {
            if (sc.BaseDateTimeIn[HistStartIndex].GetDate() == HistStartDateTime.GetDate())
            {
                // Cumulative Partial
                float DailyCumVol = 0;
                // Sanity check for length. 
                // Replaced SCDateTime::fromMinutes(60) with standard double math (1.0/24.0 = 1 hour)
                if (sc.BaseDateTimeIn[HistEndIndex] <= HistEndDateTime + (1.0 / 24.0)) 
                {
                    for (int k = HistStartIndex; k <= HistEndIndex; k++)
                    {
                        DailyCumVol += sc.Volume[k];
                    }
                }

                if (DailyCumVol > 0)
                {
                    TotalHistoricalCumVol += DailyCumVol;
                    ValidCumDaysCount++;
                }
                
                // Single Bar
                if (sc.BaseDateTimeIn[HistEndIndex].GetDate() == HistEndDateTime.GetDate())
                {
                    float DailyBarVol = sc.Volume[HistEndIndex];
                    if (DailyBarVol > 0)
                    {
                        TotalHistoricalBarVol += DailyBarVol;
                        ValidBarDaysCount++;
                    }
                }
            }
        }
    }

    float AvgCumVolume = 0;
    if (ValidCumDaysCount > 0)
        AvgCumVolume = (float)(TotalHistoricalCumVol / ValidCumDaysCount);
        
    float AvgBarVolume = 0;
    if (ValidBarDaysCount > 0)
        AvgBarVolume = (float)(TotalHistoricalBarVol / ValidBarDaysCount);

    // ---------------------------------------------------------
    // 6. Set Subgraph Values
    // ---------------------------------------------------------
    
    Subgraph_CurrentCumVol[sc.Index] = CurrentCumVolume;
    Subgraph_AvgHistCumVol[sc.Index] = AvgCumVolume;
    Subgraph_CurrentBarVol[sc.Index] = CurrentBarVolume;
    Subgraph_AvgHistBarVol[sc.Index] = AvgBarVolume;
    Subgraph_AvgFullSessionVol[sc.Index] = CachedAvgFullVol; // From Cache
    Subgraph_ZeroLine[sc.Index] = 1.0f;

    // A. Cumulative Ratio
    if (AvgCumVolume > 0)
        Subgraph_CumRVol[sc.Index] = CurrentCumVolume / AvgCumVolume;
    else
        Subgraph_CumRVol[sc.Index] = 0;
        
    // B. Bar Ratio
    if (AvgBarVolume > 0)
        Subgraph_BarRVol[sc.Index] = CurrentBarVolume / AvgBarVolume;
    else
        Subgraph_BarRVol[sc.Index] = 0;

    // C. Full Session Ratio (Current Accumulated / Avg Full Day)
    if (CachedAvgFullVol > 0)
        Subgraph_FullSessionRatio[sc.Index] = CurrentCumVolume / CachedAvgFullVol;
    else
        Subgraph_FullSessionRatio[sc.Index] = 0;
}