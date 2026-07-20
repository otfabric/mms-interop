// SPDX-License-Identifier: MIT
package org.otfabric.interop;

import java.io.PrintStream;
import java.util.List;

/**
 * Minimal JSON Lines helpers for the iec61850bean adapter.
 *
 * All output is written to stdout; logging goes to stderr via logback.
 * JSON is hand-built intentionally — adding Jackson just to emit these
 * simple lines is unnecessary coupling.
 */
public final class JsonLines {

    private static final PrintStream OUT = System.out;

    private JsonLines() {}

    /** Emit a successful result with no extra fields. */
    public static void success(String operation) {
        emit("{\"operation\":" + quote(operation) + ",\"ok\":true}");
    }

    /** Emit a successful result with one target field and no value. */
    public static void successTarget(String operation, String target) {
        emit("{\"operation\":" + quote(operation)
                + ",\"target\":" + quote(target)
                + ",\"ok\":true}");
    }

    /** Emit a successful directory result (names array). */
    public static void successNames(String operation, String target, List<String> names) {
        StringBuilder sb = new StringBuilder("{\"operation\":");
        sb.append(quote(operation));
        if (target != null) {
            sb.append(",\"target\":").append(quote(target));
        }
        sb.append(",\"ok\":true,\"names\":").append(namesArray(names)).append("}");
        emit(sb.toString());
    }

    /** Emit a successful read result (boolean). */
    public static void successReadBool(String target, boolean value) {
        emit("{\"operation\":\"read\",\"target\":" + quote(target)
                + ",\"ok\":true,\"value\":" + value + "}");
    }

    /** Emit a successful read result with a named operation (boolean). */
    public static void successReadBoolOp(String operation, String target, boolean value) {
        emit("{\"operation\":" + quote(operation) + ",\"target\":" + quote(target)
                + ",\"ok\":true,\"value\":" + value + "}");
    }

    /** Emit a successful read result (integer). */
    public static void successReadInt(String target, long value) {
        emit("{\"operation\":\"read\",\"target\":" + quote(target)
                + ",\"ok\":true,\"value\":" + value + "}");
    }

    /** Emit a successful read result with a named operation (integer). */
    public static void successReadIntOp(String operation, String target, long value) {
        emit("{\"operation\":" + quote(operation) + ",\"target\":" + quote(target)
                + ",\"ok\":true,\"value\":" + value + "}");
    }

    /** Emit a successful read result (float). */
    public static void successReadFloat(String target, float value) {
        emit("{\"operation\":\"read\",\"target\":" + quote(target)
                + ",\"ok\":true,\"value\":" + value + "}");
    }

    /** Emit a successful read result (string). */
    public static void successReadString(String target, String value) {
        emit("{\"operation\":\"read\",\"target\":" + quote(target)
                + ",\"ok\":true,\"value\":" + quote(value) + "}");
    }

    /** Emit a successful dataset read result. */
    public static void successDataSet(String target, String valuesJson) {
        emit("{\"operation\":\"read-dataset\",\"target\":" + quote(target)
                + ",\"ok\":true,\"values\":" + valuesJson + "}");
    }

    /** Emit a failed operation. */
    public static void error(String operation, String target, String message) {
        StringBuilder sb = new StringBuilder("{\"operation\":");
        sb.append(quote(operation));
        if (target != null) {
            sb.append(",\"target\":").append(quote(target));
        }
        sb.append(",\"ok\":false,\"error\":").append(quote(message)).append("}");
        emit(sb.toString());
    }

    /** JSON-encode a string value, including surrounding quotes. */
    public static String quote(String s) {
        if (s == null) return "null";
        return "\"" + s.replace("\\", "\\\\")
                       .replace("\"", "\\\"")
                       .replace("\n", "\\n")
                       .replace("\r", "\\r") + "\"";
    }

    /** Build a JSON array of quoted strings. */
    public static String namesArray(List<String> names) {
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < names.size(); i++) {
            if (i > 0) sb.append(",");
            sb.append(quote(names.get(i)));
        }
        sb.append("]");
        return sb.toString();
    }

    private static void emit(String line) {
        OUT.println(line);
        OUT.flush();
    }
}
