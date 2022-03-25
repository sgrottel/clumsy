#include "tracing.h"
#include "common.h"

#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winmeta.h>
#include <TraceLoggingProvider.h>

TRACELOGGING_DECLARE_PROVIDER(g_traceloggingProvider);

TRACELOGGING_DEFINE_PROVIDER(g_traceloggingProvider,
    "clumsy",
    (0x60e1d964, 0xd3dc, 0x4acf, 0xb0, 0x72, 0x82, 0x25, 0xd5, 0x8c, 0xb0, 0xb2));

void tracingSetup() {
    TraceLoggingRegister(g_traceloggingProvider);
}

void tracingTeardown() {
    TraceLoggingUnregister(g_traceloggingProvider);
}

#define WRAPPED_CALLBACK "__ETL_WRAPPED_CALLBACK"
#define CHAINED_CALLBACK "__ETL_CHAINED_CALLBACK"

static int wrappingCallback(Ihandle* ih) {
    Icallback ocb = IupGetCallback(ih, WRAPPED_CALLBACK);
    int rv = ocb(ih);
    IupGetCallback(ih, CHAINED_CALLBACK)(ih);
    return rv;
}

void IupInstallCallbackChain(
    Ihandle* ih,
    chainedTraceCallback cbChained
) {
    const char* slot = "VALUECHANGED_CB";
    Icallback cb = IupGetCallback(ih, slot);
    if (!cb) {
        slot = "ACTION";
        cb = IupGetCallback(ih, slot);
        if (!cb) {
            return;
        }
    }

    if (cb == &wrappingCallback) {
        LOG("`IupInstallCallbackChain` can only be called one time per object.");
        return;
    }

    IupSetCallback(ih, WRAPPED_CALLBACK, cb);
    IupSetCallback(ih, CHAINED_CALLBACK, cbChained);
    IupSetCallback(ih, slot, &wrappingCallback);
}

void tracingWriteEnabledEvent(
    BOOL running,
    const Module * const * mods,
    unsigned int modsCnt
) {
#define _MOD_STRBUF_LEN 1024
    char modStr[_MOD_STRBUF_LEN + 1];
    unsigned int i;
    size_t modStrPos;

    modStrPos = 0;
    for (i = 0; i < modsCnt; ++i) {
        size_t nameLen = strlen(mods[i]->shortName);
        if (modStrPos + nameLen + 4 > _MOD_STRBUF_LEN) break;
        memcpy(modStr + modStrPos, mods[i]->shortName, nameLen);
        modStrPos += nameLen;
        memcpy(modStr + modStrPos, " x, ", 4);
        modStr[modStrPos + 1] = (*mods[i]->enabledFlag) ? '1' : '0';
        modStrPos += 4;
    }
    if (modStrPos > 4) modStrPos -= 2; // take back last comma
    modStr[modStrPos] = 0;

    TraceLoggingWrite(g_traceloggingProvider,
        "ModuleEnabled",
        TraceLoggingDescription("Clumsy was started/stopped or a specific module was enabled/disabled"),
        TraceLoggingBool(running, "Running", "Clumsy is active"),
        TraceLoggingString(modStr, "Modules", "All modules and there enabled/disabled state")
    );
}

void tracingWriteEventLagSettings(
    short inbound,
    short outbound,
    short time,
    short variation
) {
    TraceLoggingWrite(g_traceloggingProvider,
        "LagSettings",
        TraceLoggingDescription("Settings of Lag Module changed"),
        TraceLoggingInt16(inbound, "Inbound", "Add lag to inbound packets"),
        TraceLoggingInt16(outbound, "Outbound", "Add lag to outbound packets"),
        TraceLoggingInt16(time, "Time", "Lag time to add in milliseconds"),
        TraceLoggingInt16(variation, "Variation", "Lag time variation in milliseconds")
    );
}

void tracingWriteEventRateLimitSettings(
    short inbound,
    short outbound,
    short delay,
    short variation,
    short dataRate
) {
    TraceLoggingWrite(g_traceloggingProvider,
        "RateLimitSettings",
        TraceLoggingDescription("Settings of RateLimit Module changed"),
        TraceLoggingInt16(dataRate, "DataRateCaps", "The maximum data rate (Mbits/s)."),
        TraceLoggingInt16(inbound, "Inbound", "Add rate limit to inbound packets"),
        TraceLoggingInt16(outbound, "Outbound", "Add rate limit to outbound packets"),
        TraceLoggingInt16(delay, "Time", "Lag time to add in milliseconds"),
        TraceLoggingInt16(variation, "Variation", "Lag time variation in milliseconds")
    );
}
