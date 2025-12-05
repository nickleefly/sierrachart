#include "sierrachart.h"

SCDLLName("XYL - Momentum Bot V5")

SCSFExport scsf_MomentumReversal_MES_Signals_Fixed(SCStudyInterfaceRef sc)
{
    // --- VISIBLE Subgraphs ---
    SCSubgraphRef RSI         = sc.Subgraph[0];
    SCSubgraphRef MFI         = sc.Subgraph[1];
    SCSubgraphRef CCI         = sc.Subgraph[2];
    SCSubgraphRef ATR         = sc.Subgraph[3];
    SCSubgraphRef Score       = sc.Subgraph[4];
    SCSubgraphRef DailyTrades = sc.Subgraph[5];
    SCSubgraphRef LongSignal  = sc.Subgraph[6];
    SCSubgraphRef ShortSignal = sc.Subgraph[7];

    // --- HIDDEN Subgraphs ---
    SCSubgraphRef SMA100      = sc.Subgraph[10];
    SCSubgraphRef EMA1000     = sc.Subgraph[11];
    SCSubgraphRef VWAP        = sc.Subgraph[12];
    SCSubgraphRef ADX         = sc.Subgraph[13];
    SCSubgraphRef StochK      = sc.Subgraph[14];
    SCSubgraphRef ATR20       = sc.Subgraph[15];
    SCSubgraphRef CumDelta    = sc.Subgraph[16];
    SCSubgraphRef CCITempSMA  = sc.Subgraph[17];

    // --- Persistent Variables ---
    int& LastDayDate = sc.GetPersistentInt(0);
    double& CumVol   = sc.GetPersistentDouble(1);
    double& CumPV    = sc.GetPersistentDouble(2);
    int& DailyCount  = sc.GetPersistentInt(3);

    // --- Inputs ---
    SCInputRef MaxDailyTrades    = sc.Input[0];
    SCInputRef ContractsPerTrade = sc.Input[1];
    SCInputRef HardStopPercent   = sc.Input[2];
    SCInputRef TargetATRMult     = sc.Input[3];
    SCInputRef ATRLength         = sc.Input[4];
    SCInputRef TradeRTHOnly      = sc.Input[5];

    if (sc.SetDefaults)
    {
        sc.GraphName        = "XYL - Momentum Bot V5";
        sc.GraphRegion      = 0;
        sc.AutoLoop         = 1;

        // Input Defaults
        MaxDailyTrades.Name = "Max Daily Trades";
        MaxDailyTrades.SetInt(8);
        ContractsPerTrade.Name = "Contracts Per Trade";
        ContractsPerTrade.SetInt(2);
        HardStopPercent.Name = "Hard Stop %";
        HardStopPercent.SetFloat(0.20f);
        TargetATRMult.Name = "Target ATR Multiplier";
        TargetATRMult.SetFloat(8.0f);
        ATRLength.Name = "ATR Length";
        ATRLength.SetInt(14);
        TradeRTHOnly.Name = "Trade RTH Only";
        TradeRTHOnly.SetYesNo(true);

        RSI.Name = "RSI"; RSI.DrawStyle = DRAWSTYLE_LINE; RSI.PrimaryColor = RGB(0, 255, 255);
        MFI.Name = "MFI"; MFI.DrawStyle = DRAWSTYLE_LINE; MFI.PrimaryColor = RGB(255, 0, 255);
        CCI.Name = "CCI"; CCI.DrawStyle = DRAWSTYLE_LINE; CCI.PrimaryColor = RGB(255, 165, 0);
        ATR.Name = "ATR"; ATR.DrawStyle = DRAWSTYLE_LINE; ATR.PrimaryColor = RGB(255, 255, 0);
        
        Score.Name = "Momentum Score"; Score.DrawStyle = DRAWSTYLE_IGNORE; 
        DailyTrades.Name = "Daily Trades Count"; DailyTrades.DrawStyle = DRAWSTYLE_IGNORE;

        LongSignal.Name = "Buy Signal"; LongSignal.DrawStyle = DRAWSTYLE_ARROW_UP; LongSignal.PrimaryColor = RGB(0, 255, 0); LongSignal.LineWidth = 3;
        ShortSignal.Name = "Sell Signal"; ShortSignal.DrawStyle = DRAWSTYLE_ARROW_DOWN; ShortSignal.PrimaryColor = RGB(255, 0, 0); ShortSignal.LineWidth = 3;

        // Flags
        sc.AllowMultipleEntriesInSameDirection = 0; 
        sc.AllowOppositeEntryWithOpposingPositionOrOrders = 0;
        sc.CancelAllOrdersOnEntriesAndReversals = 1;
        sc.AllowOnlyOneTradePerBar = 1;
        sc.SupportAttachedOrdersForTrading = 1;
        sc.MaximumPositionAllowed = 12;

        return;
    }
    sc.SendOrdersToTradeService = false;

    // --- SAFETY CHECK: Prevent calculation on first 100 bars to avoid bad SMA data ---
    if (sc.Index < 100) return;

    // --- 1. Daily Reset + Manual VWAP ---
    if (sc.GetTradingDayDate(sc.Index) != LastDayDate)
    {
        CumVol = 0.0;
        CumPV = 0.0;
        LastDayDate = sc.GetTradingDayDate(sc.Index);
        DailyCount = 0; 
    }

    float TP = (sc.High[sc.Index] + sc.Low[sc.Index] + sc.Close[sc.Index]) / 3.0f;
    CumVol += sc.Volume[sc.Index];
    CumPV  += (TP * sc.Volume[sc.Index]);

    if (CumVol > 0) VWAP[sc.Index] = (float)(CumPV / CumVol);
    else VWAP[sc.Index] = sc.Close[sc.Index];

    // --- 2. Indicators ---
    sc.SimpleMovAvg(sc.Close, SMA100, sc.Index, 100);
    sc.ExponentialMovAvg(sc.Close, EMA1000, sc.Index, 1000);
    sc.ATR(sc.BaseDataIn, ATR, sc.Index, ATRLength.GetInt(), MOVAVGTYPE_SIMPLE);
    sc.RSI(sc.Close, RSI, sc.Index, MOVAVGTYPE_SIMPLE, 14);
    sc.CCI(sc.Close, CCITempSMA, CCI, sc.Index, 14, 0.015f, MOVAVGTYPE_SIMPLE);
    sc.ADX(sc.BaseDataIn, ADX, sc.Index, 14, 14);
    sc.SimpleMovAvg(ATR, ATR20, sc.Index, 20);
    sc.CumulativeDeltaTicks(sc.BaseDataIn, CumDelta, sc.Index, 0);

    // Stoch
    float HighestHigh = sc.GetHighest(sc.High, 14);
    float LowestLow   = sc.GetLowest(sc.Low, 14);
    float FastK = 0.0f;
    if (HighestHigh - LowestLow != 0)
        FastK = 100.0f * (sc.Close[sc.Index] - LowestLow) / (HighestHigh - LowestLow);
    StochK[sc.Index] = FastK;

    // MFI
    double posFlow = 0.0;
    double negFlow = 0.0;
    for (int i = 0; i < 14; i++)
    {
        int idx = sc.Index - i;
        // Logic already protected by sc.Index < 100 check above
        float tpNow = (sc.High[idx] + sc.Low[idx] + sc.Close[idx]) / 3.0f;
        float tpPrev = (sc.High[idx-1] + sc.Low[idx-1] + sc.Close[idx-1]) / 3.0f;
        if (tpNow > tpPrev) posFlow += (tpNow * sc.Volume[idx]);
        else if (tpNow < tpPrev) negFlow += (tpNow * sc.Volume[idx]);
    }
    float mfiVal = 50.0f;
    if (posFlow > 0 && negFlow > 0) mfiVal = 100.0f / (1.0f + (float)(negFlow / posFlow));
    else if (posFlow > 0) mfiVal = 100.0f;
    MFI[sc.Index] = mfiVal;

    // --- 5. Scoring Logic ---
    float cClose = sc.Close[sc.Index];
    bool priceAboveAll = (cClose > SMA100[sc.Index] && cClose > EMA1000[sc.Index] && cClose > VWAP[sc.Index]);
    bool priceBelowAll = (cClose < SMA100[sc.Index] && cClose < EMA1000[sc.Index] && cClose < VWAP[sc.Index]);
    int priceScore = priceAboveAll ? 50 : (priceBelowAll ? 0 : 25);

    int rsiScore = RSI[sc.Index] > 70 ? 25 : (RSI[sc.Index] < 30 ? 0 : (RSI[sc.Index] > 50 ? 15 : 5));
    
    // Fixed: Ensure index is valid for lookback
    float vwapPast = VWAP[sc.Index - 20]; 
    float vwmo = (vwapPast > 0) ? ((VWAP[sc.Index] - vwapPast) / vwapPast) * 100.0f : 0.0f;
    int vwmoScore = vwmo > 1 ? 25 : (vwmo < -1 ? 0 : (vwmo > 0 ? 15 : 5));

    int adxScore = ADX[sc.Index] > 40 ? 20 : (ADX[sc.Index] > 25 ? 10 : 0);
    int stochScore = StochK[sc.Index] > 80 ? 20 : (StochK[sc.Index] < 20 ? 0 : (StochK[sc.Index] > 50 ? 12 : 4));
    int cciScore = CCI[sc.Index] > 100 ? 20 : (CCI[sc.Index] < -100 ? 0 : (CCI[sc.Index] > 0 ? 12 : 4));
    int mfiScore = MFI[sc.Index] > 80 ? 15 : (MFI[sc.Index] < 20 ? 0 : (MFI[sc.Index] > 50 ? 10 : 3));

    bool highVol = ATR[sc.Index] > (ATR20[sc.Index] * 1.5f);
    bool lowVol = ATR[sc.Index] < (ATR20[sc.Index] * 0.5f);
    int volScore = highVol ? 10 : (lowVol ? 0 : 5);
    bool cumDeltaRising = CumDelta[sc.Index] > CumDelta[sc.Index - 1];
    int deltaScore = cumDeltaRising ? 15 : 0;

    int totalScore = priceScore + rsiScore + vwmoScore + adxScore + stochScore + cciScore + mfiScore + volScore + deltaScore;
    Score[sc.Index] = (float)totalScore;

    // --- 6. Trading & Signal Logic ---
    bool InRTH = true; 
    if (TradeRTHOnly.GetYesNo()) {
        SCDateTime bt = sc.BaseDateTimeIn[sc.Index];
        int mins = bt.GetHour() * 60 + bt.GetMinute();
        InRTH = (mins >= 570 && mins < 960);
    }

    bool CCIBuy  = CCI[sc.Index] > -100 && CCI[sc.Index-1] <= -100;
    bool CCISell = CCI[sc.Index] < 100 && CCI[sc.Index-1] >= 100;
    bool barClosed = (sc.GetBarHasClosedStatus() == BHCS_BAR_HAS_CLOSED);
    
    bool isLongSetup = (totalScore < 70 && CCIBuy && InRTH && barClosed);
    bool isShortSetup = (totalScore > 130 && totalScore < 190 && CCISell && InRTH && barClosed);

    if (isLongSetup) LongSignal[sc.Index] = sc.Low[sc.Index] - (ATR[sc.Index] * 0.5f);
    if (isShortSetup) ShortSignal[sc.Index] = sc.High[sc.Index] + (ATR[sc.Index] * 0.5f);

    // --- EXECUTE TRADES ---
    if (DailyCount < MaxDailyTrades.GetInt() && InRTH && barClosed)
    {
        // SAFETY: Check if we are flat before entering to prevent machine-gunning
        // because AllowMultipleEntries is ON.
        s_SCPositionData PosData;
        sc.GetTradePosition(PosData);
        bool isFlat = (PosData.PositionQuantity == 0);

        if (isFlat) 
        {
            s_SCNewOrder Order;
            Order.OrderQuantity = ContractsPerTrade.GetInt();
            Order.OrderType = SCT_ORDERTYPE_MARKET;
            Order.TimeInForce = SCT_TIF_GOOD_TILL_CANCELED;
            
            // --- FIX IS HERE: REMOVED * sc.TickSize ---
            Order.Target1Offset = TargetATRMult.GetFloat() * ATR[sc.Index];
            
            Order.Stop1Offset = (HardStopPercent.GetFloat() / 100.0f) * sc.Close[sc.Index];
            Order.AttachedOrderTarget1Type = SCT_ORDERTYPE_LIMIT;
            Order.AttachedOrderStop1Type = SCT_ORDERTYPE_STOP;
            Order.OCOGroup1Quantity = Order.OrderQuantity;

            SCString logMsg; 
            if (isLongSetup) 
            {
                if (sc.BuyEntry(Order) > 0) 
                {
                    DailyCount++;
                    logMsg.Format("XYL Bot: LONG Triggered. Score: %d | CCI: %.2f", totalScore, CCI[sc.Index]);
                    sc.AddMessageToLog(logMsg, 1);
                }
            }
            else if (isShortSetup) 
            {
                if (sc.SellEntry(Order) > 0) 
                {
                    DailyCount++;
                    logMsg.Format("XYL Bot: SHORT Triggered. Score: %d | CCI: %.2f", totalScore, CCI[sc.Index]);
                    sc.AddMessageToLog(logMsg, 1);
                }
            }
        }
    }
    
    DailyTrades[sc.Index] = (float)DailyCount;
}