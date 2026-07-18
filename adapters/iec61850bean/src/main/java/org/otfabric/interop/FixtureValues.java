// SPDX-License-Identifier: MIT
package org.otfabric.interop;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.io.File;
import java.io.IOException;

/**
 * Typed accessor for fixtures/iec61850/values.json.
 *
 * The JSON structure is:
 * <pre>
 * {
 *   "values": {
 *     "InteropLD/LLN0.Mod.stVal":  1,
 *     "InteropLD/GGIO1.SPS1.stVal": false,
 *     ...
 *   },
 *   "writable": ["InteropLD/GGIO1.SPCSO1"]
 * }
 * </pre>
 *
 * The server and client use this to ensure their initial values and
 * expected-value assertions both derive from the same source.
 */
public final class FixtureValues {

    private final JsonNode values;

    public FixtureValues(String jsonPath) throws IOException {
        JsonNode root = new ObjectMapper().readTree(new File(jsonPath));
        this.values = root.get("values");
        if (this.values == null) {
            throw new IOException("values.json has no 'values' key: " + jsonPath);
        }
    }

    public int getInt(String key) {
        return require(key).asInt();
    }

    public boolean getBoolean(String key) {
        return require(key).asBoolean();
    }

    public String getString(String key) {
        return require(key).asText();
    }

    public float getFloat(String key) {
        return (float) require(key).asDouble();
    }

    private JsonNode require(String key) {
        JsonNode node = values.get(key);
        if (node == null) {
            throw new IllegalArgumentException("Key not found in values.json: " + key);
        }
        return node;
    }
}
