/* SPDX-License-Identifier: MIT */
/*
 * mms_server.c — libIEC61850 generic MMS server for mms-interop.
 *
 * Phase 0 finding: the generic MMS server requires private libiec61850
 * headers (see SPIKE.md).  Include paths are set by CMake.
 *
 * Lifecycle:
 *   1. Parse --fixture and --port arguments.
 *   2. Load fixture JSON.
 *   3. Build MmsDevice → MmsDomain(s) → MmsVariableSpecification(s).
 *   4. Create MmsServer, install write handler, set identity.
 *   5. Pre-populate value cache (MmsServer_insertIntoCache).
 *   6. Start listening (threaded).
 *   7. Emit {"event":"ready","address":"0.0.0.0:<port>"} on stdout.
 *   8. Wait for SIGTERM/SIGINT.
 *   9. Stop and clean up.
 *
 * Exit codes (REQ-3.6):
 *   0  — clean shutdown
 *   1  — startup / argument error
 *   3  — fixture load error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* Public libiec61850 headers (installed) */
#include "mms_value.h"
#include "mms_server.h"
#include "mms_types.h"

/* Private libiec61850 headers (source-tree, see SPIKE.md) */
#include "mms_server_libinternal.h"   /* MmsServer_create, start/stop, handlers */
#include "mms_device_model.h"         /* MmsDevice_create, MmsDomain_create, struct sMmsDomain */

#include "fixture.h"
#include "jsonlines.h"

/* -------------------------------------------------------------------------
 * Global state
 * ---------------------------------------------------------------------- */

typedef struct {
    char*    domain_name;
    char*    var_name;
    bool     writable;
    MmsValue* cached; /* direct pointer into the server cache; not owned */
} WriteEntry;

static WriteEntry* g_entries  = NULL;
static int         g_nentries = 0;
static MmsServer   g_server   = NULL;

static volatile sig_atomic_t g_running = 1;

/* -------------------------------------------------------------------------
 * Signal handler
 * ---------------------------------------------------------------------- */

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

/* -------------------------------------------------------------------------
 * Write handler
 *
 * Called by MmsServer for every confirmed Write request.
 * We validate writability and update the cache in place.
 * ---------------------------------------------------------------------- */

static MmsDataAccessError write_handler(
    void*              param,
    MmsDomain*         domain,
    const char*        var_id,
    int                array_idx,
    const char*        component_id,
    MmsValue*          value,
    MmsServerConnection conn)
{
    (void)param; (void)array_idx; (void)component_id; (void)conn;

    const char* dom_name = MmsDomain_getName(domain);

    for (int i = 0; i < g_nentries; i++) {
        if (strcmp(g_entries[i].domain_name, dom_name) == 0 &&
            strcmp(g_entries[i].var_name,    var_id)   == 0) {
            if (!g_entries[i].writable)
                return DATA_ACCESS_ERROR_OBJECT_ACCESS_DENIED;

            MmsValue* cached = MmsServer_getValueFromCache(g_server, domain, var_id);
            if (!cached)
                return DATA_ACCESS_ERROR_OBJECT_UNDEFINED;

            MmsValue_update(cached, value);
            return DATA_ACCESS_ERROR_SUCCESS;
        }
    }
    return DATA_ACCESS_ERROR_OBJECT_UNDEFINED;
}

/* -------------------------------------------------------------------------
 * Model construction helpers
 * ---------------------------------------------------------------------- */

static MmsVariableSpecification* clone_spec(const MmsVariableSpecification* src)
{
    MmsVariableSpecification* s =
        (MmsVariableSpecification*)calloc(1, sizeof(MmsVariableSpecification));
    if (!s) return NULL;
    *s = *src;
    s->name = src->name ? strdup(src->name) : NULL;
    return s;
}

static MmsDevice* build_device(FixtureDef* f)
{
    MmsDevice* dev = MmsDevice_create(NULL);
    if (!dev) return NULL;

    dev->domainCount = f->domain_count;
    dev->domains     = (MmsDomain**)calloc(f->domain_count, sizeof(MmsDomain*));
    if (!dev->domains) return NULL;

    for (int di = 0; di < f->domain_count; di++) {
        DomainDef* dd = &f->domains[di];

        MmsDomain* dom = MmsDomain_create(dd->name);
        if (!dom) return NULL;

        dom->namedVariablesCount = dd->var_count;
        dom->namedVariables = (MmsVariableSpecification**)
            calloc(dd->var_count, sizeof(MmsVariableSpecification*));
        if (!dom->namedVariables) return NULL;

        for (int vi = 0; vi < dd->var_count; vi++) {
            dom->namedVariables[vi] = clone_spec(dd->vars[vi].spec);
        }

        dev->domains[di] = dom;
    }
    return dev;
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

int main(int argc, char* argv[])
{
    jl_handle_meta_flags(argc, argv, "libiec61850");
    const char* fixture_path = NULL;
    int         port         = 1102;

    /* argument parsing */
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--fixture") == 0) fixture_path = argv[i + 1];
        if (strcmp(argv[i], "--port")    == 0) port = atoi(argv[i + 1]);
    }

    if (!fixture_path) {
        fixture_path = "/fixtures/mms/interop.json";
    }

    /* load fixture */
    FixtureDef* fix = fixture_load(fixture_path);
    if (!fix) {
        jl_fatal(3, "error: fixture load failed: %s", fixture_path);
    }

    jl_log("loaded fixture: %d domain(s)", fix->domain_count);

    /* build device model */
    MmsDevice* dev = build_device(fix);
    if (!dev) {
        jl_fatal(1, "error: failed to build MMS device model");
    }

    /* create server */
    g_server = MmsServer_create(dev, NULL);
    if (!g_server) {
        jl_fatal(1, "error: MmsServer_create failed");
    }

    /* install write handler */
    MmsServer_installWriteHandler(g_server, write_handler, NULL);

    /* set server identity */
    MmsServer_setServerIdentity(g_server,
        fix->vendor, fix->model, fix->revision);

    /* pre-populate value cache and build write-entry table */
    int total_vars = 0;
    for (int di = 0; di < fix->domain_count; di++)
        total_vars += fix->domains[di].var_count;

    g_entries  = (WriteEntry*)calloc(total_vars, sizeof(WriteEntry));
    g_nentries = 0;

    for (int di = 0; di < fix->domain_count; di++) {
        DomainDef* dd  = &fix->domains[di];
        MmsDomain* dom = dev->domains[di];

        for (int vi = 0; vi < dd->var_count; vi++) {
            VarDef* vd = &dd->vars[vi];

            /* insert a CLONE into the server cache */
            MmsValue* cached_val = MmsValue_clone(vd->value);
            MmsServer_insertIntoCache(g_server, dom, vd->name, cached_val);

            /* keep reference for write handler */
            g_entries[g_nentries].domain_name = dd->name;
            g_entries[g_nentries].var_name    = vd->name;
            g_entries[g_nentries].writable    = vd->writable;
            g_entries[g_nentries].cached      = cached_val;
            g_nentries++;

            jl_log("  registered %s/%s (%s)", dd->name, vd->name,
                   vd->writable ? "rw" : "ro");
        }
    }

    /* signals */
    signal(SIGTERM, on_signal);
    signal(SIGINT,  on_signal);

    /* start listening (threadless mode — runs in the main thread).
     * IsoServer_startListeningThreadless() binds and listens synchronously;
     * MmsServer_isRunning() reports success immediately after the call.
     * MmsServer_waitReady() is a select/poll for INCOMING activity — it must
     * NOT be used as a startup check (see SPIKE.md — Threading model). */
    MmsServer_startListeningThreadless(g_server, port);

    if (!MmsServer_isRunning(g_server)) {
        jl_fatal(1, "error: server failed to start on port %d", port);
    }

    /* emit readiness event */
    char addr_buf[64];
    snprintf(addr_buf, sizeof(addr_buf), "0.0.0.0:%d", port);
    jl_ready(addr_buf, "mms-v1", "libiec61850", "");

    jl_log("listening on port %d", port);

    /* main loop: wait up to 500 ms for incoming activity, then service it */
    while (g_running) {
        MmsServer_waitReady(g_server, 500);
        MmsServer_handleIncomingMessages(g_server);
        MmsServer_handleBackgroundTasks(g_server);
    }

    jl_log("shutting down");
    MmsServer_stopListeningThreadless(g_server);
    MmsServer_destroy(g_server);

    free(g_entries);
    fixture_free(fix);

    return 0;
}
