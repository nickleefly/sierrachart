#include "sierrachart.h"

SCDLLName("XYL - Momentum Bot")

/*
    CHANGELOG:
    1. REFACTOR:
       - Setup A: Momentum (CCI + Score).
       - Setup B: Extreme Reversion (2.0 SD).
       - Setup C: Trend Pullback (SMA 100 + 0.5 SD Internal).
    2. LOGIC: Maintained strict slope filters and kill switches.
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

    // --- Visual Bands (2.0 SD Only) ---
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

    // --- Hidden Data ---
    SCSubgraphRef VWAP_Slope    = sc.Subgraph[25];
    SCSubgraphRef TrendLineZero = sc.Subgraph[26];

    // =========================================================================
    // 2. PERSISTENT VARIABLES
    // =========================================================================

    int& LastDayDate            = sc.GetPersistentInt(0);
    double& CumVol              = sc.GetPersistentDouble(1);
    double& CumPV               = sc.GetPersistentDouble(2);
    double& CumP2V              = sc.GetPersistentDouble(3);
    int& DailyCount             = sc.GetPersistentInt(4);
    int& LastTradeIndex         = sc.GetPersistentInt(5);

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

    // =========================================================================
    // 4. CONFIGURATION (SetDefaults)
    // =========================================================================

    if (sc.SetDefaults)
    {
        sc.GraphName        = "XYL - Momentum Bot V19 (Renamed Setups)";
        sc.GraphRegion      = 0;
        sc.AutoLoop         = 1;
        sc.UpdateAlways     = 1;

        // --- Inputs Configuration ---
        MaxDailyTrades.Name = "Max Daily Trades";
        MaxDailyTrades.SetInt(5);

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

        MinSlopeThreshold.Name = "Trend Detection Slope";
        MinSlopeThreshold.SetFloat(0.20f);

        ExtremeSlopeBlock.Name = "Kill Switch Slope";
        ExtremeSlopeBlock.SetFloat(8.0f);

        // --- Visual Bands Configuration ---
        Band_Top_20.Name = "Top 2.0 SD";
        Band_Top_20.DrawStyle = DRAWSTYLE_LINE;
        Band_Top_20.PrimaryColor = RGB(255, 0, 0);
        Band_Top_20.LineWidth = 2;

        Band_Bot_20.Name = "Bot 2.0 SD";
        Band_Bot_20.DrawStyle = DRAWSTYLE_IGNORE; // Hidden
        Band_Bot_20.PrimaryColor = RGB(0, 255, 0);
        Band_Bot_20.LineWidth = 2;

        // --- Signals Configuration ---
        LongSignal.Name = "Buy Signal";
        LongSignal.DrawStyle = DRAWSTYLE_ARROW_UP;
        LongSignal.PrimaryColor = RGB(0, 255, 0);
        LongSignal.LineWidth = 4;

        ShortSignal.Name = "Sell Signal";
        ShortSignal.DrawStyle = DRAWSTYLE_ARROW_DOWN;
        ShortSignal.PrimaryColor = RGB(255, 0, 0);
        ShortSignal.LineWidth = 4;

        // --- Hidden Subgraphs ---
        VWAP_Slope.Name = "VWAP 5-Bar Slope";
        VWAP_Slope.DrawStyle = DRAWSTYLE_IGNORE;

        TrendLineZero.Name = "Slope Zero Line";
        TrendLineZero.DrawStyle = DRAWSTYLE_IGNORE;

        VWAP.Name = "VWAP";
        VWAP.DrawStyle = DRAWSTYLE_IGNORE;

        RSI.Name = "RSI";
        RSI.DrawStyle = DRAWSTYLE_IGNORE;

        CCI.Name = "CCI";
        CCI.DrawStyle = DRAWSTYLE_IGNORE;

        ATR.Name = "ATR";
        ATR.DrawStyle = DRAWSTYLE_IGNORE;

        SMA100.Name = "SMA 100";
        SMA100.DrawStyle = DRAWSTYLE_LINE;
        SMA100.PrimaryColor = RGB(0, 128, 255);

        EMA1000.Name = "EMA 1000";
        EMA1000.DrawStyle = DRAWSTYLE_IGNORE;

        // --- System Flags ---
        sc.AllowMultipleEntriesInSameDirection = 0;
        sc.AllowOppositeEntryWithOpposingPositionOrOrders = 0;
        sc.CancelAllOrdersOnEntriesAndReversals = 0;
        sc.AllowOnlyOneTradePerBar = 1;
        sc.SupportAttachedOrdersForTrading = 1;
        sc.MaximumPositionAllowed = 10;

        return;
    }

    // Apply Live Trading Flag
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
    }
    else
    {
        CumDelta[sc.Index] = CumDelta[sc.Index - 1] + (sc.AskVolume[sc.Index] - sc.BidVolume[sc.Index]);
    }

    float Price = sc.BaseData[SC_LAST][sc.Index];
    float Vol   = sc.Volume[sc.Index];

    CumVol += Vol;
    CumPV  += (Price * Vol);
    CumP2V += (Price * Price * Vol);

    float StdDev = 0.0f;
    if (CumVol > 0)
    {
        VWAP[sc.Index] = (float)(CumPV / CumVol);
        double Variance = (CumP2V / CumVol) - (VWAP[sc.Index] * VWAP[sc.Index]);

        if (Variance > 0)
        {
            StdDev = (float)sqrt(Variance);
        }
    }
    else
    {
        VWAP[sc.Index] = Price;
    }

    // --- Main Bands (Only 2.0 Used) ---
    Band_Top_20[sc.Index] = VWAP[sc.Index] + (2.0f * StdDev);
    Band_Bot_20[sc.Index] = VWAP[sc.Index] - (2.0f * StdDev);

    // --- Shallow Bands (Setup C Internal - 0.5 SD) ---
    float Band_Top_05 = VWAP[sc.Index] + (0.5f * StdDev);
    float Band_Bot_05 = VWAP[sc.Index] - (0.5f * StdDev);

    // =========================================================================
    // 6. TREND & KILL SWITCH
    // =========================================================================

    sc.SimpleMovAvg(sc.Close, SMA100, sc.Index, 100);
    sc.ExponentialMovAvg(sc.Close, EMA1000, sc.Index, 1000);

    // 1. Slope Calculation (5-Bar)
    float CurrentSlope = VWAP[sc.Index] - VWAP[sc.Index - 5];
    VWAP_Slope[sc.Index] = CurrentSlope;
    TrendLineZero[sc.Index] = 0.0f;

    // 2. Trend Sensitivity
    float TrendThresh = MinSlopeThreshold.GetFloat();
    bool StrongUp     = (CurrentSlope > TrendThresh);
    bool StrongDown   = (CurrentSlope < -TrendThresh);

    // 3. Kill Switch Thresholds
    float KillThresh  = ExtremeSlopeBlock.GetFloat();

    // BLOCK LOGIC
    // If Slope < -8.0, Longs are FORBIDDEN
    bool KillSwitchLong = (CurrentSlope < -KillThresh);

    // If Slope > 8.0, Shorts are FORBIDDEN
    bool KillSwitchShort = (CurrentSlope > KillThresh);

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
    // 8. TRADE MANAGEMENT
    // =========================================================================

    s_SCPositionData PosData;
    sc.GetTradePosition(PosData);

    if (PosData.PositionQuantity != 0)
    {
        double PnL = PosData.PositionQuantity > 0
            ? (sc.Close[sc.Index] - PosData.AveragePrice)
            : (PosData.AveragePrice - sc.Close[sc.Index]);

        double CurATR = ATR[sc.Index];

        // --- Trailing Stop ---
        if (CurATR > 0 && PnL >= (TrailTriggerATR.GetFloat() * CurATR))
        {
            double NewStop = (PosData.PositionQuantity > 0)
                ? sc.Close[sc.Index] - (TrailDistATR.GetFloat() * CurATR)
                : sc.Close[sc.Index] + (TrailDistATR.GetFloat() * CurATR);

            int idx = 0;
            s_SCTradeOrder Ord;
            while (sc.GetOrderByIndex(idx, Ord) != SCTRADING_ORDER_ERROR)
            {
                if (Ord.OrderStatusCode == SCT_OSC_OPEN && Ord.Symbol == sc.Symbol)
                {
                    bool IsStop = (PosData.PositionQuantity > 0 && Ord.OrderType == SCT_ORDERTYPE_STOP && Ord.BuySell == BSE_SELL) ||
                                  (PosData.PositionQuantity < 0 && Ord.OrderType == SCT_ORDERTYPE_STOP && Ord.BuySell == BSE_BUY);

                    if (IsStop)
                    {
                        bool Better = (PosData.PositionQuantity > 0 && NewStop > Ord.Price1) ||
                                      (PosData.PositionQuantity < 0 && NewStop < Ord.Price1);

                        if (Better)
                        {
                            s_SCNewOrder Mod;
                            Mod.InternalOrderID = Ord.InternalOrderID;
                            Mod.Price1 = NewStop;
                            sc.ModifyOrder(Mod);
                        }
                    }
                }
                idx++;
            }
        }

        // --- Dynamic Target ---
        bool ExtTgt = (PosData.PositionQuantity > 0 && CCI[sc.Index] < 80) ||
                      (PosData.PositionQuantity < 0 && CCI[sc.Index] > -80);

        if (ExtTgt)
        {
            double NewTgt = (PosData.PositionQuantity > 0)
                ? PosData.AveragePrice + (ExtTargetATRMult.GetFloat() * CurATR)
                : PosData.AveragePrice - (ExtTargetATRMult.GetFloat() * CurATR);

            int idx = 0;
            s_SCTradeOrder Ord;
            while (sc.GetOrderByIndex(idx, Ord) != SCTRADING_ORDER_ERROR)
            {
                if (Ord.OrderStatusCode == SCT_OSC_OPEN && Ord.Symbol == sc.Symbol)
                {
                    bool IsTgt = (PosData.PositionQuantity > 0 && Ord.OrderType == SCT_ORDERTYPE_LIMIT && Ord.BuySell == BSE_SELL) ||
                                 (PosData.PositionQuantity < 0 && Ord.OrderType == SCT_ORDERTYPE_LIMIT && Ord.BuySell == BSE_BUY);

                    if (IsTgt)
                    {
                        bool Move = (PosData.PositionQuantity > 0 && NewTgt > Ord.Price1) ||
                                    (PosData.PositionQuantity < 0 && NewTgt < Ord.Price1);

                        if (Move)
                        {
                            s_SCNewOrder Mod;
                            Mod.InternalOrderID = Ord.InternalOrderID;
                            Mod.Price1 = NewTgt;
                            sc.ModifyOrder(Mod);
                        }
                    }
                }
                idx++;
            }
        }
    }

    // =========================================================================
    // 9. LOGIC DEFINITIONS
    // =========================================================================

    if (sc.GetBarHasClosedStatus() != BHCS_BAR_HAS_CLOSED) return;

    bool InRTH = true;
    if (TradeRTHOnly.GetYesNo())
    {
        SCDateTime bt = sc.BaseDateTimeIn[sc.Index];
        int m = bt.GetHour() * 60 + bt.GetMinute();
        InRTH = (m >= 570 && m < 960);
    }

    bool CCIBuy  = CCI[sc.Index] > -100 && CCI[sc.Index-1] <= -100;
    bool CCISell = CCI[sc.Index] < 100 && CCI[sc.Index-1] >= 100;

    // --- Band Zone Logic ---
    bool AtBot20 = sc.Close[sc.Index] < Band_Bot_20[sc.Index];
    bool AtTop20 = sc.Close[sc.Index] > Band_Top_20[sc.Index];

    // --- Shallow Pullback Zone Logic (Setup C Internal) ---
    // Price must be below 0.5 internal bands (Dip) but above Bottom 2.0
    bool InShallowLongZone  = (sc.Low[sc.Index] <= Band_Bot_05) && (sc.Close[sc.Index] >= Band_Bot_20[sc.Index]);
    bool InShallowShortZone = (sc.High[sc.Index] >= Band_Top_05) && (sc.Close[sc.Index] <= Band_Top_20[sc.Index]);

    bool CandleBullish      = sc.Close[sc.Index] > sc.Open[sc.Index-1];
    bool CandleBearish      = sc.Close[sc.Index] < sc.Open[sc.Index-1];

    // --- SMA Confluence Logic ---
    bool TouchedSMA = (sc.Low[sc.Index] <= SMA100[sc.Index] && sc.High[sc.Index] >= SMA100[sc.Index]);

    // =========================================================================
    // 10. ENTRY LOGIC SETUP
    // =========================================================================

    // --- SETUP A: MOMENTUM (Classic) ---
    bool SetupA_Long  = (totalScore < 70 && CCIBuy);
    if (StrongDown) SetupA_Long = false; // Block Longs if Counter Trend

    bool SetupA_Short = (totalScore > 130 && totalScore < 190 && CCISell);
    if (StrongUp) SetupA_Short = false; // Block Shorts if Counter Trend

    // --- SETUP B: EXTREME REVERSION (2.0 SD) ---
    bool SetupB_Long  = AtBot20 && CandleBullish;
    bool SetupB_Short = AtTop20 && CandleBearish;

    // --- SETUP C: TREND PULLBACK (SMA + 0.5 SD) ---
    bool SetupC_Long = false;
    if (StrongUp && InShallowLongZone && TouchedSMA && CandleBullish)
    {
        SetupC_Long = true; // Buy off SMA100/0.5SD
    }

    bool SetupC_Short = false;
    if (StrongDown && InShallowShortZone && TouchedSMA && CandleBearish)
    {
        SetupC_Short = true; // Sell off SMA100/0.5SD
    }

    // --- FINAL TRIGGER + KILL SWITCH ---
    bool DoLong  = InRTH && (SetupA_Long || SetupB_Long || SetupC_Long);

    // KILL SWITCH: If Slope < -8.0, NO LONGS allowed
    if (KillSwitchLong)
        DoLong = false;

    bool DoShort = InRTH && (SetupA_Short || SetupB_Short || SetupC_Short);

    // KILL SWITCH: If Slope > 8.0, NO SHORTS allowed
    if (KillSwitchShort)
        DoShort = false;

    if (DoLong)
        LongSignal[sc.Index] = sc.Low[sc.Index] - (ATR[sc.Index] * 0.5f);

    if (DoShort)
        ShortSignal[sc.Index] = sc.High[sc.Index] + (ATR[sc.Index] * 0.5f);

    // =========================================================================
    // 11. ENTRY EXECUTION
    // =========================================================================

    if (DailyCount < MaxDailyTrades.GetInt() && InRTH)
    {
        if (PosData.PositionQuantity == 0 && sc.Index != LastTradeIndex)
        {
            s_SCNewOrder Order;
            Order.OrderQuantity          = ContractsPerTrade.GetInt();
            Order.OrderType              = SCT_ORDERTYPE_MARKET;
            Order.TimeInForce            = SCT_TIF_GOOD_TILL_CANCELED;

            double StopOffset            = sc.Close[sc.Index] * (HardStopPercent.GetFloat() / 100.0f);
            Order.Stop1Offset            = StopOffset;

            Order.Target1Offset          = TargetATRMult.GetFloat() * ATR[sc.Index];

            Order.AttachedOrderTarget1Type = SCT_ORDERTYPE_LIMIT;
            Order.AttachedOrderStop1Type   = SCT_ORDERTYPE_STOP;

            if (DoLong)
            {
                if (sc.BuyEntry(Order) > 0)
                {
                    DailyCount++;
                    LastTradeIndex = sc.Index;

                    SCString msg;
                    msg.Format("XYL V19: LONG (A:%d B:%d C:%d Sc:%d Slope:%.2f)",
                        SetupA_Long, SetupB_Long, SetupC_Long, totalScore, CurrentSlope);
                    sc.AddMessageToLog(msg, 1);
                }
            }
            else if (DoShort)
            {
                if (sc.SellEntry(Order) > 0)
                {
                    DailyCount++;
                    LastTradeIndex = sc.Index;

                    SCString msg;
                    msg.Format("XYL V19: SHORT (A:%d B:%d C:%d Sc:%d Slope:%.2f)",
                        SetupA_Short, SetupB_Short, SetupC_Short, totalScore, CurrentSlope);
                    sc.AddMessageToLog(msg, 1);
                }
            }
        }
    }

    DailyTrades[sc.Index] = (float)DailyCount;
}