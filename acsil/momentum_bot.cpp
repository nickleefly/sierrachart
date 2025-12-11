#include "sierrachart.h"

SCDLLName("XYL - Momentum Bot")

/*
    TRADING SETUPS:

    Setup A (Momentum): Classic mean reversion using CCI crosses gated by
    extreme Momentum Score (<70/>130), strictly disabled against strong trends.
    Setup B (Extreme Reversion): Statistical exhaustion play at 2.0 SD bands
    with Candle Reversal, requiring Price Slope confirmation (Gate > 0.05%)
    to validate the turn.
    Setup C (Trend Pullback): "Buy the Dip" logic triggering on SMA 100 touches
    during Strong Trends (Slope > 0.10%) while price remains in the shallow zone.
    Setup D (Extreme Breakout): Trend-following mode for violent moves
    (Slope > 0.25% or < -0.25%) using EMA 50 confirmation and 5-bar swing breakouts.
    FILTERS:
    - Chop Detection: Blocks A/C/D when slope flips > 2 times in 10 bars
    - Signal Spacing: Min 5 bars between signals
    - Virtual Position: Tracks trades to prevent clustering
*/


SCSFExport scsf_MomentumReversal(SCStudyInterfaceRef sc)
{
    // =========================================================================
    // 1. SUBGRAPH DEFINITIONS
    // =========================================================================

    // --- Main Chart Visuals ---
    SCSubgraphRef RSI           = sc.Subgraph[0];
    SCSubgraphRef CCI           = sc.Subgraph[2];
    SCSubgraphRef ATR           = sc.Subgraph[3];
    SCSubgraphRef Score         = sc.Subgraph[4];
    SCSubgraphRef DailyTrades   = sc.Subgraph[5];
    SCSubgraphRef LongSignal    = sc.Subgraph[6];
    SCSubgraphRef ShortSignal   = sc.Subgraph[7];

    // --- Visual Bands ---
    SCSubgraphRef Band_Top_20   = sc.Subgraph[12];
    SCSubgraphRef Band_Bot_20   = sc.Subgraph[15];

    // --- Hidden Calculations ---
    SCSubgraphRef VWAP          = sc.Subgraph[16];
    SCSubgraphRef SMA100        = sc.Subgraph[17];
    SCSubgraphRef EMA1000       = sc.Subgraph[18];
    SCSubgraphRef MFI           = sc.Subgraph[19];
    SCSubgraphRef ADX           = sc.Subgraph[20];
    SCSubgraphRef StochK        = sc.Subgraph[21];
    SCSubgraphRef CumDelta      = sc.Subgraph[22];
    SCSubgraphRef CCITempSMA    = sc.Subgraph[23];
    SCSubgraphRef ATR20         = sc.Subgraph[24];
    SCSubgraphRef EMA50         = sc.Subgraph[25];

    // --- Hidden Data ---
    SCSubgraphRef PriceSlope    = sc.Subgraph[26];
    SCSubgraphRef TrendLineZero = sc.Subgraph[27];
    SCSubgraphRef ChopState     = sc.Subgraph[28];

    // --- Active Trade Visuals ---
    SCSubgraphRef VisTarget     = sc.Subgraph[29];
    SCSubgraphRef VisStop       = sc.Subgraph[30];

    // =========================================================================
    // 2. PERSISTENT VARIABLES
    // =========================================================================

    int& LastDayDate            = sc.GetPersistentInt(0);
    double& CumVol              = sc.GetPersistentDouble(1);
    double& CumPV               = sc.GetPersistentDouble(2);
    double& CumP2V              = sc.GetPersistentDouble(3);
    int& DailyCount             = sc.GetPersistentInt(4);
    int& LastTradeIndex         = sc.GetPersistentInt(5);

    // Virtual Position (For Visual Backtesting)
    int& VirtPos                = sc.GetPersistentInt(6); // 0=Flat, 1=Long, -1=Short
    double& VirtEntryPrice      = sc.GetPersistentDouble(7);
    double& VirtStopPrice       = sc.GetPersistentDouble(8);
    double& VirtTargetPrice     = sc.GetPersistentDouble(9);
    int& LastExitIndex          = sc.GetPersistentInt(10); // Cooldown after exit
    int& LastSignalIndex        = sc.GetPersistentInt(11); // Track last signal bar
    int& LastVWAPBar            = sc.GetPersistentInt(12); // Track last VWAP processed bar

    // =========================================================================
    // 3. INPUTS
    // =========================================================================

    SCInputRef MaxDailyTrades      = sc.Input[0];
    SCInputRef ContractsPerTrade   = sc.Input[1];
    SCInputRef HardStopPercent     = sc.Input[2];
    SCInputRef TargetATRMult       = sc.Input[3];
    SCInputRef ExtTargetATRMult    = sc.Input[4];
    SCInputRef TrailTriggerATR     = sc.Input[5];
    SCInputRef TrailDistATR        = sc.Input[6];
    SCInputRef ATRLength           = sc.Input[7];
    SCInputRef TradeRTHOnly        = sc.Input[8];
    SCInputRef SendOrdersToService = sc.Input[9];
    SCInputRef MinSlopeThreshold   = sc.Input[10];
    SCInputRef ExtremeSlopeBlock   = sc.Input[11];
    SCInputRef SetupBSlopeGate     = sc.Input[12];
    SCInputRef ChopLookback        = sc.Input[13];
    SCInputRef MaxSlopeFlips       = sc.Input[14];
    SCInputRef MinBarsBetweenTrades= sc.Input[15];

    // =========================================================================
    // 4. CONFIGURATION (SetDefaults)
    // =========================================================================

    if (sc.SetDefaults)
    {
        sc.GraphName        = "XYL - Momentum Bot";
        sc.GraphRegion      = 0;
        sc.AutoLoop         = 1;
        sc.UpdateAlways     = 1;

        // --- Inputs ---
        MaxDailyTrades.Name = "Max Daily Trades";
        MaxDailyTrades.SetInt(15);

        ContractsPerTrade.Name = "Contracts Per Trade";
        ContractsPerTrade.SetInt(1);

        HardStopPercent.Name = "Hard Stop %";
        HardStopPercent.SetFloat(0.12f);

        TargetATRMult.Name = "Initial Target (ATR)";
        TargetATRMult.SetFloat(4.0f);

        ExtTargetATRMult.Name = "Extended Target (ATR)";
        ExtTargetATRMult.SetFloat(6.0f);

        TrailTriggerATR.Name = "Trailing Trigger (ATR)";
        TrailTriggerATR.SetFloat(2.5f);

        TrailDistATR.Name = "Trailing Dist (ATR)";
        TrailDistATR.SetFloat(0.7f);

        ATRLength.Name = "ATR Length";
        ATRLength.SetInt(14);

        TradeRTHOnly.Name = "Trade RTH Only";
        TradeRTHOnly.SetYesNo(false);

        SendOrdersToService.Name = "Send Orders to Trade Service";
        SendOrdersToService.SetYesNo(false);

        MinSlopeThreshold.Name = "Trend Detection Slope (%)";
        MinSlopeThreshold.SetFloat(0.10f); // ~7 pts on ES, ~26 pts on NQ

        ExtremeSlopeBlock.Name = "Kill Switch Slope (%)";
        ExtremeSlopeBlock.SetFloat(0.25f); // ~17 pts on ES, ~64 pts on NQ

        SetupBSlopeGate.Name = "Setup B Reversion Gate (%)";
        SetupBSlopeGate.SetFloat(0.05f); // ~3 pts on ES, ~13 pts on NQ

        ChopLookback.Name = "Chop Detection Lookback";
        ChopLookback.SetInt(10);

        MaxSlopeFlips.Name = "Max Slope Flips Allowed";
        MaxSlopeFlips.SetInt(3);  // Lower = more sensitive chop detection

        MinBarsBetweenTrades.Name = "Min Bars Between Signals";
        MinBarsBetweenTrades.SetInt(5); // Reduced from 10

        // --- Visuals ---
        Band_Top_20.Name = "T2 std";
        Band_Top_20.DrawStyle = DRAWSTYLE_HIDDEN;
        Band_Top_20.PrimaryColor = RGB(255, 0, 0);
        Band_Top_20.LineWidth = 2;

        Band_Bot_20.Name = "B2 std";
        Band_Bot_20.DrawStyle = DRAWSTYLE_HIDDEN;
        Band_Bot_20.PrimaryColor = RGB(0, 255, 0);
        Band_Bot_20.LineWidth = 2;

        LongSignal.Name = "Buy Signal";
        LongSignal.DrawStyle = DRAWSTYLE_ARROW_UP;
        LongSignal.PrimaryColor = RGB(0, 255, 0);
        LongSignal.LineWidth = 4;

        ShortSignal.Name = "Sell Signal";
        ShortSignal.DrawStyle = DRAWSTYLE_ARROW_DOWN;
        ShortSignal.PrimaryColor = RGB(255, 0, 0);
        ShortSignal.LineWidth = 4;

        VisTarget.Name = "Active Target";
        VisTarget.DrawStyle = DRAWSTYLE_DASH;
        VisTarget.PrimaryColor = RGB(0, 255, 0);
        VisTarget.LineWidth = 2;

        VisStop.Name = "Active Stop";
        VisStop.DrawStyle = DRAWSTYLE_DASH;
        VisStop.PrimaryColor = RGB(255, 0, 0);
        VisStop.LineWidth = 2;

        // --- Hidden ---
        VWAP.Name = "VWAP";
        VWAP.DrawStyle = DRAWSTYLE_IGNORE;

        RSI.Name = "RSI";
        RSI.DrawStyle = DRAWSTYLE_HIDDEN;

        CCI.Name = "CCI";
        CCI.DrawStyle = DRAWSTYLE_HIDDEN;

        ATR.Name = "ATR";
        ATR.DrawStyle = DRAWSTYLE_HIDDEN;

        SMA100.Name = "SMA 100";
        SMA100.DrawStyle = DRAWSTYLE_LINE;
        SMA100.PrimaryColor = RGB(0, 128, 255);

        EMA1000.Name = "EMA 1000";
        EMA1000.DrawStyle = DRAWSTYLE_IGNORE;

        EMA50.Name = "EMA 50";
        EMA50.DrawStyle = DRAWSTYLE_IGNORE;

        PriceSlope.Name = "Price Slope (%)";
        PriceSlope.DrawStyle = DRAWSTYLE_HIDDEN;

        TrendLineZero.Name = "Zero";
        TrendLineZero.DrawStyle = DRAWSTYLE_IGNORE;

        ChopState.Name = "Chop (1=True)";
        ChopState.DrawStyle = DRAWSTYLE_HIDDEN;

        sc.AllowMultipleEntriesInSameDirection = 0;
        sc.AllowOppositeEntryWithOpposingPositionOrOrders = 0;
        sc.CancelAllOrdersOnEntriesAndReversals = 0;
        sc.AllowOnlyOneTradePerBar = 1;
        sc.SupportAttachedOrdersForTrading = 1;
        sc.MaximumPositionAllowed = 10;

        return;
    }

    sc.SendOrdersToTradeService = SendOrdersToService.GetYesNo();

    // =========================================================================
    // 5. DATA & VWAP CALCULATION
    // =========================================================================

    if (sc.GetTradingDayDate(sc.Index) != LastDayDate)
    {
        CumVol              = 0.0;
        CumPV               = 0.0;
        CumP2V              = 0.0;
        LastDayDate         = sc.GetTradingDayDate(sc.Index);
        DailyCount          = 0;
        CumDelta[sc.Index]  = 0;
        VirtPos             = 0; // Reset Virtual Position
        LastVWAPBar         = -1; // Reset VWAP bar tracking
    }
    else
    {
        CumDelta[sc.Index] = CumDelta[sc.Index - 1] + (sc.AskVolume[sc.Index] - sc.BidVolume[sc.Index]);
    }

    // --- Find Day Start Bar Index (same as vwap_bands.cpp) ---
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
    }

    // --- VWAP Calculation (recalculate from day start - proven approach) ---
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

    double VWAPValue = 0.0;
    float StdDev = 0.0f;

    if (CumulativeVolume > 0)
    {
        VWAPValue = CumulativePV / CumulativeVolume;
        VWAP[sc.Index] = (float)VWAPValue;

        double MeanOfSquares = CumulativeP2V / CumulativeVolume;
        double Variance = MeanOfSquares - (VWAPValue * VWAPValue);
        if (Variance < 0) Variance = 0;
        StdDev = (float)sqrt(Variance);
    }
    else
    {
        VWAPValue = sc.Close[sc.Index];
        VWAP[sc.Index] = (float)VWAPValue;
    }

    // Bands
    Band_Top_20[sc.Index] = VWAP[sc.Index] + (2.0f * StdDev);
    Band_Bot_20[sc.Index] = VWAP[sc.Index] - (2.0f * StdDev);

    // Internal 0.5 SD
    float Band_Top_05 = VWAP[sc.Index] + (0.5f * StdDev);
    float Band_Bot_05 = VWAP[sc.Index] - (0.5f * StdDev);

    // =========================================================================
    // 6. TREND & CHOP DETECTION
    // =========================================================================

    sc.SimpleMovAvg(sc.Close, SMA100, sc.Index, 100);
    sc.ExponentialMovAvg(sc.Close, EMA1000, sc.Index, 1000);
    sc.ExponentialMovAvg(sc.Close, EMA50, sc.Index, 50);

    // 1. Slope (percentage of price movement for cross-instrument compatibility)
    float CurrentSlope = 0.0f;
    if (sc.Index >= 5 && sc.Close[sc.Index] > 0 && sc.Close[sc.Index - 5] > 0)
    {
        float PriceNow = sc.Close[sc.Index];
        float PricePrev = sc.Close[sc.Index - 5];
        float AbsoluteSlope = PriceNow - PricePrev;
        CurrentSlope = (AbsoluteSlope / PriceNow) * 100.0f;
    }
    PriceSlope[sc.Index] = CurrentSlope;

    // 2. Trend States
    float TrendThresh = MinSlopeThreshold.GetFloat();
    bool StrongUp     = (CurrentSlope > TrendThresh);
    bool StrongDown   = (CurrentSlope < -TrendThresh);

    // 3. Extreme States
    float KillThresh  = ExtremeSlopeBlock.GetFloat();
    bool IsExtremeUp  = (CurrentSlope > KillThresh);
    bool IsExtremeDown= (CurrentSlope < -KillThresh);

    // 4. Chop Detection
    int FlipCount = 0;
    int Lookback  = ChopLookback.GetInt();

    for (int i = 0; i < Lookback; i++)
    {
        int idx = sc.Index - i;
        if (idx <= 0)
            break;

        float sCurrent = PriceSlope[idx];
        float sPrev    = PriceSlope[idx-1];

        if ((sCurrent > 0 && sPrev < 0) || (sCurrent < 0 && sPrev > 0))
        {
            FlipCount++;
        }
    }

    bool IsChoppy = (FlipCount > MaxSlopeFlips.GetInt());
    ChopState[sc.Index] = IsChoppy ? 1.0f : 0.0f;  // 1.0 = True (Choppy), 0.0 = False

    // =========================================================================
    // 7. INDICATORS & SCORING
    // =========================================================================

    sc.ATR(sc.BaseData, ATR, sc.Index, ATRLength.GetInt(), MOVAVGTYPE_SIMPLE);
    sc.RSI(sc.Close, RSI, sc.Index, MOVAVGTYPE_SIMPLE, 14);
    sc.CCI(sc.Close, CCITempSMA, CCI, sc.Index, 14, 0.015f, MOVAVGTYPE_SIMPLE);
    sc.ADX(sc.BaseData, ADX, sc.Index, 14, 14);
    sc.SimpleMovAvg(ATR, ATR20, sc.Index, 20);

    // MFI
    double posFlow = 0.0;
    double negFlow = 0.0;
    for (int i = 0; i < 14; i++)
    {
        int idx = sc.Index - i;
        if (idx < 1) continue;

        float tpNow  = (sc.High[idx] + sc.Low[idx] + sc.Close[idx]) / 3.0f;
        float tpPrev = (sc.High[idx-1] + sc.Low[idx-1] + sc.Close[idx-1]) / 3.0f;

        if (tpNow > tpPrev)
            posFlow += (tpNow * sc.Volume[idx]);
        else if (tpNow < tpPrev)
            negFlow += (tpNow * sc.Volume[idx]);
    }
    MFI[sc.Index] = (posFlow + negFlow > 0) ? 100.0f - (100.0f / (1.0f + (float)(posFlow / negFlow))) : 50.0f;

    // Stoch
    float HH = sc.GetHighest(sc.High, 14);
    float LL = sc.GetLowest(sc.Low, 14);
    StochK[sc.Index] = (HH - LL != 0) ? 100.0f * (sc.Close[sc.Index] - LL) / (HH - LL) : 0.0f;

    // Scoring
    float cC = sc.Close[sc.Index];
    bool pHigh = (cC > SMA100[sc.Index] && cC > EMA1000[sc.Index] && cC > VWAP[sc.Index]);
    bool pLow  = (cC < SMA100[sc.Index] && cC < EMA1000[sc.Index] && cC < VWAP[sc.Index]);

    int sPrice = pHigh ? 50 : (pLow ? 0 : 25);
    int sRSI   = RSI[sc.Index] > 70 ? 25 : (RSI[sc.Index] < 30 ? 0 : 15);

    float vP   = VWAP[sc.Index - 20];
    float vm   = (vP > 0) ? ((VWAP[sc.Index] - vP) / vP) * 100.0f : 0.0f;
    int sVW    = vm > 1 ? 25 : (vm < -1 ? 0 : 15);

    int sADX   = ADX[sc.Index] > 40 ? 20 : (ADX[sc.Index] > 25 ? 10 : 0);
    int sStoch = StochK[sc.Index] > 80 ? 20 : (StochK[sc.Index] < 20 ? 0 : 12);
    int sCCI   = CCI[sc.Index] > 100 ? 20 : (CCI[sc.Index] < -100 ? 0 : 12);
    int sMFI   = MFI[sc.Index] > 80 ? 15 : (MFI[sc.Index] < 20 ? 0 : 10);
    int sVol   = (ATR[sc.Index] > ATR20[sc.Index] * 1.5) ? 10 : 5;
    int sDel   = (CumDelta[sc.Index] > CumDelta[sc.Index - 1]) ? 15 : 0;

    int totalScore = sPrice + sRSI + sVW + sADX + sStoch + sCCI + sMFI + sVol + sDel;
    Score[sc.Index] = (float)totalScore;

    // =========================================================================
    // 8. VIRTUAL TRADE STATE (Visual Backtesting)
    // =========================================================================

    // A. Check Virtual Exits
    if (VirtPos != 0)
    {
        VisTarget[sc.Index] = (float)VirtTargetPrice;
        VisStop[sc.Index]   = (float)VirtStopPrice;

        bool Exit = false;

        if (VirtPos == 1) // Long
        {
            if (sc.Low[sc.Index] <= VirtStopPrice)
            {
                Exit = true;
            }
            else if (sc.High[sc.Index] >= VirtTargetPrice)
            {
                Exit = true;
            }

            // Virtual Trailing
            if (!Exit && (sc.Close[sc.Index] - VirtEntryPrice) > (TrailTriggerATR.GetFloat() * ATR[sc.Index]))
            {
                float NewStop = sc.Close[sc.Index] - (TrailDistATR.GetFloat() * ATR[sc.Index]);
                if (NewStop > VirtStopPrice)
                    VirtStopPrice = NewStop;
            }
        }
        else if (VirtPos == -1) // Short
        {
            if (sc.High[sc.Index] >= VirtStopPrice)
            {
                Exit = true;
            }
            else if (sc.Low[sc.Index] <= VirtTargetPrice)
            {
                Exit = true;
            }

            // Virtual Trailing
            if (!Exit && (VirtEntryPrice - sc.Close[sc.Index]) > (TrailTriggerATR.GetFloat() * ATR[sc.Index]))
            {
                float NewStop = sc.Close[sc.Index] + (TrailDistATR.GetFloat() * ATR[sc.Index]);
                if (NewStop < VirtStopPrice)
                    VirtStopPrice = NewStop;
            }
        }

        if (Exit)
        {
            VirtPos = 0; // Trade Closed
            LastExitIndex = sc.Index; // Mark exit bar for cooldown
        }
    }

    // B. Sync with Real Trading (If Active)
    s_SCPositionData PosData;
    sc.GetTradePosition(PosData);

    if (PosData.PositionQuantity != 0)
    {
        VirtPos = (PosData.PositionQuantity > 0) ? 1 : -1;
    }

    // =========================================================================
    // 9. LOGIC & SIGNALS
    // =========================================================================

    if (sc.GetBarHasClosedStatus() != BHCS_BAR_HAS_CLOSED) return;

    // *** BLOCKER: No signals if trade (Virtual or Real) is active ***
    if (VirtPos != 0) return;

    // *** MINIMUM SPACING: Only allow signals every N bars ***
    if ((sc.Index - LastSignalIndex) < MinBarsBetweenTrades.GetInt()) return;

    bool InRTH = true;
    if (TradeRTHOnly.GetYesNo())
    {
        SCDateTime bt = sc.BaseDateTimeIn[sc.Index];
        int m = bt.GetHour() * 60 + bt.GetMinute();
        InRTH = (m >= 570 && m < 960);
    }

    bool CCIBuy  = CCI[sc.Index] > -100 && CCI[sc.Index-1] <= -100;
    bool CCISell = CCI[sc.Index] < 100 && CCI[sc.Index-1] >= 100;

    bool AtBot20 = sc.Close[sc.Index] < Band_Bot_20[sc.Index];
    bool AtTop20 = sc.Close[sc.Index] > Band_Top_20[sc.Index];

    bool InShallowLongZone  = (sc.Low[sc.Index] <= Band_Bot_05) && (sc.Close[sc.Index] >= Band_Bot_20[sc.Index]);
    bool InShallowShortZone = (sc.High[sc.Index] >= Band_Top_05) && (sc.Close[sc.Index] <= Band_Top_20[sc.Index]);

    bool CandleBullish      = sc.Close[sc.Index] > sc.Open[sc.Index-1];
    bool CandleBearish      = sc.Close[sc.Index] < sc.Open[sc.Index-1];
    bool TouchedSMA         = (sc.Low[sc.Index] <= SMA100[sc.Index] && sc.High[sc.Index] >= SMA100[sc.Index]);

    float Highest5          = sc.GetHighest(sc.High, 5, sc.Index - 1);
    float Lowest5           = sc.GetLowest(sc.Low, 5, sc.Index - 1);

    // --- SETUP A: MOMENTUM ---
    bool SetupA_Long  = (totalScore < 70 && CCIBuy);
    if (IsChoppy || StrongDown || IsExtremeDown) SetupA_Long = false;

    bool SetupA_Short = (totalScore > 130 && totalScore < 190 && CCISell);
    if (IsChoppy || StrongUp || IsExtremeUp)     SetupA_Short = false;

    // --- SETUP B: EXTREME REVERSION (2.0 SD) ---
    float GateVal = SetupBSlopeGate.GetFloat();
    bool SetupB_Long  = AtBot20 && CandleBullish && (CurrentSlope > GateVal);
    bool SetupB_Short = AtTop20 && CandleBearish && (CurrentSlope < -GateVal);

    // --- SETUP C: TREND PULLBACK ---
    bool SetupC_Long = false;
    if (!IsChoppy && StrongUp && InShallowLongZone && TouchedSMA && CandleBullish)
    {
        SetupC_Long = true;
    }

    bool SetupC_Short = false;
    if (!IsChoppy && StrongDown && InShallowShortZone && TouchedSMA && CandleBearish)
    {
        SetupC_Short = true;
    }

    // --- SETUP D: EXTREME BREAKOUT ---
    bool SetupD_Long = false;
    if (!IsChoppy && IsExtremeUp && sc.Close[sc.Index] > EMA50[sc.Index] && sc.Close[sc.Index] > Highest5)
    {
        SetupD_Long = true;
    }

    bool SetupD_Short = false;
    if (!IsChoppy && IsExtremeDown && sc.Close[sc.Index] < EMA50[sc.Index] && sc.Close[sc.Index] < Lowest5)
    {
        SetupD_Short = true;
    }

    // --- TRIGGERS ---
    bool DoLong  = InRTH && (SetupA_Long || SetupB_Long || SetupC_Long || SetupD_Long);
    if (IsExtremeDown && !SetupD_Long) DoLong = false;

    bool DoShort = InRTH && (SetupA_Short || SetupB_Short || SetupC_Short || SetupD_Short);
    if (IsExtremeUp && !SetupD_Short) DoShort = false;

    // Target Selection Logic
    float SlopeMag = (CurrentSlope > 0) ? CurrentSlope : -CurrentSlope;
    float UsedMult = (SlopeMag > MinSlopeThreshold.GetFloat())
        ? ExtTargetATRMult.GetFloat()
        : TargetATRMult.GetFloat();

    // =========================================================================
    // 10. EXECUTION & VIRTUAL UPDATE
    // =========================================================================

    if (DoLong)
    {
        // 1. Paint Signal & Mark
        LongSignal[sc.Index] = sc.Low[sc.Index] - (ATR[sc.Index] * 0.5f);
        LastSignalIndex = sc.Index;

        // 2. Set Virtual State
        VirtPos         = 1;
        VirtEntryPrice  = sc.Close[sc.Index];
        VirtStopPrice   = sc.Close[sc.Index] * (1.0f - (HardStopPercent.GetFloat() / 100.0f));
        VirtTargetPrice = sc.Close[sc.Index] + (UsedMult * ATR[sc.Index]);

        // 3. Real Execution
        if (DailyCount < MaxDailyTrades.GetInt() && sc.SendOrdersToTradeService)
        {
            s_SCNewOrder Order;
            Order.OrderQuantity          = ContractsPerTrade.GetInt();
            Order.OrderType              = SCT_ORDERTYPE_MARKET;
            Order.TimeInForce            = SCT_TIF_GOOD_TILL_CANCELED;
            Order.Stop1Offset            = sc.Close[sc.Index] * (HardStopPercent.GetFloat() / 100.0f);
            Order.Target1Offset          = UsedMult * ATR[sc.Index];
            Order.AttachedOrderTarget1Type = SCT_ORDERTYPE_LIMIT;
            Order.AttachedOrderStop1Type   = SCT_ORDERTYPE_STOP;

            if (sc.BuyEntry(Order) > 0)
            {
                DailyCount++;
                LastTradeIndex = sc.Index;
            }
        }
    }
    else if (DoShort)
    {
        // 1. Paint Signal & Mark
        ShortSignal[sc.Index] = sc.High[sc.Index] + (ATR[sc.Index] * 0.5f);
        LastSignalIndex = sc.Index;

        // 2. Set Virtual State
        VirtPos         = -1;
        VirtEntryPrice  = sc.Close[sc.Index];
        VirtStopPrice   = sc.Close[sc.Index] * (1.0f + (HardStopPercent.GetFloat() / 100.0f));
        VirtTargetPrice = sc.Close[sc.Index] - (UsedMult * ATR[sc.Index]);

        // 3. Real Execution
        if (DailyCount < MaxDailyTrades.GetInt() && sc.SendOrdersToTradeService)
        {
            s_SCNewOrder Order;
            Order.OrderQuantity          = ContractsPerTrade.GetInt();
            Order.OrderType              = SCT_ORDERTYPE_MARKET;
            Order.TimeInForce            = SCT_TIF_GOOD_TILL_CANCELED;
            Order.Stop1Offset            = sc.Close[sc.Index] * (HardStopPercent.GetFloat() / 100.0f);
            Order.Target1Offset          = UsedMult * ATR[sc.Index];
            Order.AttachedOrderTarget1Type = SCT_ORDERTYPE_LIMIT;
            Order.AttachedOrderStop1Type   = SCT_ORDERTYPE_STOP;

            if (sc.SellEntry(Order) > 0)
            {
                DailyCount++;
                LastTradeIndex = sc.Index;
            }
        }
    }

    DailyTrades[sc.Index] = (float)DailyCount;
}