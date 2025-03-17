#include "sierrachart.h"

SCDLLName("Volume Spike Trading Bot")

SCSFExport scsf_VolumeBasedTradingBot(SCStudyInterfaceRef sc)
{
    // Subgraphs
    SCSubgraphRef BuySignal = sc.Subgraph[0];
    SCSubgraphRef SellSignal = sc.Subgraph[1];
    SCSubgraphRef VolumeTriggerLong = sc.Subgraph[2];
    SCSubgraphRef VolumeTriggerShort = sc.Subgraph[3];
    
    // Inputs
    SCInputRef EnableTrading = sc.Input[0];
    SCInputRef TargetMultiplier = sc.Input[1];
    SCInputRef StopMultiplier = sc.Input[2];
    SCInputRef RiskPercentage = sc.Input[3];
    SCInputRef MaxDailyLoss = sc.Input[4];
    SCInputRef TradingStartTime = sc.Input[5];
    SCInputRef TradingEndTime = sc.Input[6];
    SCInputRef TickMoveThreshold = sc.Input[7];
    SCInputRef VolumeThreshold = sc.Input[8];
    SCInputRef FirstTargetMultiplier = sc.Input[9];
    SCInputRef SecondTargetMultiplier = sc.Input[10];
    SCInputRef MaxPositionSize = sc.Input[11]; // ADDED: Max position size

    // Persistent variables
    SCPersistentInt& LastTradeBarIndex = sc.PersistVars->i1;
    SCPersistentInt& TradeActive = sc.PersistVars->i2;
    SCPersistentFloat& DailyPnL = sc.PersistVars->f1;
    SCPersistentInt& LastDayProcessed = sc.PersistVars->i3;
    SCPersistentInt& TradeCount = sc.PersistVars->i4;
    SCPersistentFloat& TotalProfit = sc.PersistVars->f5;
    SCPersistentInt& WinningTrades = sc.PersistVars->i5;
    SCPersistentInt& LosingTrades = sc.PersistVars->i6;
    SCPersistentFloat& MaxDrawdown = sc.PersistVars->f6;
    SCPersistentFloat& PeakEquity = sc.PersistVars->f7;
    SCPersistentFloat& LastBuyPrice = sc.PersistVars->f8;
    SCPersistentFloat& LastSellPrice = sc.PersistVars->f9;
    SCPersistentInt& BuyVolumeTrigger = sc.PersistVars->i7;
    SCPersistentInt& SellVolumeTrigger = sc.PersistVars->i8;
    SCPersistentInt& OriginalPositionSize = sc.PersistVars->i9;
    SCPersistentInt& HalfClosed = sc.PersistVars->i10;

    if (sc.SetDefaults)
    {
        sc.GraphName = "Volume Spike Trading Bot";
        sc.AutoLoop = 1;
        sc.FreeDLL = 0;

        EnableTrading.Name = "Enable Trading";
        EnableTrading.SetYesNo(0);
        TargetMultiplier.Name = "Target Multiplier";
        TargetMultiplier.SetFloat(2.0f);
        TargetMultiplier.SetFloatLimits(0.1f, 10.0f);
        StopMultiplier.Name = "Stop Multiplier";
        StopMultiplier.SetFloat(1.0f);
        StopMultiplier.SetFloatLimits(0.1f, 10.0f);
        RiskPercentage.Name = "Risk Percentage per Trade";
        RiskPercentage.SetFloat(1.0f);
        RiskPercentage.SetFloatLimits(0.1f, 10.0f);
        MaxDailyLoss.Name = "Max Daily Loss Percentage";
        MaxDailyLoss.SetFloat(2.0f);
        MaxDailyLoss.SetFloatLimits(0.1f, 20.0f);
        TradingStartTime.Name = "Trading Start Time (HHMM)";
        TradingStartTime.SetTime(HMS_TIME(9, 30, 0));
        TradingEndTime.Name = "Trading End Time (HHMM)";
        TradingEndTime.SetTime(HMS_TIME(16, 0, 0));
        TickMoveThreshold.Name = "Tick Move Threshold";
        TickMoveThreshold.SetInt(3);
        TickMoveThreshold.SetIntLimits(1, 20);
        VolumeThreshold.Name = "Minimum Volume Threshold";
        VolumeThreshold.SetInt(100);
        VolumeThreshold.SetIntLimits(1, 10000);
        FirstTargetMultiplier.Name = "First Target Multiplier";
        FirstTargetMultiplier.SetFloat(1.0f);
        FirstTargetMultiplier.SetFloatLimits(0.1f, 10.0f);
        SecondTargetMultiplier.Name = "Second Target Multiplier";
        SecondTargetMultiplier.SetFloat(2.0f);
        SecondTargetMultiplier.SetFloatLimits(0.1f, 10.0f);
        MaxPositionSize.Name = "Max Position Size"; // ADDED
        MaxPositionSize.SetInt(10);                // ADDED
        MaxPositionSize.SetIntLimits(1, 100);      // ADDED

        BuySignal.Name = "Buy Signal";
        BuySignal.DrawStyle = DRAWSTYLE_ARROW_UP;
        BuySignal.PrimaryColor = RGB(0, 255, 0);
        BuySignal.LineWidth = 2;
        
        SellSignal.Name = "Sell Signal";
        SellSignal.DrawStyle = DRAWSTYLE_ARROW_DOWN;
        SellSignal.PrimaryColor = RGB(255, 0, 0);
        SellSignal.LineWidth = 2;
        
        VolumeTriggerLong.Name = "Volume Trigger Long";
        VolumeTriggerLong.DrawStyle = DRAWSTYLE_DASH;
        VolumeTriggerLong.PrimaryColor = RGB(0, 200, 200);
        VolumeTriggerLong.LineWidth = 1;
        
        VolumeTriggerShort.Name = "Volume Trigger Short";
        VolumeTriggerShort.DrawStyle = DRAWSTYLE_DASH;
        VolumeTriggerShort.PrimaryColor = RGB(200, 0, 200);
        VolumeTriggerShort.LineWidth = 1;

        TotalProfit = 0.0f;
        WinningTrades = 0;
        LosingTrades = 0;
        MaxDrawdown = 0.0f;
        PeakEquity = 0.0f;
        LastBuyPrice = 0.0f;
        LastSellPrice = 0.0f;
        BuyVolumeTrigger = 0;
        SellVolumeTrigger = 0;
        OriginalPositionSize = 0;
        HalfClosed = 0;

        return;
    }
    
    // Calculate ATR - Simplified and more efficient
    SCFloatArray ATR;
    sc.ATR(sc.BaseDataIn, ATR, 14, MOVAVGTYPE_SIMPLE);
    float AvgRange = ATR[sc.Index];
    if (AvgRange < 0.0001f) return;

    SCDateTime CurrentTime = sc.CurrentSystemDateTime;
    int CurrentDay = CurrentTime.GetDayOfYear();
    int CurrentMinutes = CurrentTime.GetHour() * 60 + CurrentTime.GetMinute();
    int StartMinutes = HM_TIME_TO_MINUTES(TradingStartTime.GetTime());
    int EndMinutes = HM_TIME_TO_MINUTES(TradingEndTime.GetTime());

    if (LastDayProcessed != CurrentDay)
    {
        DailyPnL = 0.0f;
        LastDayProcessed = CurrentDay;
    }

    float ClosingPrice = sc.Close[sc.Index];
    float TickSize = sc.TickSize;

    s_SCPositionData PositionData;
    sc.GetTradePosition(PositionData);
    float AccountBalance = sc.AccountBalance;

    float RiskPerTrade = AccountBalance * (RiskPercentage.GetFloat() / 100.0f);
    float StopDistance = AvgRange * StopMultiplier.GetFloat();
    // Cap the position size
    int PositionSize = max(1, static_cast<int>(RiskPerTrade / (StopDistance * sc.CurrencyValuePerTick)));
    PositionSize = min(PositionSize, MaxPositionSize.GetInt()); // ADDED: Limit position size

    float CurrentBidPrice = sc.Bid;
    float CurrentAskPrice = sc.Ask;
    int RecentBuyVolume = sc.GetRecentAskVolumeAtPrice(CurrentAskPrice);
    int RecentSellVolume = sc.GetRecentBidVolumeAtPrice(CurrentBidPrice);

    // Clear previous volume trigger markers
    VolumeTriggerLong[sc.Index] = 0;
    VolumeTriggerShort[sc.Index] = 0;

    char volMessage[256];
    sprintf_s(volMessage, "BidVol=%d at %.4f, AskVol=%d at %.4f", 
             RecentSellVolume, CurrentBidPrice, RecentBuyVolume, CurrentAskPrice);
    sc.AddMessageToLog(volMessage, 0);

    // Track volume triggers - more efficient detection
    if (RecentBuyVolume >= VolumeThreshold.GetInt())
    {
        LastBuyPrice = CurrentAskPrice;
        BuyVolumeTrigger = 1;
        VolumeTriggerLong[sc.Index] = ClosingPrice;
        sprintf_s(volMessage, "Buy Volume Triggered: %d at %.4f", RecentBuyVolume, LastBuyPrice);
        sc.AddMessageToLog(volMessage, 0);
    }
    
    if (RecentSellVolume >= VolumeThreshold.GetInt())
    {
        LastSellPrice = CurrentBidPrice;
        SellVolumeTrigger = 1;
        VolumeTriggerShort[sc.Index] = ClosingPrice;
        sprintf_s(volMessage, "Sell Volume Triggered: %d at %.4f", RecentSellVolume, LastSellPrice);
        sc.AddMessageToLog(volMessage, 0);
    }

    // Immediate entry checks - just need required price movement of 3 ticks
    bool BuyConditionMet = false;
    bool SellConditionMet = false;
    
    if (BuyVolumeTrigger)
    {
        float PriceMoveUp = ClosingPrice - LastBuyPrice;
        if (PriceMoveUp >= TickMoveThreshold.GetInt() * TickSize)
        {
            BuyConditionMet = true;
            sprintf_s(volMessage, "Buy Condition Met: %.4f moved %.4f from %.4f", 
                    ClosingPrice, PriceMoveUp, LastBuyPrice);
            sc.AddMessageToLog(volMessage, 0);
        }
        else if (PriceMoveUp <= -TickSize)  // Reset if price moves against by 1 tick
        {
            LastBuyPrice = 0.0f;
            BuyVolumeTrigger = 0;
            sc.AddMessageToLog("Buy Trigger Reset: Price Reversed", 0);
        }
    }

    if (SellVolumeTrigger)
    {
        float PriceMoveDown = LastSellPrice - ClosingPrice;
        if (PriceMoveDown >= TickMoveThreshold.GetInt() * TickSize)
        {
            SellConditionMet = true;
            sprintf_s(volMessage, "Sell Condition Met: %.4f moved %.4f from %.4f", 
                    ClosingPrice, PriceMoveDown, LastSellPrice);
            sc.AddMessageToLog(volMessage, 0);
        }
        else if (PriceMoveDown <= -TickSize)  // Reset if price moves against by 1 tick
        {
            LastSellPrice = 0.0f;
            SellVolumeTrigger = 0;
            sc.AddMessageToLog("Sell Trigger Reset: Price Reversed", 0);
        }
    }

   // Manage exits with targets
    if (TradeActive && PositionData.PositionQuantity != 0)
    {
        int HalfSize = OriginalPositionSize / 2;
        float EntryPrice = PositionData.AveragePrice;
		float FirstTargetPrice, SecondTargetPrice;

        if (PositionData.PositionQuantity > 0) // Long position
        {
			FirstTargetPrice = EntryPrice + (AvgRange * FirstTargetMultiplier.GetFloat());
			SecondTargetPrice = EntryPrice + (AvgRange * SecondTargetMultiplier.GetFloat());

            // Close half of the long position at first target
            if (!HalfClosed && ClosingPrice >= FirstTargetPrice)
            {
                s_SCNewOrder ExitOrder;
                ExitOrder.OrderQuantity = HalfSize;
                ExitOrder.OrderType = SCT_ORDERTYPE_MARKET;
                sc.SellExit(ExitOrder);
                HalfClosed = 1;
                float TradeProfit = (ClosingPrice - EntryPrice) * HalfSize * sc.CurrencyValuePerTick;
                TotalProfit += TradeProfit;
                DailyPnL += TradeProfit;
                PeakEquity = max(PeakEquity, TotalProfit);
                MaxDrawdown = max(MaxDrawdown, PeakEquity - TotalProfit);
                char message[256];
                sprintf_s(message, "Long Half Closed (First Target): Profit=%.2f, Price=%.4f", TradeProfit, ClosingPrice);
                sc.AddMessageToLog(message, 0);
            }
            // Close the remaining half at second target
            else if (HalfClosed && ClosingPrice >= SecondTargetPrice)
            {
                sc.FlattenAndCancelAllOrders();
                TradeActive = 0;
                HalfClosed = 0;
                OriginalPositionSize = 0;
                float TradeProfit = (ClosingPrice - EntryPrice) * HalfSize * sc.CurrencyValuePerTick;
                TotalProfit += TradeProfit;
                DailyPnL += TradeProfit;
                PeakEquity = max(PeakEquity, TotalProfit);
                MaxDrawdown = max(MaxDrawdown, PeakEquity - TotalProfit);
                if (TradeProfit > 0) WinningTrades++;
                else if (TradeProfit < 0) LosingTrades++;
                char message[256];
                sprintf_s(message, "Long Full Closed (Second Target): Profit=%.2f, Price=%.4f", TradeProfit, ClosingPrice);
                sc.AddMessageToLog(message, 0);
            }
        }
        else if (PositionData.PositionQuantity < 0) // Short position
        {
			FirstTargetPrice = EntryPrice - (AvgRange * FirstTargetMultiplier.GetFloat());
			SecondTargetPrice = EntryPrice - (AvgRange * SecondTargetMultiplier.GetFloat());
            // Close half of the short position at first target
            if (!HalfClosed && ClosingPrice <= FirstTargetPrice)
            {
                s_SCNewOrder ExitOrder;
                ExitOrder.OrderQuantity = HalfSize;
                ExitOrder.OrderType = SCT_ORDERTYPE_MARKET;
                sc.BuyExit(ExitOrder);
                HalfClosed = 1;
                float TradeProfit = (EntryPrice - ClosingPrice) * HalfSize * sc.CurrencyValuePerTick;
                TotalProfit += TradeProfit;
                DailyPnL += TradeProfit;
                PeakEquity = max(PeakEquity, TotalProfit);
                MaxDrawdown = max(MaxDrawdown, PeakEquity - TotalProfit);
                char message[256];
                sprintf_s(message, "Short Half Closed (First Target): Profit=%.2f, Price=%.4f", TradeProfit, ClosingPrice);
                sc.AddMessageToLog(message, 0);
            }
            // Close the remaining half at second target
            else if (HalfClosed && ClosingPrice <= SecondTargetPrice)
            {
                sc.FlattenAndCancelAllOrders();
                TradeActive = 0;
                HalfClosed = 0;
                OriginalPositionSize = 0;
                float TradeProfit = (EntryPrice - ClosingPrice) * HalfSize * sc.CurrencyValuePerTick;
                TotalProfit += TradeProfit;
                DailyPnL += TradeProfit;
                PeakEquity = max(PeakEquity, TotalProfit);
                MaxDrawdown = max(MaxDrawdown, PeakEquity - TotalProfit);
                if (TradeProfit > 0) WinningTrades++;
                else if (TradeProfit < 0) LosingTrades++;
                char message[256];
                sprintf_s(message, "Short Full Closed (Second Target): Profit=%.2f, Price=%.4f", TradeProfit, ClosingPrice);
                sc.AddMessageToLog(message, 0);
            }
        }
    }
    // Handle closure by stop loss from attached orders
    else if (TradeActive && PositionData.PositionQuantity == 0 && PositionData.LastFillOrderID != 0)
    {
        s_SCTradingOrderFillData LastFill;
        if (sc.GetOrderFillEntry(PositionData.LastFillOrderID, LastFill) != 0)
        {
            float TradeProfit = (LastFill.FillPrice - PositionData.AveragePrice) * 
                              PositionData.LastFillQuantity * sc.CurrencyValuePerTick * 
                              (PositionData.PositionQuantity > 0 ? 1 : -1);
            TotalProfit += TradeProfit;
            DailyPnL += TradeProfit;
            PeakEquity = max(PeakEquity, TotalProfit);
            MaxDrawdown = max(MaxDrawdown, PeakEquity - TotalProfit);
            if (TradeProfit > 0) WinningTrades++;
            else if (TradeProfit < 0) LosingTrades++;
            char message[256];
            sprintf_s(message, "Trade Closed (Stop/Other): Profit=%.2f, TotalProfit=%.2f", TradeProfit, TotalProfit);
            sc.AddMessageToLog(message, 0);
            TradeActive = 0;
            HalfClosed = 0;
            OriginalPositionSize = 0;
        }
    }

    // Check if we can enter a new trade
    if (!EnableTrading.GetYesNo() || 
        LastTradeBarIndex == sc.Index ||
        CurrentMinutes < StartMinutes ||
        CurrentMinutes >= EndMinutes ||
        (DailyPnL < 0 && fabs(DailyPnL) > (AccountBalance * (MaxDailyLoss.GetFloat() / 100.0f))) ||
        PositionData.PositionQuantity != 0)
    {
        return;
    }

    // Set up the new order with stops and targets
    s_SCNewOrder NewOrder;
    NewOrder.OrderQuantity = PositionSize;
    NewOrder.OrderType = SCT_ORDERTYPE_MARKET;
    NewOrder.Target1Offset = 0;  // We're managing targets manually
    NewOrder.AttachedOrderTarget1Type = SCT_ORDERTYPE_LIMIT;  // Can still be useful for safety
	// Calculate StopPrice based on entry side.
	float StopPrice = (BuyConditionMet) ? ClosingPrice - StopDistance : ClosingPrice + StopDistance;
    NewOrder.AttachedOrderStopAllType = SCT_ORDERTYPE_STOP;
	NewOrder.StopAllOffset = fabs(ClosingPrice - StopPrice);

    int Result = 0;
    char message[256];
    
    // Buy entry based on volume and price move
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
            OriginalPositionSize = PositionSize;
            HalfClosed = 0;
            sprintf_s(message, "Buy Order #%d (Volume-Based): Size=%d, Price=%.4f, Range=%.4f", 
                     TradeCount, PositionSize, ClosingPrice, AvgRange);
            sc.AddMessageToLog(message, 0);
        }
        else if (Result < 0)
        {
            sprintf_s(message, "Buy Order Failed: Error %d", Result);
            sc.AddMessageToLog(message, 1);
        }
    }
    // Sell entry based on volume and price move
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
            OriginalPositionSize = PositionSize;
            HalfClosed = 0;
            sprintf_s(message, "Sell Order #%d (Volume-Based): Size=%d, Price=%.4f, Range=%.4f", 
                     TradeCount, PositionSize, ClosingPrice, AvgRange);
            sc.AddMessageToLog(message, 0);
        }
        else if (Result < 0)
        {
            sprintf_s(message, "Sell Order Failed: Error %d", Result);
            sc.AddMessageToLog(message, 1);
        }
    }

    // Display performance stats at the end of the backtest
    if (sc.Index == sc.ArraySize - 1)
    {
        float WinRate = TradeCount > 0 ? (float)WinningTrades / (WinningTrades + LosingTrades) * 100.0f : 0.0f;
        sprintf_s(message, "Backtest Results: TotalProfit=%.2f, Trades=%d, WinRate=%.1f%%, MaxDD=%.2f", 
                 TotalProfit, TradeCount, WinRate, MaxDrawdown);
        sc.AddMessageToLog(message, 0);
    }
}
