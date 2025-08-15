
/***************************************************************************\
    EXOS Kernel - Network test task (E1000 / generic NETWORK DF_* API)
    Logs in English; no emojis/icons.
\***************************************************************************/

#include "../../include/Kernel.h"
#include "../../include/Console.h"
#include "../../include/Driver.h"
#include "../../include/Network.h"
#include "../../include/Log.h"
#include "../../include/List.h"
#include "../../include/String.h"

#pragma pack(1)
/* Local view matching what E1000_OnGetInfo writes */
typedef struct NETINFO_LOCAL {
    U8  MAC[6];
    U8  LinkUp;
    U32 SpeedMbps;
    U8  DuplexFull;
    U16 MTU;
} NETINFO_LOCAL;
#pragma pack()

// --- Config ---
#define TEST_TX_INTERVAL_MS   1000U    /* send every 1s */
#define TEST_POLL_INTERVAL_MS 10U      /* poll every 10ms */
#define TEST_ETHERTYPE        0x88B5U  /* experimental Ethertype */
#define TEST_MIN_FRAME        60U      /* min Ethernet payload (no FCS) */

// --- State ---
static LPPCI_DEVICE sNetDev = NULL;
static NETINFO_LOCAL sInfo;

// --- Small hex dump (first bytes) ---
static void DumpHexShort(const U8 *buf, U32 len) {
    U32 n = (len > 64U) ? 64U : len;
    STR line[96];
    STR tmp[16];
    U32 i;

    StringCopy(line, TEXT("[RX] DATA: "));
    for (i = 0; i < n; i++) {
        U32ToHexString(buf[i], tmp);
        /* U32ToHexString prints 8 hexes; we need two last chars */
        STR byteTxt[3];
        byteTxt[0] = tmp[6]; byteTxt[1] = tmp[7]; byteTxt[2] = '\0';
        StringConcat(line, byteTxt);
        if ((i + 1) < n) StringConcat(line, TEXT(" "));
        if ((i % 16) == 15) { KernelLogText(LOG_VERBOSE, line); line[0] = '\0'; }
    }
    if (line[0]) KernelLogText(LOG_VERBOSE, line);
}

/* --- RX callback --- */
static void TestNet_Rx(const U8 *frame, U32 len) {
    if (!frame || len < 14U) return;

    U16 ethType = (U16)((frame[12] << 8) | frame[13]);
    KernelLogText(LOG_VERBOSE, TEXT("[RX] Frame len=%u, ethType=0x%04X"), len, ethType);
    DumpHexShort(frame, len);
}

/* --- Find first NETWORK driver device --- */
static LPPCI_DEVICE FindFirstNetworkDevice(void) {
    LPLISTNODE node;
    for (node = Kernel.PCIDevice->First; node; node = node->Next) {
        LPPCI_DEVICE dev = (LPPCI_DEVICE)node;
        if (dev && dev->Driver && dev->Driver->Type == DRIVER_TYPE_NETWORK) {
            return dev;
        }
    }
    return NULL;
}

/* --- Compose and send a broadcast ethernet frame --- */
static U32 SendTestBroadcast(U32 counter) {
    U8 frame[TEST_MIN_FRAME]; /* zero-init below */
    MemorySet(frame, 0, sizeof(frame));

    /* Dest = FF:FF:FF:FF:FF:FF */
    for (U32 i = 0; i < 6; i++) frame[i] = 0xFF;

    /* Src = our MAC from sInfo */
    for (U32 i = 0; i < 6; i++) frame[6 + i] = sInfo.MAC[i];

    /* Ethertype */
    frame[12] = (U8)((TEST_ETHERTYPE >> 8) & 0xFF);
    frame[13] = (U8)(TEST_ETHERTYPE & 0xFF);

    /* Payload: "EXOS NET TEST" + counter */
    static const char txt[] = "EXOS NET TEST";
    U32 pos = 14;
    for (U32 i = 0; i < sizeof(txt) - 1 && pos < TEST_MIN_FRAME; i++) frame[pos++] = (U8)txt[i];

    /* 4 bytes counter (big endian) */
    if (pos + 4 <= TEST_MIN_FRAME) {
        frame[pos++] = (U8)((counter >> 24) & 0xFF);
        frame[pos++] = (U8)((counter >> 16) & 0xFF);
        frame[pos++] = (U8)((counter >>  8) & 0xFF);
        frame[pos++] = (U8)((counter >>  0) & 0xFF);
    }

    /* Pad to minimum frame length (already zeroed) */

    NETWORKSEND snd = { .Device = sNetDev, .Data = frame, .Length = TEST_MIN_FRAME };
    return sNetDev->Driver->Command(DF_NT_SEND, (U32)(LPVOID)&snd);
}

/* --- Periodic poll --- */
static U32 DoPoll(void) {
    NETWORKPOLL poll = { .Device = sNetDev };
    return sNetDev->Driver->Command(DF_NT_POLL, (U32)(LPVOID)&poll);
}

/* --- Network test task --- */
static U32 NetworkTestTask(LPVOID param) {
    (void)param;

    KernelLogText(LOG_VERBOSE, TEXT("[NETTEST] Task started"));

    sNetDev = FindFirstNetworkDevice();
    if (!sNetDev) {
        KernelLogText(LOG_ERROR, TEXT("[NETTEST] No NETWORK device found"));
        return 0;
    }
    KernelLogText(LOG_VERBOSE, TEXT("[NETTEST] Using device %s"), sNetDev->Driver->Product);

    /* Reset (optional) */
    NETWORKRESET rst = { .Device = sNetDev };
    sNetDev->Driver->Command(DF_NT_RESET, (U32)(LPVOID)&rst);

    /* Get info (MAC/link) */
    MemorySet(&sInfo, 0, sizeof(sInfo));
    NETWORKGETINFO gi = { .Device = sNetDev, .Info = (LPNETWORKINFO)(LPVOID)&sInfo };
    sNetDev->Driver->Command(DF_NT_GETINFO, (U32)(LPVOID)&gi);

    KernelLogText(
        LOG_VERBOSE, TEXT("[NETTEST] MAC=%02X:%02X:%02X:%02X:%02X:%02X Link=%s Speed=%u Duplex=%s MTU=%u"),
        sInfo.MAC[0], sInfo.MAC[1], sInfo.MAC[2], sInfo.MAC[3], sInfo.MAC[4], sInfo.MAC[5],
        sInfo.LinkUp ? "UP" : "DOWN", (unsigned)sInfo.SpeedMbps, sInfo.DuplexFull ? "FULL" : "HALF", sInfo.MTU
    );

    /* Install RX callback */
    NETWORKSETRXCB setcb = { .Device = sNetDev, .Callback = TestNet_Rx };
    sNetDev->Driver->Command(DF_NT_SETRXCB, (U32)(LPVOID)&setcb);

    /* Simple loop: poll often, send periodically */
    U32 lastTx = 0, tick = 0, counter = 0;

    while (1) {
        /* Poll RX ring */
        DoPoll();

        /* Tick */
        tick += TEST_POLL_INTERVAL_MS;

        /* Send every TEST_TX_INTERVAL_MS */
        if ((tick - lastTx) >= TEST_TX_INTERVAL_MS) {
            U32 r = SendTestBroadcast(counter++);
            KernelLogText(LOG_VERBOSE, TEXT("[NETTEST] TX #%u result=%X"), counter - 1, r);
            lastTx = tick;
        }

        DoSystemCall(SYSCALL_Sleep, TEST_POLL_INTERVAL_MS);
    }

    return 0;
}

/* Public entry to create the task like in Kernel.c */
void StartTestNetworkTask(void) {
    TASKINFO ti;

    ti.Header.Size = sizeof(TASKINFO);
    ti.Header.Version = EXOS_ABI_VERSION;
    ti.Header.Flags = 0;
    ti.Func = NetworkTestTask;
    ti.StackSize = TASK_MINIMUM_STACK_SIZE;
    ti.Priority = TASK_PRIORITY_MEDIUM;
    ti.Flags = 0;
    ti.Parameter = NULL;
    CreateTask(&KernelProcess, &ti);

    KernelLogText(LOG_VERBOSE, TEXT("[NETTEST] Task created"));
}
