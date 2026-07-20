/* SPDX-License-Identifier: MIT */
/*
 * ied_reporter.c — libIEC61850 IED client adapter for Phase 2C URCB testing.
 *
 * Phase 2C-b: libiec61850 IED client → go-iec61850 server (URCB reporting)
 *
 * Connects to the IED server at --host HOST --port PORT, enables URCB urcb01,
 * writes GGIO1.SPS1.stVal[ST] to trigger a dchg report, waits for the report,
 * emits it as JSON Lines to stdout, then disables the URCB and disconnects.
 *
 * Operation sequence:
 *   1. get-rcb          — read URCB attributes from server
 *   2. enable-rcb       — write RptEna=true
 *   3. write (ST)       — InteropLD/GGIO1.SPS1.stVal = !initial (triggers dchg)
 *   4. receive-report   — wait for InformationReport (10 s timeout)
 *   5. disable-rcb      — write RptEna=false
 *   6. conclude         — close connection
 *
 * JSON output format (one object per line to stdout):
 *   {"operation":"get-rcb","target":"...","ok":true,"rptID":"...","rptEna":false}
 *   {"operation":"enable-rcb","target":"...","ok":true}
 *   {"operation":"write","target":"...","ok":true}
 *   {"operation":"receive-report","ok":true,"rptID":"...","seqNum":N,
 *    "inclusion":[true,false],"values":[<v>],"reasons":["data-change"]}
 *   {"operation":"disable-rcb","target":"...","ok":true}
 *   {"operation":"conclude","ok":true}
 *   {"operation":"<op>","ok":false,"error":"<msg>"}
 *
 * Exit codes:
 *   0  — all operations completed (individual results in stdout)
 *   1  — argument / startup error
 *   2  — connection failure
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <time.h>

#include "iec61850_client.h"
#include "jsonlines.h"

/* -------------------------------------------------------------------------
 * Constants — fixture-specific
 * ---------------------------------------------------------------------- */

#define RCB_REF     "InteropLD/LLN0.RP.urcb01"
#define RCB_RPTID   "interop_urcb01"
#define SPS1_REF    "InteropLD/GGIO1.SPS1.stVal"
#define DS_SIZE     2   /* dsInterop: SPS1.stVal, Mod.stVal */

/* ALL_RCB_ATTRIBUTES was added to the public API in a later libiec61850 release.
 * At the pinned SHA we sum all known RCB_ELEMENT_* bits explicitly. */
#ifndef ALL_RCB_ATTRIBUTES
#define ALL_RCB_ATTRIBUTES \
    (RCB_ELEMENT_RPT_ID | RCB_ELEMENT_RPT_ENA | RCB_ELEMENT_RESV | \
     RCB_ELEMENT_DATSET | RCB_ELEMENT_CONF_REV | RCB_ELEMENT_OPT_FLDS | \
     RCB_ELEMENT_BUF_TM | RCB_ELEMENT_SQ_NUM  | RCB_ELEMENT_TRG_OPS | \
     RCB_ELEMENT_INTG_PD | RCB_ELEMENT_GI | RCB_ELEMENT_PURGE_BUF | \
     RCB_ELEMENT_ENTRY_ID | RCB_ELEMENT_TIME_OF_ENTRY | \
     RCB_ELEMENT_RESV_TMS | RCB_ELEMENT_OWNER)
#endif

/* -------------------------------------------------------------------------
 * Shared report state (written from report callback, read from main)
 * ---------------------------------------------------------------------- */

typedef struct {
    sem_t       sem;
    bool        received;
    char        rptID[128];
    uint32_t    seqNum;
    /* Dataset members in order */
    bool        inclusion[DS_SIZE];
    /* Boolean value for member 0 (SPS1.stVal) if included */
    bool        val0;
    bool        val0_present;
    /* Integer value for member 1 (Mod.stVal) if included */
    int32_t     val1;
    bool        val1_present;
    /* Reason-for-inclusion per included member */
    ReasonForInclusion reasons[DS_SIZE];
    int         reason_count;
} ReportState;

/* -------------------------------------------------------------------------
 * JSON helpers
 * ---------------------------------------------------------------------- */

static void emit_error(const char* op, const char* target, const char* msg)
{
    if (target)
        printf("{\"operation\":\"%s\",\"target\":\"%s\",\"ok\":false,\"error\":\"%s\"}\n",
               op, target, msg);
    else
        printf("{\"operation\":\"%s\",\"ok\":false,\"error\":\"%s\"}\n", op, msg);
    fflush(stdout);
}

static const char* ied_error_str(IedClientError err)
{
    switch (err) {
    case IED_ERROR_OK:                    return "ok";
    case IED_ERROR_NOT_CONNECTED:         return "not-connected";
    case IED_ERROR_ALREADY_CONNECTED:     return "already-connected";
    case IED_ERROR_CONNECTION_LOST:       return "connection-lost";
    case IED_ERROR_SERVICE_NOT_SUPPORTED: return "service-not-supported";
    case IED_ERROR_TIMEOUT:               return "timeout";
    case IED_ERROR_ACCESS_DENIED:         return "access-denied";
    case IED_ERROR_OBJECT_DOES_NOT_EXIST: return "object-does-not-exist";
    case IED_ERROR_OBJECT_EXISTS:         return "object-exists";
    case IED_ERROR_TYPE_INCONSISTENT:     return "type-inconsistent";
    default:                              return "unknown-error";
    }
}

static const char* reason_str(ReasonForInclusion r)
{
    switch (r) {
    case IEC61850_REASON_DATA_CHANGE:    return "data-change";
    case IEC61850_REASON_QUALITY_CHANGE: return "quality-change";
    case IEC61850_REASON_DATA_UPDATE:    return "data-update";
    case IEC61850_REASON_INTEGRITY:      return "integrity";
    case IEC61850_REASON_GI:             return "gi";
    default:                             return "unknown";
    }
}

/* -------------------------------------------------------------------------
 * Report callback
 * ---------------------------------------------------------------------- */

static void reportCallback(void* param, ClientReport report)
{
    ReportState* st = (ReportState*)param;

    /* Basic fields */
    const char* id = ClientReport_getRptId(report);
    strncpy(st->rptID, id ? id : "", sizeof(st->rptID) - 1);
    st->seqNum = ClientReport_getSeqNum(report);

    /* Dataset values */
    MmsValue* dsValues = ClientReport_getDataSetValues(report);
    int n = (dsValues && (MmsValue_getType(dsValues) == MMS_STRUCTURE ||
                          MmsValue_getType(dsValues) == MMS_ARRAY))
            ? MmsValue_getArraySize(dsValues)
            : 0;
    if (n > DS_SIZE) n = DS_SIZE;

    st->reason_count = 0;
    for (int i = 0; i < DS_SIZE; i++) {
        ReasonForInclusion reason = ClientReport_getReasonForInclusion(report, i);
        st->inclusion[i] = (reason != IEC61850_REASON_NOT_INCLUDED);
        if (st->inclusion[i] && st->reason_count < DS_SIZE) {
            st->reasons[st->reason_count++] = reason;
        }
        if (!st->inclusion[i] || i >= n)
            continue;

        MmsValue* mval = MmsValue_getElement(dsValues, i);
        if (!mval) continue;

        if (i == 0 && MmsValue_getType(mval) == MMS_BOOLEAN) {
            st->val0 = MmsValue_getBoolean(mval);
            st->val0_present = true;
        } else if (i == 1 && MmsValue_getType(mval) == MMS_INTEGER) {
            st->val1 = MmsValue_toInt32(mval);
            st->val1_present = true;
        }
    }

    st->received = true;
    sem_post(&st->sem);
}

/* -------------------------------------------------------------------------
 * Emit the received report as a JSON Line
 * ---------------------------------------------------------------------- */

static void emit_report(const ReportState* st)
{
    printf("{\"operation\":\"receive-report\",\"ok\":true"
           ",\"rptID\":\"%s\",\"seqNum\":%u"
           ",\"inclusion\":[",
           st->rptID, st->seqNum);

    for (int i = 0; i < DS_SIZE; i++) {
        if (i > 0) printf(",");
        printf("%s", st->inclusion[i] ? "true" : "false");
    }
    printf("],\"values\":[");

    bool first_val = true;
    if (st->inclusion[0]) {
        printf("%s", st->val0_present ? (st->val0 ? "true" : "false") : "null");
        first_val = false;
    }
    if (st->inclusion[1]) {
        if (!first_val) printf(",");
        if (st->val1_present)
            printf("%d", st->val1);
        else
            printf("null");
    }

    printf("],\"reasons\":[");
    for (int i = 0; i < st->reason_count; i++) {
        if (i > 0) printf(",");
        printf("\"%s\"", reason_str(st->reasons[i]));
    }
    printf("]}\n");
    fflush(stdout);
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(int argc, char** argv)
{
    jl_handle_meta_flags(argc, argv, "libiec61850");
    const char* host     = "localhost";
    int         port     = 1102;
    bool        initial  = true; /* initial value of SPS1.stVal from values.json */

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host") && i + 1 < argc)
            host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc)
            port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sps1-initial") && i + 1 < argc)
            initial = (atoi(argv[++i]) != 0);
    }
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "ied-reporter: invalid port %d\n", port);
        return 1;
    }

    /* ------------------------------------------------------------------
     * Connect
     * ---------------------------------------------------------------- */
    IedConnection conn = IedConnection_create();
    IedClientError err = IED_ERROR_OK;

    IedConnection_connect(conn, &err, host, port);
    if (err != IED_ERROR_OK) {
        fprintf(stderr, "ied-reporter: connect %s:%d: %s\n",
                host, port, ied_error_str(err));
        IedConnection_destroy(conn);
        return 2;
    }

    /* ------------------------------------------------------------------
     * 1. get-rcb — read current URCB attributes
     * ---------------------------------------------------------------- */
    /* Pass NULL to allocate a new ClientReportControlBlock.
     * ALL_RCB_ATTRIBUTES is a bitmask for setRCBValues, not getRCBValues. */
    ClientReportControlBlock rcb =
        IedConnection_getRCBValues(conn, &err, RCB_REF, NULL);
    if (err != IED_ERROR_OK) {
        emit_error("get-rcb", RCB_REF, ied_error_str(err));
        IedConnection_close(conn);
        IedConnection_destroy(conn);
        return 0;
    }
    printf("{\"operation\":\"get-rcb\",\"target\":\"%s\",\"ok\":true"
           ",\"rptID\":\"%s\",\"rptEna\":%s}\n",
           RCB_REF,
           ClientReportControlBlock_getRptId(rcb) ? ClientReportControlBlock_getRptId(rcb) : "",
           ClientReportControlBlock_getRptEna(rcb) ? "true" : "false");
    fflush(stdout);

    /* ------------------------------------------------------------------
     * 2. enable-rcb — install handler and set RptEna=true
     * ---------------------------------------------------------------- */
    ReportState state;
    memset(&state, 0, sizeof(state));
    sem_init(&state.sem, 0, 0);

    IedConnection_installReportHandler(conn, RCB_REF, RCB_RPTID,
                                       reportCallback, &state);

    ClientReportControlBlock_setRptEna(rcb, true);
    IedConnection_setRCBValues(conn, &err, rcb, RCB_ELEMENT_RPT_ENA, true);
    if (err != IED_ERROR_OK) {
        emit_error("enable-rcb", RCB_REF, ied_error_str(err));
        ClientReportControlBlock_destroy(rcb);
        sem_destroy(&state.sem);
        IedConnection_close(conn);
        IedConnection_destroy(conn);
        return 0;
    }
    printf("{\"operation\":\"enable-rcb\",\"target\":\"%s\",\"ok\":true}\n",
           RCB_REF);
    fflush(stdout);

    /* ------------------------------------------------------------------
     * 3. write — toggle SPS1.stVal to trigger dchg
     * ---------------------------------------------------------------- */
    bool new_val = !initial;
    IedConnection_writeBooleanValue(conn, &err, SPS1_REF, IEC61850_FC_ST, new_val);
    if (err != IED_ERROR_OK) {
        emit_error("write", SPS1_REF "[ST]", ied_error_str(err));
        /* continue; we still want to disable the URCB */
    } else {
        printf("{\"operation\":\"write\",\"target\":\"%s[ST]\",\"ok\":true}\n",
               SPS1_REF);
        fflush(stdout);
    }

    /* ------------------------------------------------------------------
     * 4. receive-report — wait up to 10 s for the dchg report
     * ---------------------------------------------------------------- */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 10;

    if (sem_timedwait(&state.sem, &ts) == 0 && state.received) {
        emit_report(&state);
    } else {
        emit_error("receive-report", NULL, "timeout-waiting-for-dchg-report");
    }

    /* ------------------------------------------------------------------
     * 5. disable-rcb
     * ---------------------------------------------------------------- */
    ClientReportControlBlock_setRptEna(rcb, false);
    IedConnection_setRCBValues(conn, &err, rcb, RCB_ELEMENT_RPT_ENA, true);
    if (err != IED_ERROR_OK) {
        emit_error("disable-rcb", RCB_REF, ied_error_str(err));
    } else {
        printf("{\"operation\":\"disable-rcb\",\"target\":\"%s\",\"ok\":true}\n",
               RCB_REF);
        fflush(stdout);
    }

    ClientReportControlBlock_destroy(rcb);
    sem_destroy(&state.sem);

    /* ------------------------------------------------------------------
     * 6. conclude
     * ---------------------------------------------------------------- */
    IedConnection_close(conn);
    printf("{\"operation\":\"conclude\",\"ok\":true}\n");
    fflush(stdout);

    IedConnection_destroy(conn);
    return 0;
}
