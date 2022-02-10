// TODO for wireless network simulator:
// [done] Copy-paste lag.c to rateLimit.c
// Change logic in rateLimitProcess as described below.
// Hook up ZeroMQ controls for this module (in main.c).
//  Question: What feedback do we want clumsy to provide the simulation client?
//  Ideas:
//    output bandwidth
//    current buffer population
//    current lag (i.e. average duration of a packet inside the buffer)
// Provide UI controls for this module.

// rate-limit packets
#include "iup.h"
#include "common.h"
#include "rateStats.h"

#define NAME "rateLimit"
#define LAG_MIN "0"
#define LAG_MAX "15000"
#define VARIATION_MIN "0"
#define VARIATION_MAX "15000"
#define KEEP_AT_MOST 2000
// send FLUSH_WHEN_FULL packets when buffer is full
#define FLUSH_WHEN_FULL 800
#define LAG_DEFAULT 0
#define VARIATION_DEFAULT 0

// Data rate is in megabits per second (i.e. 1024 kilobits per second)
#define DATA_RATE_CAP_MBPS_MIN "0"
#define DATA_RATE_CAP_MBPS_MAX "65535"
#define DATA_RATE_CAP_MBPS_DEFAULT 1024

// don't need a chance
static Ihandle* inboundCheckbox, * outboundCheckbox;
Ihandle* rlTimeInput, * rlVariationInput;
Ihandle* rlDataRateCapMbps;

static volatile short rateLimitEnabled = 0,
    rateLimitInbound = 1,
    rateLimitOutbound = 1,
    rlLagTime = LAG_DEFAULT, // default for 50ms
    rlLagVariation = VARIATION_DEFAULT; // default for no variation
static volatile short dataRateCapMbps = DATA_RATE_CAP_MBPS_DEFAULT;

static PacketNode rateLimitHeadNode = {0}, rateLimitTailNode = {0};
static PacketNode *bufHead = &rateLimitHeadNode, *bufTail = &rateLimitTailNode;
static int bufSize = 0;

static DWORD prevTimeMs = 0;
float rateLimit_dataRateBytesPerSec = 0.0f;
float rateLimit_queueDelayMs = 0.0f;

static CRateStats* rateStats = NULL;

///////////////////////////////////////////////////////////////////////////////////////////
//#define RB_CAP 1000
//typedef struct PacketInfoS
//{
//    int _sizeBytes;
//    DWORD _timestampMs;
//} PacketInfo;
//static PacketInfo rbItems[RB_CAP];
//static int rbHead = 0;
//static int rbPopulation = 0;
//static PacketInfo * RbItem(size_t i)
//{
//    return &rbItems[(rbHead + i) % RB_CAP];
//}
//static void RbAppend(int sizeBytes, DWORD timestampMs)
//{
//    const size_t iNewTail = rbPopulation;
//    RbItem(iNewTail)->_sizeBytes = sizeBytes;
//    RbItem(iNewTail)->_timestampMs = timestampMs;
//    if (rbPopulation < RB_CAP)
//    {
//        ++rbPopulation;
//    }
//    else
//    {
//        rbHead = (rbHead+1) % RB_CAP;
//    }
//}
//static int RbSizeBytesSum()
//{
//    const DWORD timestampOldest = rbItems[rbHead]._timestampMs;
//    int sizeBytesSum = 0;
//    for (size_t i = 0; i < rbPopulation; ++i)
//    {
//        sizeBytesSum += RbItem(i)->_sizeBytes;
//        assert(RbItem(i)->_timestampMs >= timestampOldest);
//    }
//    return sizeBytesSum;
//}
//static float RbDataRateBps(DWORD timeNowMs)
//{
//    const int sizeBytesSum = RbSizeBytesSum();
//    const DWORD timestampOldest = rbItems[rbHead]._timestampMs;
//    const DWORD durationMs = timeNowMs - timestampOldest;
//    const float dataRateBps = 1000.0f * (float)sizeBytesSum / (float)durationMs;
//    return dataRateBps;
//}
///////////////////////////////////////////////////////////////////////////////////////////

static INLINE_FUNCTION short isBufEmpty()
{
    short ret = bufHead->next == bufTail;
    if (ret) assert(bufSize == 0);
    return ret;
}

static Ihandle *rateLimitSetupUI()
{
    Ihandle *rateLimitControlsBox = IupHbox(
        inboundCheckbox = IupToggle("Inbound", NULL),
        outboundCheckbox = IupToggle("Outbound", NULL),
        IupLabel("Delay(ms):"), rlTimeInput = IupText(NULL),
        IupLabel("Variation(ms):"), rlVariationInput = IupText(NULL),
        IupLabel("DataRate(Mbps):"), rlDataRateCapMbps = IupText(NULL),
        NULL
        );

    IupSetAttribute(rlTimeInput, "VISIBLECOLUMNS", "4");
    IupSetAttribute(rlTimeInput, "VALUE", STR(LAG_DEFAULT));
    IupSetCallback(rlTimeInput, "VALUECHANGED_CB", uiSyncInteger);
    IupSetAttribute(rlTimeInput, SYNCED_VALUE, (char*)&rlLagTime);
    IupSetAttribute(rlTimeInput, INTEGER_MAX, LAG_MAX);
    IupSetAttribute(rlTimeInput, INTEGER_MIN, LAG_MIN);

    IupSetAttribute(rlVariationInput, "VISIBLECOLUMNS", "4");
    IupSetAttribute(rlVariationInput, "VALUE", STR(VARIATION_DEFAULT));
    IupSetCallback(rlVariationInput, "VALUECHANGED_CB", uiSyncInteger);
    IupSetAttribute(rlVariationInput, SYNCED_VALUE, (char*)&rlLagVariation);
    IupSetAttribute(rlVariationInput, INTEGER_MAX, VARIATION_MAX);
    IupSetAttribute(rlVariationInput, INTEGER_MIN, VARIATION_MIN);

    IupSetAttribute(rlDataRateCapMbps, "VISIBLECOLUMNS", "4");
    IupSetAttribute(rlDataRateCapMbps, "VALUE", STR(DATA_RATE_CAP_MBPS_DEFAULT));
    IupSetCallback (rlDataRateCapMbps, "VALUECHANGED_CB", uiSyncInteger);
    IupSetAttribute(rlDataRateCapMbps, SYNCED_VALUE, (char*)&dataRateCapMbps);
    IupSetAttribute(rlDataRateCapMbps, INTEGER_MAX, DATA_RATE_CAP_MBPS_MAX);
    IupSetAttribute(rlDataRateCapMbps, INTEGER_MIN, DATA_RATE_CAP_MBPS_MIN);

    IupSetCallback(inboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(inboundCheckbox, SYNCED_VALUE, (char*)&rateLimitInbound);

    IupSetCallback(outboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(outboundCheckbox, SYNCED_VALUE, (char*)&rateLimitOutbound);

    // enable by default to avoid confusing
    IupSetAttribute(inboundCheckbox, "VALUE", "ON");
    IupSetAttribute(outboundCheckbox, "VALUE", "ON");

    if (parameterized)
    {
        setFromParameter(inboundCheckbox, "VALUE", NAME"-inbound");
        setFromParameter(outboundCheckbox, "VALUE", NAME"-outbound");
        setFromParameter(rlTimeInput, "VALUE", NAME"-time");
        setFromParameter(rlVariationInput, "VALUE", NAME"-variation");
    }

    return rateLimitControlsBox;
}

static void rateLimitStartUp()
{
    if (bufHead->next == NULL && bufTail->next == NULL)
    {
        bufHead->next = bufTail;
        bufTail->prev = bufHead;
        bufSize = 0;
    } else {
        assert(isBufEmpty());
    }
    startTimePeriod();

    if (rateStats) crate_stats_delete(rateStats);
    rateStats = crate_stats_new(500, 1000);

    prevTimeMs = timeGetTime();
    rateLimit_dataRateBytesPerSec = 0.0f;
    rateLimit_queueDelayMs = 0.0f;

    //rbPopulation = 0;
    //rbHead = 0;
}

static void rateLimitCloseDown(PacketNode *head, PacketNode *tail)
{
    PacketNode *oldLast = tail->prev;
    UNREFERENCED_PARAMETER(head);
    // flush all buffered packets
    LOG("Closing down lag, flushing %d packets", bufSize);
    while(!isBufEmpty())
    {
        insertAfter(popNode(bufTail->prev), oldLast);
        --bufSize;
    }
    endTimePeriod();

    if (rateStats) crate_stats_delete(rateStats);
    rateStats = NULL;

    //rbPopulation = 0;
    //rbHead = 0;
}

// When an inbound packet arrives:
//   If the buffer has room, put it into the buffer. (as in rateLimitProcess below. See "insertAfter(popNode(pac), bufHead)->sendTimestamp = currentTime + lag;"
//   If the buffer does not have room, drop a packet. (Dropping incoming is the default.) (See also freeNode(popNode(pac)) in drop.c)
//   (idea) If packet has been in buffer longer than maxDuration the drop it. (Note, that implies maxDuration is a configuration parameter.)
//   Note: policy could be to drop most recent or least recent packet. Traditionally, newest is dropped. But remember conversation about alternative.
// Every update:
//   If buffer has data, and outgoing data rate is under configured available bandwidth (and sending packet would not exceed that rate):
//     (See crate_stats_calculate)
//     Send a packet.
//   Update data rate statistics. (See how bandwidth.c does this -- by calling crate_stats_update.)
static short rateLimitProcess(PacketNode* head, PacketNode* tail)
{
    const DWORD currentTimeMs = timeGetTime();

    if (rateStats == NULL) {
        return 0;
    }

    const float queueDelayUpdateGain = 0.0625f;

    //DWORD updateDurationMs = currentTimeMs - prevTimeMs;
    //int bytesSentThisUpdate = 0;
    PacketNode* pac = tail->prev;
    // pick up all packets and fill in the current time
    while (bufSize < KEEP_AT_MOST && pac != head)
    {
        if (checkDirection(pac->addr.Outbound, rateLimitInbound, rateLimitOutbound))
        {
            // lag varies from rlLagTime - rlLagVariation <= lag <= rlLagTime + rlLagVariation
            short lag = rlLagTime + (rand() % (1 + 2 * rlLagVariation)) - rlLagVariation;
            insertAfter(popNode(pac), bufHead)->sendTimestamp = currentTimeMs + lag;
            ++bufSize;
            pac = tail->prev;
        }
        else {
            pac = pac->prev;
        }
    }

    // try sending overdue packets from buffer tail
    while (!isBufEmpty())
    {
        const int dataRateBpsMean = crate_stats_calculate(rateStats, currentTimeMs);
        pac = bufTail->prev;
        const int waitedLongEnough = 1; // currentTimeMs >= pac->sendTimestamp;
        const int dataRateUnderCap = dataRateBpsMean < (dataRateCapMbps * 131072); // 1024*1024/8=131072
        if (waitedLongEnough && dataRateUnderCap)
        {
            PacketNode* node = insertAfter(popNode(bufTail->prev), head); // sending queue is already empty by now
            {
                const DWORD enqueueDurationMs = currentTimeMs - node->sendTimestamp;
                const float delayDeltaMs = (float)enqueueDurationMs - rateLimit_queueDelayMs;
                rateLimit_queueDelayMs += queueDelayUpdateGain * delayDeltaMs;
            }
            //bytesSentThisUpdate += node->packetLen;
            crate_stats_update(rateStats, node->packetLen, currentTimeMs);
            //RbAppend(node->packetLen, currentTimeMs);
            --bufSize;
            //LOG("Sent lagged packets. rate=%i KBps", (int)(dataRateBpsMean / 1024.0f));
        }
        else {
            //LOG("Sent some lagged packets, still have %d in buf. rate=%i KBps", bufSize, (int)(dataRateBpsMean / 1024.0f));
            break;
        }
    }

    // if buffer is full just flush things out
    if (bufSize >= KEEP_AT_MOST)
    {
        int flushCnt = FLUSH_WHEN_FULL;
        while (flushCnt-- > 0)
        {
            PacketNode* node = insertAfter(popNode(bufTail->prev), head);
            {
                const DWORD enqueueDurationMs = currentTimeMs - node->sendTimestamp;
                const float delayDeltaMs = (float)enqueueDurationMs - rateLimit_queueDelayMs;
                rateLimit_queueDelayMs += queueDelayUpdateGain * delayDeltaMs;
            }
            //bytesSentThisUpdate += node->packetLen;
            --bufSize;
        }
    }

    //const float dataRateBpsThisUpdate = 1000.0f * (float)bytesSentThisUpdate / (updateDurationMs>0?(float)updateDurationMs:0.1f );
    //const float dataRateBpsDelta = dataRateBpsThisUpdate - rateLimit_dataRateBytesPerSec;
    //const float gain = 0.0625f;
    //rateLimit_dataRateBytesPerSec += gain * dataRateBpsDelta;
    //prevTimeMs = currentTimeMs;

    const int dataRateBpsMean = crate_stats_calculate(rateStats, currentTimeMs);
    rateLimit_dataRateBytesPerSec = (float)dataRateBpsMean;

    //rateLimit_dataRateBytesPerSec = RbDataRateBps(currentTimeMs);

    return bufSize > 0;
}

Module rateLimitModule =
{
    "RateLimit",
    NAME,
    (short*)&rateLimitEnabled,
    rateLimitSetupUI,
    rateLimitStartUp,
    rateLimitCloseDown,
    rateLimitProcess,
    // runtime fields
    0, 0, NULL
};