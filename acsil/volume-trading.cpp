#include "sierrachart.h"

SCDLLName("Volume Spike Trading Bot")

SCSFExport scsf_VolumeBasedTradingBot(SCStudyInterfaceRef sc)
{
    // Subgraphs
    SCSubgraphRef BuySignal = sc.Subgraph[0];
    SCSubgraphRef SellSignal = sc.Subgraph[1];
    SCSubgraphRef VolumeTriggerLong = sc.Subgraph[2];
    SCSubgraphRef VolumeTriggerShort = sc.Subgraph[3];
    // Hidden subgraph for ATR calculation (required by API)
    SCSubgraphRef ATRSubgraph = sc.Subgraph[4];

    // Inputs
    SCInputRef EnableTrading = sc.Input[0];
    SCInputRef ContractSize = sc.Input[1];
    SCInputRef VolumeMultiplier = sc.Input[2];
    SCInputRef TargetATRMultiplier = sc.Input[3];
    SCInputRef HardStopATRMultiplier = sc.Input[4];
    SCInputRef TimeStopSeconds = sc.Input[5];
    SCInputRef TradingStartTime = sc.Input[6];
    SCInputRef TradingEndTime = sc.Input[7];
    SCInputRef AvoidOpenMinutes = sc.Input[8];
    SCInputRef TickMoveThreshold = sc.Input[9];

    // Persistent variables
    int& LastTradeBarIndex = sc.GetPersistentInt(1);
    int& TradeActive = sc.GetPersistentInt(2);
    float& DailyPnL = sc.GetPersistentFloat(1);
    int& LastDayProcessed = sc.GetPersistentInt(3);
    int& TradeCount = sc.GetPersistentInt(4);
    float& TotalProfit = sc.GetPersistentFloat(2);
    int& WinningTrades = sc.GetPersistentInt(5);
    int& LosingTrades = sc.GetPersistentInt(6);
    float& MaxDrawdown = sc.GetPersistentFloat(3);
    float& PeakEquity = sc.GetPersistentFloat(4);
    float& LastBuyPrice = sc.GetPersistentFloat(5);
    float& LastSellPrice = sc.GetPersistentFloat(6);
    int& BuyVolumeTrigger = sc.GetPersistentInt(7);
    int& SellVolumeTrigger = sc.GetPersistentInt(8);
    SCDateTime& EntryTime = sc.GetPersistentSCDateTime(1);

    if (sc.SetDefaults)
    {
        sc.GraphName = "Volume Spike Trading Bot";
        sc.AutoLoop = 1;
        sc.GraphRegion = 0;
        sc.FreeDLL = 0;
        
        // Enable market depth data
        sc.UsesMarketDepthData = 1;

        EnableTrading.Name = "Enable Trading";
        EnableTrading.SetYesNo(0);

        ContractSize.Name = "Contract Size";
        ContractSize.SetInt(1);
        ContractSize.SetIntLimits(1, 100);

        VolumeMultiplier.Name = "Volume Multiplier (x Avg Depth)";
        VolumeMultiplier.SetInt(3);
        VolumeMultiplier.SetIntLimits(2, 10);

        TargetATRMultiplier.Name = "Target ATR Multiplier";
        TargetATRMultiplier.SetFloat(4.0f);
        TargetATRMultiplier.SetFloatLimits(1.0f, 10.0f);

        HardStopATRMultiplier.Name = "Hard Stop ATR Multiplier";
        HardStopATRMultiplier.SetFloat(3.0f);
        HardStopATRMultiplier.SetFloatLimits(1.0f, 10.0f);

        TimeStopSeconds.Name = "Time Stop (Seconds)";
        TimeStopSeconds.SetInt(30);
        TimeStopSeconds.SetIntLimits(5, 300);

        TradingStartTime.Name = "Trading Start Time";
        TradingStartTime.SetTime(HMS_TIME(9, 30, 0));

        TradingEndTime.Name = "Trading End Time";
        TradingEndTime.SetTime(HMS_TIME(16, 0, 0));

        AvoidOpenMinutes.Name = "Avoid Minutes After Open";
        AvoidOpenMinutes.SetInt(3);
        AvoidOpenMinutes.SetIntLimits(0, 30);

        TickMoveThreshold.Name = "Tick Move Threshold";
        TickMoveThreshold.SetInt(3);
        TickMoveThreshold.SetIntLimits(1, 20);

        BuySignal.Name = "Buy Signal";
        BuySignal.DrawStyle = DRAWSTYLE_ARROW_UP;
        BuySignal.PrimaryColor = RGB(0, 255, 0);
        BuySignal.LineWidth = 3;

        SellSignal.Name = "Sell Signal";
        SellSignal.DrawStyle = DRAWSTYLE_ARROW_DOWN;
        SellSignal.PrimaryColor = RGB(255, 0, 0);
        SellSignal.LineWidth = 3;

        VolumeTriggerLong.Name = "Volume Trigger Long";
        VolumeTriggerLong.DrawStyle = DRAWSTYLE_DASH;
        VolumeTriggerLong.PrimaryColor = RGB(0, 200, 200);
        VolumeTriggerLong.LineWidth = 1;

        VolumeTriggerShort.Name = "Volume Trigger Short";
        VolumeTriggerShort.DrawStyle = DRAWSTYLE_DASH;
        VolumeTriggerShort.PrimaryColor = RGB(200, 0, 200);
        VolumeTriggerShort.LineWidth = 1;

        return;
    }

    // 1. Calculate ATR
    sc.ATR(sc.BaseDataIn, ATRSubgraph, 14, MOVAVGTYPE_SIMPLE);
    float AvgRange = ATRSubgraph[sc.Index];
    if (AvgRange <= 0.0001f) return;

    // 2. Time Calculations
    int CurrentDate = sc.BaseDateTimeIn.DateAt(sc.Index);
    SCDateTime BarDateTime = sc.BaseDateTimeIn[sc.Index];
    int BarHour, BarMinute, BarSecond;
    BarDateTime.GetTimeHMS(BarHour, BarMinute, BarSecond);
    int CurrentMinutes = BarHour * 60 + BarMinute;

    int StartTimeVal = TradingStartTime.GetTime();
    int StartMinutes = (StartTimeVal / 10000) * 60 + (StartTimeVal % 10000) / 100;

    int EndTimeVal = TradingEndTime.GetTime();
    int EndMinutes = (EndTimeVal / 10000) * 60 + (EndTimeVal % 10000) / 100;

    // Calculate avoid period after open
    int AvoidUntilMinutes = StartMinutes + AvoidOpenMinutes.GetInt();

    // 3. PnL Tracking (Reset on new day)
    if (LastDayProcessed != CurrentDate)
    {
        DailyPnL = 0.0f;
        LastDayProcessed = CurrentDate;
    }

    float ClosingPrice = sc.Close[sc.Index];
    float TickSize = sc.TickSize;
    if (TickSize == 0) TickSize = 0.01f;

    // 4. Position Data
    s_SCPositionData PositionData;
    sc.GetTradePosition(PositionData);

    // 5. Calculate Average Market Depth Volume (Top 10 levels)
    double TotalBidVolume = 0, TotalAskVolume = 0;
    int BidLevelCount = 0, AskLevelCount = 0;

    for (int i = 0; i < 10; i++)
    {
        s_MarketDepthEntry BidEntry, AskEntry;
        if (sc.GetBidMarketDepthEntryAtLevel(BidEntry, i))
        {
            TotalBidVolume += BidEntry.Quantity;
            BidLevelCount++;
        }
        if (sc.GetAskMarketDepthEntryAtLevel(AskEntry, i))
        {
            TotalAskVolume += AskEntry.Quantity;
            AskLevelCount++;
        }
    }

    double AvgDepthVolume = 0;
    if (BidLevelCount + AskLevelCount > 0)
        AvgDepthVolume = (TotalBidVolume + TotalAskVolume) / (BidLevelCount + AskLevelCount);

    // Dynamic volume threshold based on average depth * multiplier
    int DynamicVolumeThreshold = static_cast<int>(AvgDepthVolume * VolumeMultiplier.GetInt());
    if (DynamicVolumeThreshold < 10) DynamicVolumeThreshold = 10; // Minimum threshold

    // 6. Volume Analysis
    float CurrentBidPrice = sc.Bid;
    float CurrentAskPrice = sc.Ask;

    unsigned int RecentBuyVolume = sc.GetRecentAskVolumeAtPrice(CurrentAskPrice);
    unsigned int RecentSellVolume = sc.GetRecentBidVolumeAtPrice(CurrentBidPrice);

    VolumeTriggerLong[sc.Index] = 0;
    VolumeTriggerShort[sc.Index] = 0;

    if (static_cast<int>(RecentBuyVolume) >= DynamicVolumeThreshold)
    {
        LastBuyPrice = CurrentAskPrice;
        BuyVolumeTrigger = 1;
        VolumeTriggerLong[sc.Index] = ClosingPrice;
    }

    if (static_cast<int>(RecentSellVolume) >= DynamicVolumeThreshold)
    {
        LastSellPrice = CurrentBidPrice;
        SellVolumeTrigger = 1;
        VolumeTriggerShort[sc.Index] = ClosingPrice;
    }

    bool BuyConditionMet = false;
    bool SellConditionMet = false;

    if (BuyVolumeTrigger)
    {
        float PriceMoveUp = ClosingPrice - LastBuyPrice;
        if (PriceMoveUp >= TickMoveThreshold.GetInt() * TickSize)
        {
            BuyConditionMet = true;
        }
        else if (PriceMoveUp <= -TickSize)
        {
            LastBuyPrice = 0.0f;
            BuyVolumeTrigger = 0;
        }
    }

    if (SellVolumeTrigger)
    {
        float PriceMoveDown = LastSellPrice - ClosingPrice;
        if (PriceMoveDown >= TickMoveThreshold.GetInt() * TickSize)
        {
            SellConditionMet = true;
        }
        else if (PriceMoveDown <= -TickSize)
        {
            LastSellPrice = 0.0f;
            SellVolumeTrigger = 0;
        }
    }

    // 7. Trade Management - Active Position
    if (TradeActive && PositionData.PositionQuantity != 0)
    {
        float EntryPrice = PositionData.AveragePrice;
        float TargetPrice;

        // Calculate target based on ATR multiplier
        if (PositionData.PositionQuantity > 0) // Long
        {
            TargetPrice = EntryPrice + (AvgRange * TargetATRMultiplier.GetFloat());
            
            // Check profit target hit
            if (ClosingPrice >= TargetPrice)
            {
                sc.FlattenAndCancelAllOrders();
                TradeActive = 0;
            }
        }
        else if (PositionData.PositionQuantity < 0) // Short
        {
            TargetPrice = EntryPrice - (AvgRange * TargetATRMultiplier.GetFloat());
            
            // Check profit target hit
            if (ClosingPrice <= TargetPrice)
            {
                sc.FlattenAndCancelAllOrders();
                TradeActive = 0;
            }
        }

        // Time-based stop: Exit if not in profit after N seconds
        if (TradeActive && EntryTime.IsDateSet())
        {
            SCDateTime CurrentTime = sc.CurrentSystemDateTime;
            double ElapsedSeconds = (CurrentTime.GetAsDouble() - EntryTime.GetAsDouble()) * 86400.0; // 86400 seconds per day

            if (ElapsedSeconds >= TimeStopSeconds.GetInt())
            {
                bool InProfit = false;
                if (PositionData.PositionQuantity > 0)
                    InProfit = ClosingPrice > EntryPrice;
                else if (PositionData.PositionQuantity < 0)
                    InProfit = ClosingPrice < EntryPrice;

                if (!InProfit)
                {
                    sc.FlattenAndCancelAllOrders();
                    TradeActive = 0;
                }
            }
        }
    }
    // Check if trade closed (Flat)
    else if (TradeActive && PositionData.PositionQuantity == 0)
    {
        TradeActive = 0;

        // Calculate PnL Change based on last trade's realized P/L
        float PnLChange = PositionData.LastTradeProfitLoss;

        DailyPnL += PnLChange;
        TotalProfit += PnLChange;

        if (PnLChange > 0) WinningTrades++;
        else if (PnLChange < 0) LosingTrades++;

        PeakEquity = max(PeakEquity, TotalProfit);
        MaxDrawdown = max(MaxDrawdown, PeakEquity - TotalProfit);
    }

    // 8. Entry Logic
    if (!EnableTrading.GetYesNo() ||
        LastTradeBarIndex == sc.Index ||
        CurrentMinutes < StartMinutes ||
        CurrentMinutes < AvoidUntilMinutes || // Avoid first N minutes after open
        CurrentMinutes >= EndMinutes ||
        PositionData.PositionQuantity != 0)
    {
        return;
    }

    int PositionSize = ContractSize.GetInt();

    s_SCNewOrder NewOrder;
    NewOrder.OrderQuantity = PositionSize;
    NewOrder.OrderType = SCT_ORDERTYPE_MARKET;

    // Attached Stop (Hard Stop at 3x ATR)
    float HardStopDistance = AvgRange * HardStopATRMultiplier.GetFloat();
    NewOrder.AttachedOrderStopAllType = SCT_ORDERTYPE_STOP;
    NewOrder.StopAllOffset = HardStopDistance;

    int Result = 0;

    if (BuyConditionMet)
    {
        BuySignal[sc.Index] = ClosingPrice;
        Result = sc.BuyEntry(NewOrder);
        if (Result > 0)
        {
            LastTradeBarIndex = sc.Index;
            TradeActive = 1;
            TradeCount++;
            LastBuyPrice = 0.0f;
            BuyVolumeTrigger = 0;
            EntryTime = sc.CurrentSystemDateTime;
        }
    }
    else if (SellConditionMet)
    {
        SellSignal[sc.Index] = ClosingPrice;
        Result = sc.SellEntry(NewOrder);
        if (Result > 0)
        {
            LastTradeBarIndex = sc.Index;
            TradeActive = 1;
            TradeCount++;
            LastSellPrice = 0.0f;
            SellVolumeTrigger = 0;
            EntryTime = sc.CurrentSystemDateTime;
        }
    }
}