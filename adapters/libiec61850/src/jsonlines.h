/* SPDX-License-Identifier: MIT */
/*
 * jsonlines.h — tiny helpers for emitting JSON Lines on stdout and stderr.
 *
 * stdout: structured JSON Lines (machine-readable).
 * stderr: human-readable log messages.
 *
 * All output is line-buffered; each call flushes its stream.
 */
#ifndef JSONLINES_H
#define JSONLINES_H

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

/*
 * Emit a ready event with adapter metadata.
 * addr    — "host:port" or "0.0.0.0:port"
 * fixture — canonical fixture identifier, e.g. "mms-v1" or "iec61850-v1"
 * adapter — adapter name, e.g. "libiec61850"
 * ied_name — IED name used in MMS domain names (may be empty string)
 * version — image version from $ADAPTER_VERSION env var, fallback "dev"
 */
static inline void jl_ready(const char* addr,
                             const char* fixture,
                             const char* adapter,
                             const char* ied_name)
{
    const char* version = getenv("ADAPTER_VERSION");
    if (!version) version = "dev";
    fprintf(stdout,
            "{\"event\":\"ready\",\"address\":\"%s\","
            "\"fixture\":\"%s\",\"adapter\":\"%s\",\"version\":\"%s\","
            "\"ied_name\":\"%s\"}\n",
            addr, fixture, adapter, version, ied_name ? ied_name : "");
    fflush(stdout);
}

/* Emit a human-readable log line on stderr. */
static inline void jl_log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

/* Emit a fatal error on stderr and exit with the given code. */
static inline void jl_fatal(int code, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
    _Exit(code);
}

#endif /* JSONLINES_H */
