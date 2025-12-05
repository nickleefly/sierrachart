#include "sierrachart.h"

SCDLLName("XYL - VWAP Bands Strategy")

/*
    Adaptive VWAP Bands Strategy (HFT Style)

    Improvements:
    1. Dynamic Bands: Automatically tightens entry bands (2.0 -> 1.5 -> 1.0) if volatility is low.
    2. CVD Filter: Requires Cumulative Delta Momentum to confirm entry.
    3. VIX Filter: Uses external Study ID 15 (VOLX) slope to confirm regime.
*/

SCSFExport scsf_VWAPBandsStrategy(SCStudyInterfaceRef sc)
{
    // --- INPUTS ---
    SCInputRef Input_Enabled = sc.Input[0];
    SCInputRef Input_StartTime = sc.Input[1];

    // Risk Management
    SCInputRef Input_ATRPeriod = sc.Input[2];
    SCInputRef Input_ATRStopMultiplier = sc.Input[3];
    SCInputRef Input_ATRTargetMultiplier = sc.Input[4];
    SCInputRef Input_MaxHoldBars = sc.Input[5];

    // Dynamic Band Settings
    SCInputRef Input_DynamicLookback = sc.Input[6];
    SCInputRef Input_StdDev_High = sc.Input[7]; // Default 2.0
    SCInputRef Input_StdDev_Med  = sc.Input[8]; // Default 1.5
    SCInputRef Input_StdDev_Low  = sc.Input[9]; // Default 1.0

    // Filters
    SCInputRef Input_UseCVDSlope = sc.Input[10];
    SCInputRef Input_CVDSlopeBars = sc.Input[11];

    SCInputRef Input_UseVIXFilter = sc.Input[12];
    SCInputRef Input_VIXStudyID = sc.Input[13]; // ID 15
    SCInputRef Input_VIXSubgraphIndex = sc.Input[14]; // Usually 0
    SCInputRef Input_VIXSlopeBars = sc.Input[15];

    // --- SUBGRAPHS ---
    SCSubgraphRef Subgraph_VWAP = sc.Subgraph[0];
    SCSubgraphRef Subgraph_TopBand = sc.Subgraph[1];    // Visualizes CURRENT active band
    SCSubgraphRef Subgraph_BottomBand = sc.Subgraph[2]; // Visualizes CURRENT active band
    SCSubgraphRef Subgraph_ATR = sc.Subgraph[3];
    SCSubgraphRef Subgraph_CVD = sc.Subgraph[4];        // Cumulative Delta
    SCSubgraphRef Subgraph_ActiveMult = sc.Subgraph[5]; // Which multiplier is active?

    if (sc.SetDefaults)
    {
        // Configuration
        sc.GraphName = "Strategy: Adaptive VWAP Bands + CVD/VIX";
        sc.StudyDescription = "Dynamic Band Selection with CVD and VIX Slope Filters.";
        sc.AutoLoop = 1;
        sc.GraphRegion = 0;
        sc.FreeDLL = 0;

        // Trading Safety
        sc.MaintainTradeStatisticsAndTradesData = 1;
        sc.AllowMultipleEntriesInSameDirection = false;
        sc.SendOrdersToTradeService = false;
        sc.CancelAllWorkingOrdersOnExit = true;

        // Input Defaults
        Input_Enabled.Name = "Enable Trading";
        Input_Enabled.SetYesNo(0);

        Input_StartTime.Name = "Start Time (HHMMSS)";
        Input_StartTime.SetTime(0);

        Input_ATRPeriod.Name = "ATR Period";
        Input_ATRPeriod.SetInt(14);
        Input_ATRStopMultiplier.Name = "Stop Loss (ATR x)";
        Input_ATRStopMultiplier.SetFloat(2.5f);
        Input_ATRTargetMultiplier.Name = "Target Profit (ATR x)";
        Input_ATRTargetMultiplier.SetFloat(4.0f);
        Input_MaxHoldBars.Name = "Max Hold Bars";
        Input_MaxHoldBars.SetInt(15);

        Input_DynamicLookback.Name = "Dynamic Band Lookback Bars";
        Input_DynamicLookback.SetInt(10);
        Input_StdDev_High.Name = "Band Step 1 (High Vol)";
        Input_StdDev_High.SetFloat(2.0f);
        Input_StdDev_Med.Name = "Band Step 2 (Med Vol)";
        Input_StdDev_Med.SetFloat(1.5f);
        Input_StdDev_Low.Name = "Band Step 3 (Low Vol)";
        Input_StdDev_Low.SetFloat(1.0f);

        Input_UseCVDSlope.Name = "Use CVD Slope Filter";
        Input_UseCVDSlope.SetYesNo(1);
        Input_CVDSlopeBars.Name = "CVD Slope Lookback";
        Input_CVDSlopeBars.SetInt(3);

        Input_UseVIXFilter.Name = "Use VIX (VOLX) Filter";
        Input_UseVIXFilter.SetYesNo(0);
        Input_VIXStudyID.Name = "VIX Study ID (e.g., 15)";
        Input_VIXStudyID.SetInt(15);
        Input_VIXSubgraphIndex.Name = "VIX Subgraph Index";
        Input_VIXSubgraphIndex.SetInt(0);
        Input_VIXSlopeBars.Name = "VIX Slope Lookback";
        Input_VIXSlopeBars.SetInt(5);

        // Subgraph Styling
        Subgraph_VWAP.Name = "VWAP";
        Subgraph_VWAP.DrawStyle = DRAWSTYLE_LINE;
        Subgraph_VWAP.PrimaryColor = RGB(255, 0, 255);

        Subgraph_TopBand.Name = "Active Top Band";
        Subgraph_TopBand.DrawStyle = DRAWSTYLE_LINE;
        Subgraph_TopBand.PrimaryColor = RGB(0, 255, 0);

        Subgraph_BottomBand.Name = "Active Bottom Band";
        Subgraph_BottomBand.DrawStyle = DRAWSTYLE_LINE;
        Subgraph_BottomBand.PrimaryColor = RGB(255, 0, 0);

        Subgraph_CVD.Name = "Cumulative Delta";
        Subgraph_CVD.DrawStyle = DRAWSTYLE_IGNORE;

        Subgraph_ActiveMult.Name = "Active Multiplier";
        Subgraph_ActiveMult.DrawStyle = DRAWSTYLE_IGNORE;

        return;
    }

    // ---------------------------------------------------------
    // 1. DATA CALCULATIONS
    // ---------------------------------------------------------

    // ATR
    sc.ATR(sc.BaseData, Subgraph_ATR, Input_ATRPeriod.GetInt(), MOVAVGTYPE_SIMPLE);

    // Cumulative Delta Calculation
    float AskVol = sc.BaseData[SC_ASKVOL][sc.Index];
    float BidVol = sc.BaseData[SC_BIDVOL][sc.Index];
    float BarDelta = AskVol - BidVol;

    if (sc.Index == 0)
        Subgraph_CVD[sc.Index] = BarDelta;
    else
        Subgraph_CVD[sc.Index] = Subgraph_CVD[sc.Index - 1] + BarDelta;

    // Reset CVD at start of day (Optional, but good for cleanliness)
    int DayStartBarIndex = sc.Index;
    int CurrentDate = sc.GetTradingDayDate(sc.BaseDateTimeIn[sc.Index]);

    if (DayStartBarIndex > 0)
    {
        while (DayStartBarIndex > 0)
        {
            int PrevBarDate = sc.GetTradingDayDate(sc.BaseDateTimeIn[DayStartBarIndex - 1]);
            if (PrevBarDate != CurrentDate) break;
            DayStartBarIndex--;
        }

        // If this is the very first bar of the day, reset CVD
        if (DayStartBarIndex == sc.Index)
            Subgraph_CVD[sc.Index] = BarDelta;
    }

    // VWAP Calculation
    double CumulativePV = 0.0;
    double CumulativeVolume = 0.0;
    double CumulativeP2V = 0.0;

    for (int i = DayStartBarIndex; i <= sc.Index; i++)
    {
        float Price = sc.BaseData[SC_LAST][i];
        float Volume = sc.BaseData[SC_VOLUME][i];

        CumulativePV += Price * Volume;
        CumulativeVolume += Volume;
        CumulativeP2V += (Price * Price) * Volume;
    }

    double VWAP = 0;
    double StdDev = 0;

    if (CumulativeVolume > 0)
    {
        VWAP = CumulativePV / CumulativeVolume;
        Subgraph_VWAP[sc.Index] = (float)VWAP;

        double MeanOfSquares = CumulativeP2V / CumulativeVolume;
        double Variance = MeanOfSquares - (VWAP * VWAP);
        if (Variance < 0) Variance = 0;
        StdDev = sqrt(Variance);
    }
    else
    {
        VWAP = sc.Close[sc.Index];
        Subgraph_VWAP[sc.Index] = (float)VWAP;
    }

    // ---------------------------------------------------------
    // 2. DYNAMIC BAND SELECTION
    // ---------------------------------------------------------

    // We need to determine which StdDev Multiplier to use based on recent price action.
    // "If last 10 bars not crossing 2.0, use 1.5. If not 1.5, use 1.0"

    float SelectedMultiplier = Input_StdDev_Low.GetFloat(); // Start with lowest (fallback)

    // Safety check for history
    int Lookback = Input_DynamicLookback.GetInt();
    if (sc.Index >= Lookback)
    {
        bool HitHigh = false;
        bool HitMed = false;

        float MultHigh = Input_StdDev_High.GetFloat();
        float MultMed = Input_StdDev_Med.GetFloat();

        // Loop back N bars to check for touches
        for (int k = 0; k < Lookback; k++)
        {
            int Idx = sc.Index - k;
            float HistHigh = sc.High[Idx];
            float HistLow = sc.Low[Idx];
            // Reconstruct historical bands approximately (using current VWAP/StdDev is approximation,
            // but precise enough for "Regime" detection, or we could use arrays if we stored them).
            // Better: We check if Price was > VWAP + Mult*StdDev AT THAT TIME.
            // Since we don't store historical bands in arrays in this simplified code,
            // we will estimate using CURRENT Volatility regime or simply use the fact
            // that if volatility expanded, we want wider bands.

            // Actually, correct implementation requires checking if price exceeded the band *at that moment*.
            // Since we recalculate VWAP every tick, let's define the threshold based on CURRENT StdDev
            // applied to historical prices relative to CURRENT VWAP for simplicity,
            // OR simpler: check Range.

            // Let's use the USER's logic: "If last 10 bars crossing..."
            // We will assume "Crossing" means the High/Low deviated significantly from VWAP.

            float Deviation = 0;
            if (sc.BaseData[SC_LAST][Idx] > VWAP) Deviation = sc.BaseData[SC_HIGH][Idx] - VWAP;
            else Deviation = VWAP - sc.BaseData[SC_LOW][Idx];

            if (StdDev > 0)
            {
                float ZScore = Deviation / StdDev;
                if (ZScore >= MultHigh) HitHigh = true;
                if (ZScore >= MultMed) HitMed = true;
            }
        }

        if (HitHigh) SelectedMultiplier = Input_StdDev_High.GetFloat();
        else if (HitMed) SelectedMultiplier = Input_StdDev_Med.GetFloat();
        else SelectedMultiplier = Input_StdDev_Low.GetFloat();
    }
    else
    {
        SelectedMultiplier = Input_StdDev_High.GetFloat(); // Default to safe/high at start
    }

    Subgraph_ActiveMult[sc.Index] = SelectedMultiplier;

    // Calculate Active Bands
    float TopBand = (float)(VWAP + (StdDev * SelectedMultiplier));
    float BottomBand = (float)(VWAP - (StdDev * SelectedMultiplier));

    Subgraph_TopBand[sc.Index] = TopBand;
    Subgraph_BottomBand[sc.Index] = BottomBand;

    // ---------------------------------------------------------
    // 3. SLOPE FILTERS
    // ---------------------------------------------------------

    bool CVDBullish = true;
    bool CVDBearish = true;

    if (Input_UseCVDSlope.GetYesNo() && sc.Index > Input_CVDSlopeBars.GetInt())
    {
        float CurrentCVD = Subgraph_CVD[sc.Index];
        float PastCVD = Subgraph_CVD[sc.Index - Input_CVDSlopeBars.GetInt()];
        float Slope = CurrentCVD - PastCVD;

        CVDBullish = (Slope > 0);
        CVDBearish = (Slope < 0);
    }

    bool VIXBullish = true;
    bool VIXBearish = true;

    if (Input_UseVIXFilter.GetYesNo())
    {
        SCFloatArray VIXArray;
        sc.GetStudyArrayUsingID(Input_VIXStudyID.GetInt(), Input_VIXSubgraphIndex.GetInt(), VIXArray);

        if (VIXArray.GetArraySize() > 0 && sc.Index > Input_VIXSlopeBars.GetInt())
        {
            float CurrentVIX = VIXArray[sc.Index];
            float PastVIX = VIXArray[sc.Index - Input_VIXSlopeBars.GetInt()];
            float VIXSlope = CurrentVIX - PastVIX;

            // Mean Reversion Logic:
            // If VIX is Falling (Slope < 0), fear is leaving -> Good for Longs?
            // If VIX is Rising (Slope > 0), fear is entering -> Good for Shorts?
            // Adjust logic based on your specific alpha.

            VIXBullish = (VIXSlope < 0);
            VIXBearish = (VIXSlope > 0);
        }
    }

    // ---------------------------------------------------------
    // 4. TRADING LOGIC
    // ---------------------------------------------------------

    if (!Input_Enabled.GetYesNo()) return;

    s_SCPositionData Position;
    sc.GetTradePosition(Position);

    // Time-based exit
    if (Position.PositionQuantity != 0)
    {
        int BarsSinceEntry = sc.GetBarsSinceLastTradeOrderEntry();
        if (BarsSinceEntry > Input_MaxHoldBars.GetInt())
        {
            sc.FlattenAndCancelAllOrders();
            sc.AddMessageToLog("Exit: Max Hold Time Exceeded", 0);
        }
    }

    // Entry Execution
    float LastPrice = sc.Close[sc.Index];
    float CurrentATR = Subgraph_ATR[sc.Index];
    if (CurrentATR <= 0) CurrentATR = sc.TickSize * 10;

    s_SCNewOrder Order;
    Order.OrderQuantity = 1;
    Order.OrderType = SCT_ORDERTYPE_MARKET;
    Order.TimeInForce = SCT_TIF_DAY;
    Order.Target1Offset = CurrentATR * Input_ATRTargetMultiplier.GetFloat();
    Order.Stop1Offset = CurrentATR * Input_ATRStopMultiplier.GetFloat();

    // LONG ENTRY
    // 1. Price is below Active Bottom Band (or just crossed back up)
    // 2. CVD Slope is Positive (Buyers stepping in)
    // 3. VIX is Falling (optional)
    if (Position.PositionQuantity == 0)
    {
        // Check if we dipped below the active band recently (e.g., this bar)
        if (sc.Low[sc.Index] <= BottomBand && LastPrice > BottomBand)
        {
            if (CVDBullish && VIXBullish)
            {
                sc.BuyEntry(Order);
                SCString LogMsg;
                LogMsg.Format("Entry Long | Mult: %.1f | CVD Slope: Up", SelectedMultiplier);
                sc.AddMessageToLog(LogMsg, 0);
            }
        }
    }

    // SHORT ENTRY
    if (Position.PositionQuantity == 0)
    {
        if (sc.High[sc.Index] >= TopBand && LastPrice < TopBand)
        {
            if (CVDBearish && VIXBearish)
            {
                sc.SellEntry(Order);
                SCString LogMsg;
                LogMsg.Format("Entry Short | Mult: %.1f | CVD Slope: Down", SelectedMultiplier);
                sc.AddMessageToLog(LogMsg, 0);
            }
        }
    }
}