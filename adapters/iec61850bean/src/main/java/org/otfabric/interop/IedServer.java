// SPDX-License-Identifier: MIT
package org.otfabric.interop;

import com.beanit.iec61850bean.*;

import java.util.List;
import java.util.concurrent.CountDownLatch;

/**
 * iec61850bean IED server adapter for mms-interop Phase 2B.
 *
 * <p>Loads {@code interop.icd} via {@link SclParser}, applies initial values
 * from {@code values.json}, starts a non-blocking server, and emits the
 * JSON readiness event on stdout.  All client writes are accepted; no
 * application-layer write logic is needed for the interoperability fixture.
 *
 * <p>Command-line arguments:
 * <pre>
 *   --port  P     TCP port to listen on (default: 1102)
 *   --icd   PATH  SCL/ICD file (default: /fixtures/iec61850/interop.icd)
 *   --values PATH JSON initial-values file (default: /fixtures/iec61850/values.json)
 * </pre>
 */
public class IedServer {

    public static void run(String[] args) throws Exception {
        int port = 1102;
        String icdPath    = "/fixtures/iec61850/interop.icd";
        String valuesPath = "/fixtures/iec61850/values.json";

        for (int i = 0; i < args.length - 1; i++) {
            switch (args[i]) {
                case "--port"   -> port      = Integer.parseInt(args[++i]);
                case "--icd"    -> icdPath   = args[++i];
                case "--values" -> valuesPath = args[++i];
            }
        }

        // Parse the SCL and take the first (only) server model.
        List<ServerModel> models = SclParser.parse(icdPath);
        if (models.isEmpty()) {
            System.err.println("ied-server: no server model found in " + icdPath);
            System.exit(1);
        }
        ServerModel model = models.get(0);
        String iedName = extractIedName(icdPath);

        // Apply initial values before handing the model to ServerSap.
        FixtureValues fv = new FixtureValues(valuesPath);
        applyInitialValues(model, fv, iedName);

        CountDownLatch stopped = new CountDownLatch(1);

        ServerSap sap = new ServerSap(port, 0, null, model, null);

        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            sap.stop();
            stopped.countDown();
        }));

        sap.startListening(new ServerEventListener() {
            @Override
            public List<ServiceError> write(List<BasicDataAttribute> bdas) {
                // Accept all writes so the go-iec61850 client write test passes.
                return null;
            }

            @Override
            public void serverStoppedListening(ServerSap serverSAP) {
                stopped.countDown();
            }
        });

        String version = System.getenv("ADAPTER_VERSION");
        if (version == null) version = "dev";
        System.out.printf(
            "{\"event\":\"ready\",\"address\":\"localhost:%d\",\"fixture\":\"iec61850-v1\",\"adapter\":\"iec61850bean\",\"version\":\"%s\",\"ied_name\":\"%s\"}%n",
            port, version, iedName);
        System.out.flush();

        stopped.await();
    }

    // -----------------------------------------------------------------------

    private static void applyInitialValues(ServerModel model, FixtureValues fv, String iedName) {
        String pfx = iedName + "InteropLD/";
        setInt32(model, pfx + "LLN0.Mod.stVal",       Fc.ST, fv.getInt("InteropLD/LLN0.Mod.stVal"));
        setInt8( model, pfx + "LLN0.Mod.ctlModel",    Fc.CF, fv.getInt("InteropLD/LLN0.Mod.ctlModel"));
        setStr(  model, pfx + "LLN0.Mod.d",           Fc.DC, fv.getString("InteropLD/LLN0.Mod.d"));
        setInt32(model, pfx + "LLN0.Beh.stVal",       Fc.ST, fv.getInt("InteropLD/LLN0.Beh.stVal"));
        setBool( model, pfx + "GGIO1.SPS1.stVal",     Fc.ST, fv.getBoolean("InteropLD/GGIO1.SPS1.stVal"));
        setStr(  model, pfx + "GGIO1.SPS1.d",         Fc.DC, fv.getString("InteropLD/GGIO1.SPS1.d"));
        setBool( model, pfx + "GGIO1.SPCSO1.stVal",   Fc.ST, fv.getBoolean("InteropLD/GGIO1.SPCSO1.stVal"));
        setInt8( model, pfx + "GGIO1.SPCSO1.ctlModel",Fc.CF, fv.getInt("InteropLD/GGIO1.SPCSO1.ctlModel"));
        setFloat(model, pfx + "MMXU1.TotW.mag.f",     Fc.MX, fv.getFloat("InteropLD/MMXU1.TotW.mag.f"));
    }

    private static void setInt32(ServerModel model, String ref, Fc fc, int value) {
        ModelNode node = model.findModelNode(ref, fc);
        if (node instanceof BdaInt32 bda) {
            bda.setValue(value);
        } else {
            warn(ref, fc, "BdaInt32", node);
        }
    }

    private static void setInt8(ServerModel model, String ref, Fc fc, int value) {
        ModelNode node = model.findModelNode(ref, fc);
        if (node instanceof BdaInt8 bda) {
            bda.setValue((byte) value);
        } else {
            warn(ref, fc, "BdaInt8", node);
        }
    }

    private static void setBool(ServerModel model, String ref, Fc fc, boolean value) {
        ModelNode node = model.findModelNode(ref, fc);
        if (node instanceof BdaBoolean bda) {
            bda.setValue(value);
        } else {
            warn(ref, fc, "BdaBoolean", node);
        }
    }

    private static void setStr(ServerModel model, String ref, Fc fc, String value) {
        ModelNode node = model.findModelNode(ref, fc);
        if (node instanceof BdaVisibleString bda) {
            bda.setValue(value);
        } else {
            warn(ref, fc, "BdaVisibleString", node);
        }
    }

    private static void setFloat(ServerModel model, String ref, Fc fc, float value) {
        ModelNode node = model.findModelNode(ref, fc);
        if (node instanceof BdaFloat32 bda) {
            bda.setFloat(value);
        } else {
            warn(ref, fc, "BdaFloat32", node);
        }
    }

    private static void warn(String ref, Fc fc, String expected, ModelNode actual) {
        System.err.printf("ied-server: WARNING: %s[%s] expected %s, got %s%n",
                ref, fc, expected, actual == null ? "null" : actual.getClass().getSimpleName());
    }

    /**
     * Extracts the IED name from an ICD/SCL file by searching for the first
     * {@code <IED name="...">} element. Returns an empty string if not found.
     */
    private static String extractIedName(String icdPath) {
        try {
            String content = new String(java.nio.file.Files.readAllBytes(java.nio.file.Paths.get(icdPath)));
            java.util.regex.Pattern p = java.util.regex.Pattern.compile("<IED\\s[^>]*name=\"([^\"]+)\"");
            java.util.regex.Matcher m = p.matcher(content);
            if (m.find()) return m.group(1);
        } catch (Exception e) {
            System.err.println("ied-server: WARNING: could not extract IED name from " + icdPath + ": " + e.getMessage());
        }
        return "";
    }
}
