/* SPDX-License-Identifier: MIT */
/*
 * ied_controller.c — libIEC61850 IED client adapter for Phase 2E/2I/2J control testing.
 *
 * Phase 2E: direct control in both directions
 * Phase 2I: SBO (select-before-operate) with normal security
 * Phase 2J: SBOw (select-before-operate) with enhanced security
 *
 * Connects to the IED server at --host HOST --port PORT, performs the
 * appropriate control sequence for the target DO, then reads back stVal.
 *
 * Supported targets:
 *   SPCSO1 — direct-with-normal-security (ctlModel=1): operate only
 *   SPCSO2 — sbo-with-normal-security (ctlModel=2): select then operate
 *   SPCSO3 — sbo-with-enhanced-security (ctlModel=4): selectWithValue then operate
 *
 * Operation sequences:
 *   Direct (SPCSO1):
 *     1. read-ctlmodel  — InteropLD/GGIO1.SPCSO1.ctlModel[CF]  (expect 1)
 *     2. operate        — SPCSO1 ctlVal=<--ctlval>
 *     3. read-stval     — InteropLD/GGIO1.SPCSO1.stVal[ST]
 *     4. conclude       — close connection
 *
 *   SBO Normal (SPCSO2):
 *     1. read-ctlmodel  — InteropLD/GGIO1.SPCSO2.ctlModel[CF]  (expect 2)
 *     2. select         — read InteropLD/GGIO1.SPCSO2.SBO[CO]
 *     3. operate        — SPCSO2 ctlVal=<--ctlval>
 *     4. read-stval     — InteropLD/GGIO1.SPCSO2.stVal[ST]
 *     5. conclude       — close connection
 *
 *   SBO Enhanced / SBOw (SPCSO3):
 *     1. read-ctlmodel  — InteropLD/GGIO1.SPCSO3.ctlModel[CF]  (expect 4)
 *     2. select-with-value — write Oper to InteropLD/GGIO1.SPCSO3.SBOw[CO]
 *     3. operate        — SPCSO3 ctlVal=<--ctlval>
 *     4. read-stval     — InteropLD/GGIO1.SPCSO3.stVal[ST]
 *     5. conclude       — close connection
 *
 * JSON output format (one object per line to stdout):
 *   {"operation":"read-ctlmodel","target":"InteropLD/GGIO1.SPCSO2.ctlModel[CF]","ok":true,"value":2}
 *   {"operation":"select","target":"InteropLD/GGIO1.SPCSO2","ok":true}
 *   {"operation":"select-with-value","target":"InteropLD/GGIO1.SPCSO3","ok":true}
 *   {"operation":"operate","target":"InteropLD/GGIO1.SPCSO2","ok":true,"ctlval":true}
 *   {"operation":"read-stval","target":"InteropLD/GGIO1.SPCSO2.stVal[ST]","ok":true,"value":true}
 *   {"operation":"conclude","ok":true}
 *   {"operation":"<op>","ok":false,"error":"<msg>"}
 *
 * Arguments:
 *   --host <HOST>              server host  (default: localhost)
 *   --port <PORT>              server port  (default: 1102)
 *   --ctlval <0|1>             control value to apply (default: 1 = true)
 *   --do <SPCSO1|SPCSO2|SPCSO3>  target data object (default: SPCSO1)
 *
 * Exit codes:
 *   0  — all operations completed (individual results in stdout)
 *   1  — argument / startup error
 *   2  — connection failure
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "iec61850_client.h"
#include "jsonlines.h"

/* -------------------------------------------------------------------------
 * Per-DO configuration
 * ---------------------------------------------------------------------- */

typedef struct {
    const char* ctl_ref;       /* e.g. "InteropLD/GGIO1.SPCSO2" */
    const char* ctlmodel_ref;  /* e.g. "InteropLD/GGIO1.SPCSO2.ctlModel" */
    const char* stval_ref;     /* e.g. "InteropLD/GGIO1.SPCSO2.stVal" */
    int         expected_ctlmodel;  /* 1=direct, 2=sbo-normal, 4=sbo-enhanced */
} DoConfig;

static const DoConfig DO_CONFIGS[] = {
    {
        "InteropLD/GGIO1.SPCSO1",
        "InteropLD/GGIO1.SPCSO1.ctlModel",
        "InteropLD/GGIO1.SPCSO1.stVal",
        1
    },
    {
        "InteropLD/GGIO1.SPCSO2",
        "InteropLD/GGIO1.SPCSO2.ctlModel",
        "InteropLD/GGIO1.SPCSO2.stVal",
        2
    },
    {
        "InteropLD/GGIO1.SPCSO3",
        "InteropLD/GGIO1.SPCSO3.ctlModel",
        "InteropLD/GGIO1.SPCSO3.stVal",
        4
    },
};

#define NUM_DO_CONFIGS 3

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
    case IED_ERROR_OK:                         return "ok";
    case IED_ERROR_NOT_CONNECTED:              return "not-connected";
    case IED_ERROR_ALREADY_CONNECTED:          return "already-connected";
    case IED_ERROR_CONNECTION_LOST:            return "connection-lost";
    case IED_ERROR_SERVICE_NOT_SUPPORTED:      return "service-not-supported";
    case IED_ERROR_TIMEOUT:                    return "timeout";
    case IED_ERROR_ACCESS_DENIED:              return "access-denied";
    case IED_ERROR_OBJECT_DOES_NOT_EXIST:      return "object-does-not-exist";
    case IED_ERROR_OBJECT_EXISTS:              return "object-exists";
    case IED_ERROR_OBJECT_ACCESS_UNSUPPORTED:  return "object-access-unsupported";
    case IED_ERROR_TYPE_INCONSISTENT:          return "type-inconsistent";
    default:                                   return "unknown-error";
    }
}

/* -------------------------------------------------------------------------
 * Operations
 * ---------------------------------------------------------------------- */

static bool op_read_ctlmodel(IedConnection conn, const DoConfig* cfg)
{
    IedClientError err;
    int32_t val = IedConnection_readInt32Value(conn, &err, cfg->ctlmodel_ref, IEC61850_FC_CF);
    char target_buf[128];
    snprintf(target_buf, sizeof(target_buf), "%s[CF]", cfg->ctlmodel_ref);
    if (err != IED_ERROR_OK) {
        emit_error("read-ctlmodel", target_buf, ied_error_str(err));
        return false;
    }
    printf("{\"operation\":\"read-ctlmodel\",\"target\":\"%s\",\"ok\":true,\"value\":%d}\n",
           target_buf, val);
    fflush(stdout);
    return true;
}

static bool op_select(IedConnection conn, const DoConfig* cfg)
{
    ControlObjectClient ctrl = ControlObjectClient_create(cfg->ctl_ref, conn);
    if (!ctrl) {
        emit_error("select", cfg->ctl_ref, "failed to create ControlObjectClient");
        return false;
    }

    bool ok = ControlObjectClient_select(ctrl);
    IedClientError err = ControlObjectClient_getLastApplError(ctrl).error;

    ControlObjectClient_destroy(ctrl);

    if (!ok) {
        const char* msg = (err != IED_ERROR_OK) ? ied_error_str(err) : "select returned false";
        emit_error("select", cfg->ctl_ref, msg);
        return false;
    }

    printf("{\"operation\":\"select\",\"target\":\"%s\",\"ok\":true}\n", cfg->ctl_ref);
    fflush(stdout);
    return true;
}

static bool op_select_with_value(IedConnection conn, const DoConfig* cfg, bool ctlVal)
{
    ControlObjectClient ctrl = ControlObjectClient_create(cfg->ctl_ref, conn);
    if (!ctrl) {
        emit_error("select-with-value", cfg->ctl_ref, "failed to create ControlObjectClient");
        return false;
    }

    MmsValue* val = MmsValue_newBoolean(ctlVal);
    bool ok = ControlObjectClient_selectWithValue(ctrl, val);
    IedClientError err = ControlObjectClient_getLastApplError(ctrl).error;

    if (!ok) {
        const char* msg = (err != IED_ERROR_OK) ? ied_error_str(err) : "selectWithValue returned false";
        emit_error("select-with-value", cfg->ctl_ref, msg);
        MmsValue_delete(val);
        ControlObjectClient_destroy(ctrl);
        return false;
    }

    printf("{\"operation\":\"select-with-value\",\"target\":\"%s\",\"ok\":true}\n", cfg->ctl_ref);
    fflush(stdout);

    /* Operate using the SAME ControlObjectClient so that libiec61850 sends
     * the same ctlNum that was used during selectWithValue. */
    ok = ControlObjectClient_operate(ctrl, val, 0);
    err = ControlObjectClient_getLastApplError(ctrl).error;

    MmsValue_delete(val);
    ControlObjectClient_destroy(ctrl);

    if (!ok) {
        const char* msg = (err != IED_ERROR_OK) ? ied_error_str(err) : "operate returned false";
        emit_error("operate", cfg->ctl_ref, msg);
        return false;
    }

    printf("{\"operation\":\"operate\",\"target\":\"%s\",\"ok\":true,\"ctlval\":%s}\n",
           cfg->ctl_ref, ctlVal ? "true" : "false");
    fflush(stdout);
    return true;
}

static bool op_operate(IedConnection conn, const DoConfig* cfg, bool ctlVal)
{
    ControlObjectClient ctrl = ControlObjectClient_create(cfg->ctl_ref, conn);
    if (!ctrl) {
        emit_error("operate", cfg->ctl_ref, "failed to create ControlObjectClient");
        return false;
    }

    MmsValue* val = MmsValue_newBoolean(ctlVal);
    bool ok = ControlObjectClient_operate(ctrl, val, 0);
    IedClientError err = ControlObjectClient_getLastApplError(ctrl).error;

    MmsValue_delete(val);
    ControlObjectClient_destroy(ctrl);

    if (!ok) {
        const char* msg = (err != IED_ERROR_OK) ? ied_error_str(err) : "operate returned false";
        emit_error("operate", cfg->ctl_ref, msg);
        return false;
    }

    printf("{\"operation\":\"operate\",\"target\":\"%s\",\"ok\":true,\"ctlval\":%s}\n",
           cfg->ctl_ref, ctlVal ? "true" : "false");
    fflush(stdout);
    return true;
}

static bool op_read_stval(IedConnection conn, const DoConfig* cfg)
{
    IedClientError err;
    bool val = IedConnection_readBooleanValue(conn, &err, cfg->stval_ref, IEC61850_FC_ST);
    char target_buf[128];
    snprintf(target_buf, sizeof(target_buf), "%s[ST]", cfg->stval_ref);
    if (err != IED_ERROR_OK) {
        emit_error("read-stval", target_buf, ied_error_str(err));
        return false;
    }
    printf("{\"operation\":\"read-stval\",\"target\":\"%s\",\"ok\":true,\"value\":%s}\n",
           target_buf, val ? "true" : "false");
    fflush(stdout);
    return true;
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(int argc, char** argv)
{
    jl_handle_meta_flags(argc, argv, "libiec61850");
    const char* host = "localhost";
    int         port = 1102;
    bool        ctlVal = true;
    const char* do_name = "SPCSO1";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host") && i + 1 < argc)
            host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc)
            port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ctlval") && i + 1 < argc)
            ctlVal = atoi(argv[++i]) != 0;
        else if (!strcmp(argv[i], "--do") && i + 1 < argc)
            do_name = argv[++i];
    }
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "ied-controller: invalid port %d\n", port);
        return 1;
    }

    /* Find the DO configuration */
    const DoConfig* cfg = NULL;
    for (int i = 0; i < NUM_DO_CONFIGS; i++) {
        /* Match by the last path component of ctl_ref */
        const char* last_dot = strrchr(DO_CONFIGS[i].ctl_ref, '.');
        const char* do_part = last_dot ? last_dot + 1 : DO_CONFIGS[i].ctl_ref;
        if (!strcmp(do_name, do_part)) {
            cfg = &DO_CONFIGS[i];
            break;
        }
    }
    if (!cfg) {
        fprintf(stderr, "ied-controller: unknown --do %s (supported: SPCSO1, SPCSO2, SPCSO3)\n", do_name);
        return 1;
    }

    IedConnection conn = IedConnection_create();
    IedClientError err;

    IedConnection_connect(conn, &err, host, port);
    if (err != IED_ERROR_OK) {
        fprintf(stderr, "ied-controller: connect %s:%d: %s\n",
                host, port, ied_error_str(err));
        IedConnection_destroy(conn);
        return 2;
    }

    op_read_ctlmodel(conn, cfg);

    /* SBO normal: select before operate */
    if (cfg->expected_ctlmodel == 2) {
        if (!op_select(conn, cfg)) {
            IedConnection_close(conn);
            printf("{\"operation\":\"conclude\",\"ok\":true}\n");
            fflush(stdout);
            IedConnection_destroy(conn);
            return 0;
        }
    }

    /* SBO enhanced: selectWithValue + operate are done atomically (same ctrl
     * object) inside op_select_with_value to ensure ctlNum consistency. */
    if (cfg->expected_ctlmodel == 4) {
        op_select_with_value(conn, cfg, ctlVal);
        op_read_stval(conn, cfg);
        IedConnection_close(conn);
        printf("{\"operation\":\"conclude\",\"ok\":true}\n");
        fflush(stdout);
        IedConnection_destroy(conn);
        return 0;
    }

    op_operate(conn, cfg, ctlVal);
    op_read_stval(conn, cfg);

    IedConnection_close(conn);
    printf("{\"operation\":\"conclude\",\"ok\":true}\n");
    fflush(stdout);

    IedConnection_destroy(conn);
    return 0;
}
