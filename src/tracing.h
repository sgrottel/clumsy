#pragma once
#include "common.h"

/* Register trace logging provider */
void tracingSetup();

/* Unregister trace logging provider */
void tracingTeardown();

/* Callback type. Currently identical to Iup Callbacks, but might change. */
typedef int (*chainedTraceCallback)(Ihandle* ih);

/* Changes the registered callback of `VALUECHANGED_CB` or `ACTION` on the object referenced by `ih` to:
 *  - call another (hidden) callback function, which then
 *  - 1) calls the previously registered callback function, and then
 *  - 2) calls the specified cbChained
 *    - The return value of cbChained will be ignored. Please, just return IUP_DEFAULT anyway.
 *  - The return value of the previously registered callback will be returned to the system.
 */
void IupInstallCallbackChain(Ihandle* ih, chainedTraceCallback cbChained);

/* Writes a trace event for change of `enabled` states
 * @param running Is the central running state of clumsy itself
 * @param mods Iterates over `mods` and reports their `enabledFlag` state in one concatenated string value.
 * @param modsCnt number of entries in `mods`
 */
void tracingWriteEnabledEvent(
    BOOL running,
    const Module * const * mods,
    unsigned int modsCnt
);

void tracingWriteEventLagSettings(
    short inbound,
    short outbound,
    short time,
    short variation
);

void tracingWriteEventRateLimitSettings(
    short inbound,
    short outbound,
    short delay,
    short variation,
    short dataRate
);
