#include "sierrachart.h"
#include <unordered_map>
SCDLLName("Recent Bid/Ask By Footprint")

/*
    Written and Developed by:
        Frozen Tundra
*/

// ex: 10 trades come in at .439
struct VAT {
    int DateTimeInMs;
    float Price;
    uint32_t Volume;
    int NumTrades;
};

SCSFExport scsf_RecentBidAskVolByFootprint(SCStudyInterfaceRef sc)
{
    // logging object
    SCString msg;

    // inputs
    int InputIndex = 0;
    SCInputRef i_Enabled = sc.Input[InputIndex++];
    SCInputRef i_VerticalOffset = sc.Input[InputIndex++];
    SCInputRef i_HorizontalOffset = sc.Input[InputIndex++];
    SCInputRef i_FontSize = sc.Input[InputIndex++];
    SCInputRef i_FontColor = sc.Input[InputIndex++];
    SCInputRef i_BgColor = sc.Input[InputIndex++];

    // Set configuration variables
    if (sc.SetDefaults)
    {
        sc.GraphName = "Recent Bid/Ask By Footprint";
        sc.GraphShortName = "RBABF";
        sc.GraphRegion = 0;

        i_Enabled.Name = "Enabled?";
        i_Enabled.SetYesNo(1);

        i_VerticalOffset.Name = "Vertical Offset in px";
        i_VerticalOffset.SetInt(20);

        i_HorizontalOffset.Name = "Horizontal Offset in px";
        i_HorizontalOffset.SetInt(0);

        i_FontSize.Name = "Font Size";
        i_FontSize.SetInt(12);

        // so this can be used on candlestick charts or non-footprint charts
        sc.MaintainVolumeAtPriceData = 1;

        return;
    }

    // number of lots/total volume traded 
    // - within certain amount of time
    // - within certain amount of ticks

    std::unordered_map<int, VAT> vats;



    // Get the Time and Sales
    c_SCTimeAndSalesArray TimeSales;
    sc.GetTimeAndSales(TimeSales);

    if (TimeSales.Size() == 0)
        return;  // No Time and Sales data available for the symbol

    int NUM_TIME_AND_SALES_RECORDS_TO_EXAMINE = 1000;

    int x_Time;
    float x_Price;
    int x_Volume;
    int x_NumTrades;


    // Loop through the Time and Sales
    int OutputArrayIndex = sc.ArraySize;
    //for (int TSIndex = TimeSales.Size() - 1; TSIndex >= 0; --TSIndex)
    for (int TSIndex = TimeSales.Size() - 1; TSIndex >= TimeSales.Size() - NUM_TIME_AND_SALES_RECORDS_TO_EXAMINE; --TSIndex)
    {
        //Adjust timestamps to Sierra Chart TimeZone
        SCDateTimeMS DateTime = TimeSales[TSIndex].DateTime;
        DateTime += sc.TimeScaleAdjustment;

        // trade execution details
        int16_t Type = TimeSales[TSIndex].Type;

        // only look at trades, not updates
        if (Type != SC_TS_BID && Type != SC_TS_ASK) continue;

        float Price = TimeSales[TSIndex].Price;
        uint32_t Volume = TimeSales[TSIndex].Volume;
        //This will always be a value >= 1.  It is unlikely to wrap around, but it could.  It will never be 0.
        uint32_t Sequence = TimeSales[TSIndex].Sequence;

        // Level 1 info:
        // inside market price
        float Bid = TimeSales[TSIndex].Bid;
        float Ask = TimeSales[TSIndex].Ask;
        // inside market size
        uint32_t BidSize = TimeSales[TSIndex].BidSize;
        uint32_t AskSize = TimeSales[TSIndex].AskSize;
        uint32_t TotalBidDepth = TimeSales[TSIndex].TotalBidDepth;
        uint32_t TotalAskDepth = TimeSales[TSIndex].TotalAskDepth;

        // normalize our datetime into an integer of time in ms
        // this is our VAT KEY!!!
        int TimeInMs = DateTime.GetTimeInMilliseconds();

        // can we find an existing bucket of trades for this time in ms?
        auto itr = vats.find(TimeInMs);
        if (itr != vats.end()) {
            // we found an existing bucket! add our volume to it here

            // add our latest volume to this bucket
            vats[itr->first].Volume += Volume;
            vats[itr->first].NumTrades++;

            // spit out to debug log if this millisecond traded more than this number of contracts
            if (vats[itr->first].Volume >= 30) {
                //msg.Format("%d -> P=%f, V=%d, NumTrades=%d", TimeInMs, vats[itr->first].Price, vats[itr->first].Volume, vats[itr->first].NumTrades);
                //sc.AddMessageToLog(msg, 1);

                // set these for drawing later
                x_Time = TimeInMs;
                x_Price = vats[itr->first].Price;
                x_Volume = vats[itr->first].Volume;
                x_NumTrades = vats[itr->first].NumTrades;
            }
        }
        else {
            // we didnt find an existing bucket, add one here
            VAT tmp = {
                TimeInMs,
                Price,
                Volume,
                1 // num trades
            };

            // insert this VAT into our map
            vats.insert(std::make_pair(TimeInMs, tmp));
        }

        //int16_t UnbundledTradeIndicator = 0;

        //msg.Format("[%d] Time=%s, Type=%d, P=%f, V=%d, Seq=%d", TSIndex, sc.DateTimeToString(DateTime, FLAG_DT_COMPLETE_DATETIME_MS).GetChars(), Type, Price, Volume, Sequence);
        //sc.AddMessageToLog(msg, 1);
    }




    s_UseTool Tool;
    Tool.ChartNumber = sc.ChartNumber;
    Tool.LineNumber = 8122022;
    //Tool.DrawingType = DRAWING_STATIONARY_TEXT;
    Tool.DrawingType = DRAWING_TEXT;
    //Tool.UseRelativeVerticalValues = 1;
    //Tool.BeginValue = 15;
    //Tool.BeginDateTime = 2;
    Tool.BeginValue = x_Price;
    Tool.BeginIndex = sc.Index;
    Tool.AddMethod = UTAM_ADD_OR_ADJUST;
    Tool.Region = sc.GraphRegion;
    Tool.FontSize = i_FontSize.GetInt();
    Tool.FontBold = true;
    Tool.Text.Format("\t\t[%d] %d", x_NumTrades, x_Volume);
    Tool.Color = COLOR_YELLOW;
    sc.UseTool(Tool);








    /*
    // referencing 
    // https://www.sierrachart.com/index.php?page=doc/ACSIL_Members_Variables_And_Arrays.html#scVolumeAtPriceForBars

    // declare our VAP container that we'll use to examine Volume At Price
    const s_VolumeAtPriceV2 *p_VAP=NULL;

    // get the number of price levels for a given bar index
    int VAPSizeAtBarIndex = sc.VolumeAtPriceForBars->GetSizeAtBarIndex(sc.Index);

    // loop through all price levels at this bar index
    for (int VAPIndex = 0; VAPIndex < VAPSizeAtBarIndex; VAPIndex++)
    {
        // store VAP for this price level (at this bar index) so it can be examined
        if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(sc.Index, VAPIndex, &p_VAP))
            break;

        int PriceInTicks = p_VAP->PriceInTicks;
        float Price = sc.TicksToPriceValue(PriceInTicks);
        unsigned int Volume = p_VAP->Volume;
        unsigned int BidVolume = p_VAP->BidVolume;
        unsigned int AskVolume = p_VAP->AskVolume;
        unsigned int NumberOfTrades = p_VAP->NumberOfTrades;

        // sanity check that our data is good
        msg.Format("[%d][%d] NumLvls=%d, Price=%f, PriceInTicks=%d, V=%d, B=%d, A=%d, NumTrades=%d", sc.Index, VAPIndex, VAPSizeAtBarIndex, Price, PriceInTicks, Volume, BidVolume, AskVolume, NumberOfTrades);
        sc.AddMessageToLog(msg, 1);
    }
    */

}


