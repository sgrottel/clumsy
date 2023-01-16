#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <Windows.h>
#include "iup.h"
#include "common.h"
#include "tracing.h"



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ZeroMQ glue to control clumsy from another process
#include <errno.h>
#include "zmq.h"
// To build the test client, set both ZMQ_CLIENT to 1 and ZMQ_NO_FILTER to 1.
#define ZMQ_CLIENT 0
// For development of ZMQ-specific features, set ZMQ_NO_FILTER to 1 for both client and server. This disables clumsy filters and WinDivert operations.
#define ZMQ_NO_FILTER 0

void * zmqContext = 0;
void * zmqSocket = 0;

static void zmqCleanup(void)
{   // Clean up ZeroMQ socket and context
    LOG("zmqCleanup:\n");
    zmq_close(zmqSocket);
    zmq_ctx_destroy(zmqContext);
}

static int zmqConnect(void)
{   // Create a connection to a ZeroMQ service. (For testing only.)
    LOG("zmqConnect: ");
    // Create socket to talk to server
    zmqSocket = zmq_socket(zmqContext, ZMQ_REQ);
    int rc = zmq_connect(zmqSocket, "tcp://localhost:5555");
    LOG("zmq_connect=%i\n", rc);
    return rc;
}

static int zmqListen(void)
{   // Listen on a socket for an incoming connection from a ZeroMQ client.
    LOG("zmqListen: ");
    // Create socket to talk to clients
    zmqSocket = zmq_socket(zmqContext, ZMQ_REP);
    int rc = zmq_bind(zmqSocket, "tcp://*:5555");
    LOG("zmq_bind=%i\n", rc);
    return rc;
}

static void zmqSetup(void)
{   // Create a ZeroMQ context and socket.
    LOG("zmqSetup:\n");
    zmqContext = zmq_ctx_new();
#if ZMQ_CLIENT
    int rc = zmqConnect();
#else
    int rc = zmqListen();
#endif
    if (rc != 0)
    {   // zmq_connect or zmq_bind failed so clean up ZMQ socket and context
        zmqCleanup();
    }
    assert(rc == 0);
}

static int zmqClientUpdate(Ihandle* ih)
{   // Send messages as a ZeroMQ client (for testing only).
    UNREFERENCED_PARAMETER(ih);
    {
        static int counter = 0; ++counter;
        //LOG("zmqClientUpdate: %i\n", counter);
        char sendMsg[256] = "";
        static int roundRobin = 0; roundRobin = (roundRobin + 1) % /* number of cases below*/ 4;
        switch (roundRobin)
        {
        case 0: snprintf(sendMsg, sizeof(sendMsg), "LagDelayMs %i", counter % 500); break;
        case 1: snprintf(sendMsg, sizeof(sendMsg), "DropChancePct %i", counter % 100); break;
        case 2: snprintf(sendMsg, sizeof(sendMsg), "BandwidthLimitKBps %i", counter % 10000); break;
        case 4: snprintf(sendMsg, sizeof(sendMsg), "RateLimitMbps %i", counter % 10000); break;
        default: snprintf(sendMsg, sizeof(sendMsg), "THIS_SHOULD_NEVER_HAPPEN %i", counter); break;
        }
        int numBytesSent = zmq_send(zmqSocket, sendMsg, strlen(sendMsg), 0);
        char statusString[256];
        if (numBytesSent >= 0)
        {
            char receivedMessage[257];
            int numBytesRcvd = zmq_recv(zmqSocket, receivedMessage, sizeof(receivedMessage) - 1, 0);
            receivedMessage[numBytesRcvd] = '\0';
            //LOG("zmqClientUpdate: (tick %i) sent=%i rcvd='%s'\n", counter, numBytesSent, rcvdMessage);
            sprintf(statusString, "zmqClientUpdate: (tick %i) sendMsg='%s' numBytesSent=%i rcvdMsg='%s'", counter, sendMsg, numBytesSent, receivedMessage);
        }
        else if(EFSM == errno)
        {
            sprintf(statusString, "zmqClientUpdate: (tick %i) numBytesSent=%i errno=EFSM", counter, numBytesSent);
        }
        else
        {
            sprintf(statusString, "zmqClientUpdate: (tick %i) numBytesSent=%i errno=%i", counter, numBytesSent, errno);
        }
        showStatus(statusString);
    }
    return IUP_DEFAULT;
}

extern Ihandle* timeInput, * variationInput;
extern Ihandle* chanceInput;
extern Ihandle* bandwidthInput;
extern Ihandle* rlDataRateCapMbps;
extern float rateLimit_dataRateBytesPerSec;
extern float rateLimit_queueDelayMs;

static int zmqServerUpdate(Ihandle* ih)
{   // Handle incoming ZeroMQ messages
    UNREFERENCED_PARAMETER(ih);
    static int counter = 0; ++counter;
    //LOG("zmqServerUpdate: %i", counter);
    char rcvdMessage[256];
    int numBytesRcvd = zmq_recv(zmqSocket, rcvdMessage, sizeof(rcvdMessage)-1, ZMQ_DONTWAIT);
    char statusString[256];
    do
    {
        if (numBytesRcvd >= 0)
        {   // ZMQ received a message.
            rcvdMessage[numBytesRcvd] = '\0'; // nul-termimate rcvdMessage
            sprintf(statusString, "zmqServerUpdate: (tick %i) receivedMessage='%s' sending ACK", counter, rcvdMessage);
            {
                char sendMessage[512];
                sprintf(sendMessage, "DataRateKbps %i   QueueDelayMs %i", (int)(rateLimit_dataRateBytesPerSec / 128.0f), (int)rateLimit_queueDelayMs);
                //showStatus(sendMessage);
                zmq_send(zmqSocket, sendMessage, strlen(sendMessage), 0);
            }
            int intParam = -1;
            float fltParam = -1.0f;
            if (1 == sscanf(rcvdMessage, "LagDelayMs %i", &intParam))
            {   // Scanned "LagDelayMs" request
                sprintf(statusString, "zmqServerUpdate: (tick %i) receivedMessage='%s' sending ACK parsed LagDelayMs=%i", counter, rcvdMessage, intParam);
                IupSetInt(timeInput, "VALUE", intParam);
                IupTriggerValueChangedCallback(timeInput);
            }
            else if (1 == sscanf(rcvdMessage, "DropChancePct %f", &fltParam))
            {   // Scanned "DropChancePct" request
                sprintf(statusString, "zmqServerUpdate: (tick %i) receivedMessage='%s' sending ACK parsed DropChancePct=%f", counter, rcvdMessage, fltParam);
                IupSetFloat(chanceInput, "VALUE", fltParam);
                IupTriggerValueChangedCallback(chanceInput);
            }
            else if (1 == sscanf(rcvdMessage, "BandwidthLimitKBps %i", &intParam))
            {   // Scanned "BandwidthLimitKBps" request
                sprintf(statusString, "zmqServerUpdate: (tick %i) receivedMessage='%s' sending ACK parsed BandwidthLimitKBps=%i", counter, rcvdMessage, intParam);
                IupSetInt(bandwidthInput, "VALUE", intParam);
                IupTriggerValueChangedCallback(bandwidthInput);
            }
            else if (1 == sscanf(rcvdMessage, "RateLimitMbps %i", &intParam))
            {   // Scanned "RateLimitMbps" request
                sprintf(statusString, "zmqServerUpdate: (tick %i) receivedMessage='%s' sending ACK parsed RateLimitMbps=%i", counter, rcvdMessage, intParam);
                IupSetInt(rlDataRateCapMbps, "VALUE", intParam);
                IupTriggerValueChangedCallback(rlDataRateCapMbps);
            }
            // See if ZMQ has another message to process right away.
            // Note that if only 1 client is connected,
            // because of how ZeroMQ does send/recv in pairs when in REQ-REP mode,
            // the presence now of another message implies the client got the ACK and responded very fast
            // -- not that the client queued up multiple messages before the zmq_recv above.
            numBytesRcvd = zmq_recv(zmqSocket, rcvdMessage, sizeof(rcvdMessage) - 1, ZMQ_DONTWAIT);
        }
        else if (EAGAIN == errno)
        {   // ZMQ received nothing.
            sprintf(statusString, "zmqServerUpdate: (tick %i) numBytesRcvd=%i errno=EAGAIN", counter, numBytesRcvd);
        }
        else if (EFSM == errno)
        {
            sprintf(statusString, "zmqServerUpdate: (tick %i) numBytesRcvd=%i ERROR errno=EFSM", counter, numBytesRcvd);
        }
        else
        {
            sprintf(statusString, "zmqServerUpdate: (tick %i) numBytesRcvd=%i ERROR errno=%i", counter, numBytesRcvd, errno);
        }
    } while (numBytesRcvd > 0);
    //showStatus(statusString);
    return IUP_DEFAULT;
}

static int zmqUpdate(Ihandle* ih)
{   // Perform per-tick update of ZeroMQ message handlers
#if ZMQ_CLIENT
    return zmqClientUpdate(ih);
#else
    return zmqServerUpdate(ih);
#endif
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




// ! the order decides which module get processed first
Module* modules[MODULE_CNT] = {
    &lagModule,
    &dropModule,
    &throttleModule,
    &dupModule,
    &oodModule,
    &tamperModule,
    &resetModule,
    &bandwidthModule,
    &rateLimitModule,
};

volatile short sendState = SEND_STATUS_NONE;

// global iup handlers
static Ihandle *dialog, *topFrame, *bottomFrame; 
static Ihandle *statusLabel;
static Ihandle *filterText, *filterButton;
Ihandle *filterSelectList;
// timer to update icons
static Ihandle *stateIcon;
static Ihandle *timer;
static Ihandle *timeout = NULL;

void showStatus(const char *line);
static int uiOnDialogShow(Ihandle *ih, int state);
static int uiStopCb(Ihandle *ih);
static int uiStartCb(Ihandle *ih);
static int uiTimerCb(Ihandle *ih);
static int uiTimeoutCb(Ihandle *ih);
static int uiListSelectCb(Ihandle *ih, char *text, int item, int state);
static int uiFilterTextCb(Ihandle *ih);
static void uiSetupModule(Module *module, Ihandle *parent);

// serializing config files using a stupid custom format
#define CONFIG_FILE "config.txt"
#define CONFIG_MAX_RECORDS 64
#define CONFIG_BUF_SIZE 4096
typedef struct {
    char* filterName;
    char* filterValue;
} filterRecord;
UINT filtersSize;
filterRecord filters[CONFIG_MAX_RECORDS] = {0};
char configBuf[CONFIG_BUF_SIZE+2]; // add some padding to write \n
BOOL parameterized = 0; // parameterized flag, means reading args from command line

// loading up filters and fill in
void loadConfig() {
    char path[MSG_BUFSIZE];
    char *p;
    FILE *f;
    GetModuleFileName(NULL, path, MSG_BUFSIZE);
    LOG("Executable path: %s", path);
    p = strrchr(path, '\\');
    if (p == NULL) p = strrchr(path, '/'); // holy shit
    strcpy(p+1, CONFIG_FILE);
    LOG("Config path: %s", path);
    f = fopen(path, "r");
    if (f) {
        size_t len;
        char *current, *last;
        len = fread(configBuf, sizeof(char), CONFIG_BUF_SIZE, f);
        if (len == CONFIG_BUF_SIZE) {
            LOG("Config file is larger than %d bytes, get truncated.", CONFIG_BUF_SIZE);
        }
        // always patch in a newline at the end to ease parsing
        configBuf[len] = '\n';
        configBuf[len+1] = '\0';

        // parse out the kv pairs. isn't quite safe
        filtersSize = 0;
        last = current = configBuf;
        do {
            // eat up empty lines
EAT_SPACE:  while (isspace(*current)) { ++current; }
            if (*current == '#') {
                current = strchr(current, '\n');
                if (!current) break;
                current = current + 1;
                goto EAT_SPACE;
            }

            // now we can start
            last = current;
            current = strchr(last, ':');
            if (!current) break;
            *current = '\0';
            filters[filtersSize].filterName = last;
            current += 1;
            while (isspace(*current)) { ++current; } // eat potential space after :
            last = current;
            current = strchr(last, '\n');
            if (!current) break;
            filters[filtersSize].filterValue = last;
            *current = '\0';
            if (*(current-1) == '\r') *(current-1) = 0;
            last = current = current + 1;
            ++filtersSize;
        } while (last && last - configBuf < CONFIG_BUF_SIZE);
        LOG("Loaded %u records.", filtersSize);
    }

    if (!f || filtersSize == 0)
    {
        LOG("Failed to load from config. Fill in a simple one.");
        // config is missing or ill-formed. fill in some simple ones
        filters[filtersSize].filterName = "loopback packets";
        filters[filtersSize].filterValue = "outbound and ip.DstAddr >= 127.0.0.1 and ip.DstAddr <= 127.255.255.255";
        filtersSize = 1;
    }
}

void init(int argc, char* argv[]) {
    UINT ix;
    Ihandle *topVbox, *bottomVbox, *dialogVBox, *controlHbox;
    Ihandle *noneIcon, *doingIcon, *errorIcon;
    char* arg_value = NULL;

    // fill in config
    loadConfig();

    // iup inits
    IupOpen(&argc, &argv);

    // this is so easy to get wrong so it's pretty worth noting in the program
    statusLabel = IupLabel("NOTICE: When capturing localhost (loopback) packets, you CAN'T include inbound criteria.\n"
        "Filters like 'udp' need to be 'udp and outbound' to work. See readme for more info.");
    IupSetAttribute(statusLabel, "EXPAND", "HORIZONTAL");
    IupSetAttribute(statusLabel, "PADDING", "8x8");

    topFrame = IupFrame(
        topVbox = IupVbox(
            filterText = IupText(NULL),
            controlHbox = IupHbox(
                stateIcon = IupLabel(NULL),
                filterButton = IupButton("Start", NULL),
                IupFill(),
                IupLabel("Presets:  "),
                filterSelectList = IupList(NULL),
                NULL
            ),
            NULL
        )
    );

    // parse arguments and set globals *before* setting up UI.
    // arguments can be read and set after callbacks are setup
    // FIXME as Release is built as WindowedApp, stdout/stderr won't show
    LOG("argc: %d", argc);
    if (argc > 1) {
        if (!parseArgs(argc, argv)) {
            fprintf(stderr, "invalid argument count. ensure you're using options as \"--drop on\"");
            exit(-1); // fail fast.
        }
        parameterized = 1;
    }

    IupSetAttribute(topFrame, "TITLE", "Filtering");
    IupSetAttribute(topFrame, "EXPAND", "HORIZONTAL");
    IupSetAttribute(filterText, "EXPAND", "HORIZONTAL");
    IupSetCallback(filterText, "VALUECHANGED_CB", (Icallback)uiFilterTextCb);
    IupSetAttribute(filterButton, "PADDING", "8x");
    IupSetCallback(filterButton, "ACTION", uiStartCb);
    IupSetAttribute(topVbox, "NCMARGIN", "4x4");
    IupSetAttribute(topVbox, "NCGAP", "4x2");
    IupSetAttribute(controlHbox, "ALIGNMENT", "ACENTER");

    // setup state icon
    IupSetAttribute(stateIcon, "IMAGE", "none_icon");
    IupSetAttribute(stateIcon, "PADDING", "4x");

    // fill in options and setup callback
    IupSetAttribute(filterSelectList, "VISIBLECOLUMNS", "24");
    IupSetAttribute(filterSelectList, "DROPDOWN", "YES");
    for (ix = 0; ix < filtersSize; ++ix) {
        char ixBuf[4];
        sprintf(ixBuf, "%d", ix+1); // ! staring from 1, following lua indexing
        IupStoreAttribute(filterSelectList, ixBuf, filters[ix].filterName);
    }
    IupSetAttribute(filterSelectList, "VALUE", "1");
    IupSetCallback(filterSelectList, "ACTION", (Icallback)uiListSelectCb);
    // set filter text value since the callback won't take effect before main loop starts
    IupSetAttribute(filterText, "VALUE", filters[0].filterValue);

    // functionalities frame 
    bottomFrame = IupFrame(
        bottomVbox = IupVbox(
            NULL
        )
    );
    IupSetAttribute(bottomFrame, "TITLE", "Functions");
    IupSetAttribute(bottomVbox, "NCMARGIN", "4x4");
    IupSetAttribute(bottomVbox, "NCGAP", "4x2");

    // create icons
    noneIcon = IupImage(8, 8, icon8x8);
    doingIcon = IupImage(8, 8, icon8x8);
    errorIcon = IupImage(8, 8, icon8x8);
    IupSetAttribute(noneIcon, "0", "BGCOLOR");
    IupSetAttribute(noneIcon, "1", "224 224 224");
    IupSetAttribute(doingIcon, "0", "BGCOLOR");
    IupSetAttribute(doingIcon, "1", "109 170 44");
    IupSetAttribute(errorIcon, "0", "BGCOLOR");
    IupSetAttribute(errorIcon, "1", "208 70 72");
    IupSetHandle("none_icon", noneIcon);
    IupSetHandle("doing_icon", doingIcon);
    IupSetHandle("error_icon", errorIcon);

    // setup module uis
    for (ix = 0; ix < MODULE_CNT; ++ix) {
        uiSetupModule(*(modules+ix), bottomVbox);
    }

    // dialog
    dialog = IupDialog(
        dialogVBox = IupVbox(
            topFrame,
            bottomFrame,
            statusLabel,
            NULL
        )
    );

    IupSetAttribute(dialog, "TITLE", "clumsy " CLUMSY_VERSION "_MichaGou_varLag_zmq");
    IupSetAttribute(dialog, "SIZE", "480x"); // add padding manually to width
    IupSetAttribute(dialog, "RESIZE", "NO");
    IupSetCallback(dialog, "SHOW_CB", (Icallback)uiOnDialogShow);


    // global layout settings to affect childrens
    IupSetAttribute(dialogVBox, "ALIGNMENT", "ACENTER");
    IupSetAttribute(dialogVBox, "NCMARGIN", "4x4");
    IupSetAttribute(dialogVBox, "NCGAP", "4x2");

    // setup timer
    timer = IupTimer();
    IupSetAttribute(timer, "TIME", STR(ICON_UPDATE_MS));
    IupSetCallback(timer, "ACTION_CB", uiTimerCb);

    // setup timeout of program
    arg_value = IupGetGlobal("timeout");
    if(arg_value != NULL)
    {
        char valueBuf[16];
        sprintf(valueBuf, "%s000", arg_value);  // convert from seconds to milliseconds

        timeout = IupTimer();
        IupStoreAttribute(timeout, "TIME", valueBuf);
        IupSetCallback(timeout, "ACTION_CB", uiTimeoutCb);
        IupSetAttribute(timeout, "RUN", "YES");
    }
}

void startup() {
    // initialize seed
    srand((unsigned int)time(NULL));

    zmqSetup();

    // kickoff event loops
    IupShowXY(dialog, IUP_CENTER, IUP_CENTER);
    IupMainLoop();
    // ! main loop won't return until program exit
}

void cleanup() {

    IupDestroy(timer);
    if (timeout) {
        IupDestroy(timeout);
    }

    IupClose();
    endTimePeriod(); // try close if not closing
}

// ui logics
void showStatus(const char *line) {
    IupStoreAttribute(statusLabel, "TITLE", line); 
}

// in fact only 32bit binary would run on 64 bit os
// if this happens pop out message box and exit
static BOOL check32RunningOn64(HWND hWnd) {
    BOOL is64ret;
    // consider IsWow64Process return value
    if (IsWow64Process(GetCurrentProcess(), &is64ret) && is64ret) {
        MessageBox(hWnd, (LPCSTR)"You're running 32bit clumsy on 64bit Windows, which wouldn't work. Please use the 64bit clumsy version.",
            (LPCSTR)"Aborting", MB_OK);
        return TRUE;
    }
    return FALSE;
}

static BOOL checkIsRunning() {
    //It will be closed and destroyed when programm terminates (according to MSDN).
    HANDLE hStartEvent = CreateEventW(NULL, FALSE, FALSE, L"Global\\CLUMSY_IS_RUNNING_EVENT_NAME");

    if (hStartEvent == NULL)
        return TRUE;

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hStartEvent);
        hStartEvent = NULL;
        return TRUE;
    }

    return FALSE;
}


static int uiOnDialogShow(Ihandle *ih, int state) {
    // only need to process on show
    HWND hWnd;
    HICON icon;
    HINSTANCE hInstance;
    if (state != IUP_SHOW) return IUP_DEFAULT;
    hWnd = (HWND)IupGetAttribute(ih, "HWND");
    hInstance = GetModuleHandle(NULL);

    // set application icon
    icon = LoadIcon(hInstance, "CLUMSY_ICON");
    SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
    SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);

#if ZMQ_NO_FILTER // for development, do not run elevated, so that original process can be debugged
    return IUP_DEFAULT;
#else

    BOOL exit;
    exit = checkIsRunning();
    if (exit) {
        MessageBox(hWnd, (LPCSTR)"Theres' already an instance of clumsy running.",
            (LPCSTR)"Aborting", MB_OK);
        return IUP_CLOSE;
    }

#ifdef _WIN32
    exit = check32RunningOn64(hWnd);
    if (exit) {
        return IUP_CLOSE;
    }
#endif

    // try elevate and decides whether to exit
    exit = tryElevate(hWnd, parameterized);

    if (!exit && parameterized) {
        setFromParameter(filterText, "VALUE", "filter");
        LOG("is parameterized, start filtering upon execution.");
        uiStartCb(filterButton);
    }
    else if(exit)
    {   // tryElevate wants to terminate the original process, so clean up ZMQ first so replacement process can bind the socket.
        zmqCleanup();
    }
    return exit ? IUP_CLOSE : IUP_DEFAULT;
#endif
}

static int uiStartCb(Ihandle *ih) {
    UNREFERENCED_PARAMETER(ih);
#if ! ZMQ_NO_FILTER // Run WinDivert in regular builds (not ZMQ-specific development builds)
    char buf[MSG_BUFSIZE];
    if (divertStart(IupGetAttribute(filterText, "VALUE"), buf) == 0) {
        showStatus(buf);
        return IUP_DEFAULT;
    }
#endif

    tracingWriteEnabledEvent(
        TRUE,
        modules,
        MODULE_CNT
    );

    // successfully started
    showStatus("Started filtering. Enable functionalities to take effect.");
    IupSetAttribute(filterText, "ACTIVE", "NO");
    IupSetAttribute(filterButton, "TITLE", "Stop");
    IupSetCallback(filterButton, "ACTION", uiStopCb);
    IupSetAttribute(timer, "RUN", "YES");

    return IUP_DEFAULT;
}

static int uiStopCb(Ihandle *ih) {
    int ix;
    UNREFERENCED_PARAMETER(ih);

    tracingWriteEnabledEvent(
        FALSE,
        modules,
        MODULE_CNT
    );
    
    // try stopping
    IupSetAttribute(filterButton, "ACTIVE", "NO");
    IupFlush(); // flush to show disabled state
    divertStop();

    IupSetAttribute(filterText, "ACTIVE", "YES");
    IupSetAttribute(filterButton, "TITLE", "Start");
    IupSetAttribute(filterButton, "ACTIVE", "YES");
    IupSetCallback(filterButton, "ACTION", uiStartCb);

    // stop timer and clean up icons
    IupSetAttribute(timer, "RUN", "NO");
    for (ix = 0; ix < MODULE_CNT; ++ix) {
        modules[ix]->processTriggered = 0; // use = here since is threads already stopped
        IupSetAttribute(modules[ix]->iconHandle, "IMAGE", "none_icon");
    }
    sendState = SEND_STATUS_NONE;
    IupSetAttribute(stateIcon, "IMAGE", "none_icon");

    showStatus("Stopped. To begin again, edit criteria and click Start.");
    return IUP_DEFAULT;
}

static int uiToggleControls(Ihandle *ih, int state) {
    Ihandle *controls = (Ihandle*)IupGetAttribute(ih, CONTROLS_HANDLE);
    short *target = (short*)IupGetAttribute(ih, SYNCED_VALUE);
    int controlsActive = IupGetInt(controls, "ACTIVE");
    int running = (timer != NULL) ? (memcmp("YES", IupGetAttribute(timer, "RUN"), 3) == 0) : 0;

    if (controlsActive && !state) {
        IupSetAttribute(controls, "ACTIVE", "NO");
        InterlockedExchange16(target, I2S(state));
        tracingWriteEnabledEvent(
            running,
            modules,
            MODULE_CNT
        );

    } else if (!controlsActive && state) {
        IupSetAttribute(controls, "ACTIVE", "YES");
        InterlockedExchange16(target, I2S(state));
        tracingWriteEnabledEvent(
            running,
            modules,
            MODULE_CNT
        );

    }

    return IUP_DEFAULT;
}

static int uiTimerCb(Ihandle *ih) {
    int ix;
    UNREFERENCED_PARAMETER(ih);
    zmqUpdate(ih);

    for (ix = 0; ix < MODULE_CNT; ++ix) {
        if (modules[ix]->processTriggered) {
            IupSetAttribute(modules[ix]->iconHandle, "IMAGE", "doing_icon");
            InterlockedAnd16(&(modules[ix]->processTriggered), 0);
        } else {
            IupSetAttribute(modules[ix]->iconHandle, "IMAGE", "none_icon");
        }
    }

    // update global send status icon
    switch (sendState)
    {
    case SEND_STATUS_NONE:
        IupSetAttribute(stateIcon, "IMAGE", "none_icon");
        break;
    case SEND_STATUS_SEND:
        IupSetAttribute(stateIcon, "IMAGE", "doing_icon");
        InterlockedAnd16(&sendState, SEND_STATUS_NONE);
        break;
    case SEND_STATUS_FAIL:
        IupSetAttribute(stateIcon, "IMAGE", "error_icon");
        InterlockedAnd16(&sendState, SEND_STATUS_NONE);
        break;
    }

    if( * rateLimitModule.enabledFlag )
    {
        static int counter = 0; ++counter;
        static float dataRateSmoothed = 0.0f;
        static const float gain = 0.2f;
        const float delta = rateLimit_dataRateBytesPerSec - dataRateSmoothed;
        dataRateSmoothed += gain * delta;
        if (dataRateSmoothed != dataRateSmoothed) dataRateSmoothed = rateLimit_dataRateBytesPerSec;
        if (counter > 5)
        {
            char statusString[512];
            sprintf(statusString, "data rate (Kbps)=%4i    queue delay (ms)=%i", (int)( dataRateSmoothed / 128.0f /*1024/8=128*/), (int)rateLimit_queueDelayMs);
            showStatus(statusString);
            counter = 0;
        }
    }

    return IUP_DEFAULT;
}

static int uiTimeoutCb(Ihandle *ih) {
    UNREFERENCED_PARAMETER(ih);
    return IUP_CLOSE;
 }

static int uiListSelectCb(Ihandle *ih, char *text, int item, int state) {
    UNREFERENCED_PARAMETER(text);
    UNREFERENCED_PARAMETER(ih);
    if (state == 1) {
        IupSetAttribute(filterText, "VALUE", filters[item-1].filterValue);
    }
    return IUP_DEFAULT;
}

static int uiFilterTextCb(Ihandle *ih)  {
    UNREFERENCED_PARAMETER(ih);
    // unselect list
    IupSetAttribute(filterSelectList, "VALUE", "0");
    return IUP_DEFAULT;
}

static void uiSetupModule(Module *module, Ihandle *parent) {
    Ihandle *groupBox, *toggle, *controls, *icon;
    groupBox = IupHbox(
        icon = IupLabel(NULL),
        toggle = IupToggle(module->displayName, NULL),
        IupFill(),
        controls = module->setupUIFunc(),
        NULL
    );
    IupSetAttribute(groupBox, "EXPAND", "HORIZONTAL");
    IupSetAttribute(groupBox, "ALIGNMENT", "ACENTER");
    IupSetAttribute(controls, "ALIGNMENT", "ACENTER");
    IupAppend(parent, groupBox);

    // set controls as attribute to toggle and enable toggle callback
    IupSetCallback(toggle, "ACTION", (Icallback)uiToggleControls);
    IupSetAttribute(toggle, CONTROLS_HANDLE, (char*)controls);
    IupSetAttribute(toggle, SYNCED_VALUE, (char*)module->enabledFlag);
    IupSetAttribute(controls, "ACTIVE", "NO"); // startup as inactive
    IupSetAttribute(controls, "NCGAP", "4"); // startup as inactive

    // set default icon
    IupSetAttribute(icon, "IMAGE", "none_icon");
    IupSetAttribute(icon, "PADDING", "4x");
    module->iconHandle = icon;

    // parameterize toggle
    if (parameterized) {
        setFromParameter(toggle, "VALUE", module->shortName);
    }
}

int zmqSendCommand(int argc, char* argv[]) {
    if (argc > 2) {
        size_t s = strlen(argv[1]);
        if (s > 10) s = 10;
        if (memcmp(argv[1], "--sendcmd", s) == 0) {

            void* context = zmq_ctx_new();
            void* requester = zmq_socket(context, ZMQ_REQ);
            zmq_connect(requester, "tcp://localhost:5555");
            zmq_send(requester, "LagDelayMs 10", 14, 0);
            zmq_close(requester);
            zmq_ctx_destroy(context);

            return 1;
        }
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (zmqSendCommand(argc, argv)) {
        return 0;
    }
    LOG("Is Run As Admin: %d", IsRunAsAdmin());
    LOG("Is Elevated: %d", IsElevated());
    tracingSetup();
    init(argc, argv);
    startup();
    cleanup();
    tracingTeardown();
    return 0;
}
