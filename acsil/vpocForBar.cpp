#include "sierrachart.h"
#include <string>
SCDLLName("VPOC for bar")

/*============================================================================

----------------------------------------------------------------------------*/
SCSFExport scsf_VolumePointOfControlForBars(SCStudyInterfaceRef sc)
{
    SCSubgraphRef Subgraph_VPOC = sc.Subgraph[0];
    SCInputRef Input_NumberOfBarsToCalculate = sc.Input[0];

    if (sc.SetDefaults)
    {
        // Set the configuration and defaults

        sc.GraphName = "Volume Point of Control for Bars";

        sc.AutoLoop = 1;
        sc.MaintainVolumeAtPriceData = true;

        sc.GraphRegion = 0;

        Subgraph_VPOC.Name = "VPOC";
        Subgraph_VPOC.DrawStyle = DRAWSTYLE_DASH;
        Subgraph_VPOC.LineWidth = 2;
        Subgraph_VPOC.PrimaryColor = RGB(255, 128, 0);

        Input_NumberOfBarsToCalculate.Name = "Number of Bars To Calculate";
        Input_NumberOfBarsToCalculate.SetIntLimits(1, MAX_STUDY_LENGTH);
        Input_NumberOfBarsToCalculate.SetInt(4);

        return;
    }

    sc.DataStartIndex = sc.ArraySize - Input_NumberOfBarsToCalculate.GetInt();
    if (sc.DataStartIndex < 0)
        sc.DataStartIndex = 0;

    if (sc.Index < sc.DataStartIndex)
        return;

    // Do data processing
    for (int BarIndex = sc.DataStartIndex; BarIndex < sc.ArraySize; BarIndex++)
    {
        s_VolumeAtPriceV2 VolumeAtPrice;
        sc.GetPointOfControlPriceVolumeForBar(BarIndex, VolumeAtPrice);

        if (VolumeAtPrice.PriceInTicks != 0)
            Subgraph_VPOC.Data[BarIndex] = sc.TicksToPriceValue(VolumeAtPrice.PriceInTicks);
    }
}