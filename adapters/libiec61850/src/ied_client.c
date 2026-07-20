/* SPDX-License-Identifier: MIT */
/*
 * ied_client.c — libIEC61850 IED client adapter for mms-interop.
 *
 * Phase 2A Step 8: reverse IED direction
 *   libiec61850 IED client → go-iec61850 server
 *
 * Executes the Phase 2A fixed operation sequence against the server at
 * --host HOST --port PORT and emits one JSON Line per operation to stdout.
 *
 * Operation sequence:
 *   1.  get-server-directory  — list logical devices
 *   2.  get-ld-directory      — list logical nodes in InteropLD
 *   3.  get-ln-directory      — list data objects in InteropLD/GGIO1
 *   4.  read (ST)             — InteropLD/GGIO1.SPS1.stVal
 *   5.  read (MX)             — InteropLD/MMXU1.TotW.mag.f
 *   6.  read (CF)             — InteropLD/LLN0.Mod.ctlModel
 *   7.  read (DC)             — InteropLD/LLN0.Mod.d
 *   8.  write (ST)            — InteropLD/LLN0.Mod.stVal = 5
 *   9.  read-dataset          — InteropLD/LLN0$dsInterop
 *  10.  conclude              — close connection
 *
 * JSON output format (one object per line to stdout):
 *   {"operation":"get-server-directory","ok":true,"names":["InteropLD"]}
 *   {"operation":"read","target":"...","ok":true,"value":<v>}
 *   {"operation":"write","target":"...","ok":true}
 *   {"operation":"read-dataset","target":"...","ok":true,"values":[...]}
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

#include "iec61850_client.h"
#include "linked_list.h"
#include "jsonlines.h"

/* -------------------------------------------------------------------------
 * JSON Line helpers
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

static void emit_names(const char* op, const char* target, LinkedList names)
{
    printf("{\"operation\":\"%s\"", op);
    if (target)
        printf(",\"target\":\"%s\"", target);
    printf(",\"ok\":true,\"names\":[");
    bool first = true;
    LinkedList cur = LinkedList_getNext(names);
    while (cur) {
        if (!first) printf(",");
        printf("\"%s\"", (char*)cur->data);
        first = false;
        cur = LinkedList_getNext(cur);
    }
    printf("]}\n");
    fflush(stdout);
}

static void emit_bool(const char* target, bool value)
{
    printf("{\"operation\":\"read\",\"target\":\"%s\",\"ok\":true,\"value\":%s}\n",
           target, value ? "true" : "false");
    fflush(stdout);
}

static void emit_float(const char* target, float value)
{
    printf("{\"operation\":\"read\",\"target\":\"%s\",\"ok\":true,\"value\":%g}\n",
           target, (double)value);
    fflush(stdout);
}

static void emit_int(const char* target, int32_t value)
{
    printf("{\"operation\":\"read\",\"target\":\"%s\",\"ok\":true,\"value\":%d}\n",
           target, value);
    fflush(stdout);
}

static void emit_string(const char* target, const char* value)
{
    printf("{\"operation\":\"read\",\"target\":\"%s\",\"ok\":true,\"value\":\"%s\"}\n",
           target, value ? value : "");
    fflush(stdout);
}

static void emit_write_ok(const char* target)
{
    printf("{\"operation\":\"write\",\"target\":\"%s\",\"ok\":true}\n", target);
    fflush(stdout);
}

static const char* ied_error_str(IedClientError err)
{
    switch (err) {
    case IED_ERROR_OK:                       return "ok";
    case IED_ERROR_NOT_CONNECTED:            return "not-connected";
    case IED_ERROR_ALREADY_CONNECTED:        return "already-connected";
    case IED_ERROR_CONNECTION_LOST:          return "connection-lost";
    case IED_ERROR_SERVICE_NOT_SUPPORTED:    return "service-not-supported";
    case IED_ERROR_TIMEOUT:                  return "timeout";
    case IED_ERROR_ACCESS_DENIED:            return "access-denied";
    case IED_ERROR_OBJECT_DOES_NOT_EXIST:    return "object-does-not-exist";
    case IED_ERROR_OBJECT_EXISTS:            return "object-exists";
    case IED_ERROR_OBJECT_ACCESS_UNSUPPORTED: return "object-access-unsupported";
    case IED_ERROR_TYPE_INCONSISTENT:        return "type-inconsistent";
    default:                                 return "unknown-error";
    }
}

/* -------------------------------------------------------------------------
 * Phase 2A operation helpers
 * ---------------------------------------------------------------------- */

static bool op_get_server_directory(IedConnection conn)
{
    IedClientError err;
    LinkedList lds = IedConnection_getServerDirectory(conn, &err, false);
    if (err != IED_ERROR_OK) {
        emit_error("get-server-directory", NULL, ied_error_str(err));
        return false;
    }
    emit_names("get-server-directory", NULL, lds);
    LinkedList_destroyDeep(lds, free);
    return true;
}

static bool op_get_ld_directory(IedConnection conn, const char* ld)
{
    IedClientError err;
    LinkedList lns = IedConnection_getLogicalDeviceDirectory(conn, &err, ld);
    if (err != IED_ERROR_OK) {
        emit_error("get-ld-directory", ld, ied_error_str(err));
        return false;
    }
    emit_names("get-ld-directory", ld, lns);
    LinkedList_destroyDeep(lns, free);
    return true;
}

static bool op_get_ln_directory(IedConnection conn, const char* ld_ln)
{
    IedClientError err;
    /* getDataObjectTypes=false → returns DO names only */
    LinkedList dos = IedConnection_getLogicalNodeDirectory(conn, &err, ld_ln, false);
    if (err != IED_ERROR_OK) {
        emit_error("get-ln-directory", ld_ln, ied_error_str(err));
        return false;
    }
    emit_names("get-ln-directory", ld_ln, dos);
    LinkedList_destroyDeep(dos, free);
    return true;
}

static bool op_read_bool(IedConnection conn, const char* ref, FunctionalConstraint fc)
{
    IedClientError err;
    bool val = IedConnection_readBooleanValue(conn, &err, ref, fc);
    char target[256];
    snprintf(target, sizeof(target), "%s[%s]", ref, FunctionalConstraint_toString(fc));
    if (err != IED_ERROR_OK) {
        emit_error("read", target, ied_error_str(err));
        return false;
    }
    emit_bool(target, val);
    return true;
}

static bool op_read_float(IedConnection conn, const char* ref, FunctionalConstraint fc)
{
    IedClientError err;
    float val = IedConnection_readFloatValue(conn, &err, ref, fc);
    char target[256];
    snprintf(target, sizeof(target), "%s[%s]", ref, FunctionalConstraint_toString(fc));
    if (err != IED_ERROR_OK) {
        emit_error("read", target, ied_error_str(err));
        return false;
    }
    emit_float(target, val);
    return true;
}

static bool op_read_int(IedConnection conn, const char* ref, FunctionalConstraint fc)
{
    IedClientError err;
    int32_t val = IedConnection_readInt32Value(conn, &err, ref, fc);
    char target[256];
    snprintf(target, sizeof(target), "%s[%s]", ref, FunctionalConstraint_toString(fc));
    if (err != IED_ERROR_OK) {
        emit_error("read", target, ied_error_str(err));
        return false;
    }
    emit_int(target, val);
    return true;
}

static bool op_read_string(IedConnection conn, const char* ref, FunctionalConstraint fc)
{
    IedClientError err;
    char* val = IedConnection_readStringValue(conn, &err, ref, fc);
    char target[256];
    snprintf(target, sizeof(target), "%s[%s]", ref, FunctionalConstraint_toString(fc));
    if (err != IED_ERROR_OK) {
        emit_error("read", target, ied_error_str(err));
        return false;
    }
    emit_string(target, val);
    free(val);
    return true;
}

static bool op_write_int(IedConnection conn, const char* ref,
                         FunctionalConstraint fc, int32_t value)
{
    IedClientError err;
    IedConnection_writeInt32Value(conn, &err, ref, fc, value);
    char target[256];
    snprintf(target, sizeof(target), "%s[%s]", ref, FunctionalConstraint_toString(fc));
    if (err != IED_ERROR_OK) {
        emit_error("write", target, ied_error_str(err));
        return false;
    }
    emit_write_ok(target);
    return true;
}

static bool op_read_dataset(IedConnection conn, const char* dsRef)
{
    IedClientError err;
    ClientDataSet ds = IedConnection_readDataSetValues(conn, &err, dsRef, NULL);
    if (err != IED_ERROR_OK) {
        emit_error("read-dataset", dsRef, ied_error_str(err));
        return false;
    }

    int n = ClientDataSet_getDataSetSize(ds);
    MmsValue* dsArray = ClientDataSet_getValues(ds);
    printf("{\"operation\":\"read-dataset\",\"target\":\"%s\",\"ok\":true,\"values\":[",
           dsRef);

    for (int i = 0; i < n; i++) {
        if (i > 0) printf(",");
        MmsValue* val = dsArray ? MmsValue_getElement(dsArray, i) : NULL;
        if (!val) {
            printf("null");
            continue;
        }
        switch (MmsValue_getType(val)) {
        case MMS_BOOLEAN:
            printf("%s", MmsValue_getBoolean(val) ? "true" : "false");
            break;
        case MMS_INTEGER:
            printf("%d", MmsValue_toInt32(val));
            break;
        case MMS_UNSIGNED:
            printf("%u", MmsValue_toUint32(val));
            break;
        case MMS_FLOAT:
            printf("%g", (double)MmsValue_toFloat(val));
            break;
        case MMS_VISIBLE_STRING:
            printf("\"%s\"", MmsValue_toString(val) ? MmsValue_toString(val) : "");
            break;
        default:
            printf("null");
            break;
        }
    }

    printf("]}\n");
    fflush(stdout);

    ClientDataSet_destroy(ds);
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

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host") && i + 1 < argc)
            host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc)
            port = atoi(argv[++i]);
    }
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "ied-client: invalid port %d\n", port);
        return 1;
    }

    IedConnection conn = IedConnection_create();
    IedClientError err;

    IedConnection_connect(conn, &err, host, port);
    if (err != IED_ERROR_OK) {
        fprintf(stderr, "ied-client: connect %s:%d: %s\n",
                host, port, ied_error_str(err));
        IedConnection_destroy(conn);
        return 2;
    }

    /* Phase 2A fixed sequence */
    op_get_server_directory(conn);
    op_get_ld_directory(conn, "InteropLD");
    op_get_ln_directory(conn, "InteropLD/GGIO1");

    /* Read ST */
    op_read_bool(conn, "InteropLD/GGIO1.SPS1.stVal",  IEC61850_FC_ST);
    /* Read MX */
    op_read_float(conn, "InteropLD/MMXU1.TotW.mag.f", IEC61850_FC_MX);
    /* Read CF */
    op_read_int(conn,   "InteropLD/LLN0.Mod.ctlModel", IEC61850_FC_CF);
    /* Read DC */
    op_read_string(conn, "InteropLD/LLN0.Mod.d",       IEC61850_FC_DC);

    /* Write ST */
    op_write_int(conn, "InteropLD/LLN0.Mod.stVal", IEC61850_FC_ST, 5);

    /* Read dataset */
    op_read_dataset(conn, "InteropLD/LLN0$dsInterop");

    /* Conclude */
    IedConnection_close(conn);
    printf("{\"operation\":\"conclude\",\"ok\":true}\n");
    fflush(stdout);

    IedConnection_destroy(conn);
    return 0;
}
