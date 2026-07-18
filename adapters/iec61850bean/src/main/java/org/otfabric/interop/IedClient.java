// SPDX-License-Identifier: MIT
package org.otfabric.interop;

import com.beanit.iec61850bean.*;

import java.net.InetAddress;
import java.util.*;

/**
 * iec61850bean IED client adapter for mms-interop Phase 2B.
 *
 * <p>Executes the same fixed operation sequence as the libiec61850 IED client
 * (see {@code adapters/libiec61850/src/ied_client.c}) and emits one JSON Line
 * per operation to stdout, using identical field names and value encoding.
 *
 * <p>Operation sequence:
 * <ol>
 *   <li>get-server-directory  — list logical devices</li>
 *   <li>get-ld-directory      — list logical nodes in InteropLD</li>
 *   <li>get-ln-directory      — list unique data objects in InteropLD/GGIO1</li>
 *   <li>read (ST)             — InteropLD/GGIO1.SPS1.stVal</li>
 *   <li>read (MX)             — InteropLD/MMXU1.TotW.mag.f</li>
 *   <li>read (CF)             — InteropLD/LLN0.Mod.ctlModel</li>
 *   <li>read (DC)             — InteropLD/LLN0.Mod.d</li>
 *   <li>write (ST)            — InteropLD/LLN0.Mod.stVal = 5</li>
 *   <li>read-dataset          — InteropLD/LLN0$dsInterop</li>
 *   <li>conclude              — disconnect</li>
 * </ol>
 *
 * <p>Command-line arguments:
 * <pre>
 *   --host H    server host (default: localhost)
 *   --port P    server TCP port (default: 1102)
 * </pre>
 */
public class IedClient {

    public static void run(String[] args) throws Exception {
        String host = "localhost";
        int    port = 1102;

        for (int i = 0; i < args.length - 1; i++) {
            switch (args[i]) {
                case "--host" -> host = args[++i];
                case "--port" -> port = Integer.parseInt(args[++i]);
            }
        }

        ClientSap clientSap = new ClientSap();
        ClientAssociation assoc;
        try {
            assoc = clientSap.associate(InetAddress.getByName(host), port, null, null);
        } catch (Exception e) {
            JsonLines.error("conclude", null, "connect " + host + ":" + port + ": " + e.getMessage());
            System.exit(2);
            return;
        }

        try {
            // Retrieve the complete model tree (triggers all GetDirectory services).
            ServerModel model = assoc.retrieveModel();
            // Populate persistent dataset definitions (dsInterop is defined in the ICD).
            assoc.updateDataSets();

            // 1. get-server-directory
            List<String> ldNames = new ArrayList<>();
            for (ModelNode ld : model) {
                ldNames.add(ld.getName());
            }
            JsonLines.successNames("get-server-directory", null, ldNames);

            // 2. get-ld-directory for InteropLD
            ModelNode interopLD = model.findModelNode("InteropLD", null);
            if (interopLD == null) {
                JsonLines.error("get-ld-directory", "InteropLD", "not found in model");
            } else {
                List<String> lnNames = new ArrayList<>();
                for (ModelNode ln : interopLD) {
                    lnNames.add(ln.getName());
                }
                JsonLines.successNames("get-ld-directory", "InteropLD", lnNames);

                // 3. get-ln-directory for InteropLD/GGIO1
                // FCDOs with the same DO name but different FCs appear as separate children;
                // emit unique DO names preserving first-seen order.
                ModelNode ggio1 = model.findModelNode("InteropLD/GGIO1", null);
                if (ggio1 == null) {
                    JsonLines.error("get-ln-directory", "InteropLD/GGIO1", "not found in model");
                } else {
                    Set<String> seen = new LinkedHashSet<>();
                    for (ModelNode fcdo : ggio1) {
                        seen.add(fcdo.getName());
                    }
                    JsonLines.successNames("get-ln-directory", "InteropLD/GGIO1",
                            new ArrayList<>(seen));
                }
            }

            // 4. read GGIO1.SPS1.stVal[ST]
            readBool(assoc, model, "InteropLD/GGIO1.SPS1.stVal", Fc.ST,
                    "InteropLD/GGIO1.SPS1.stVal[ST]");

            // 5. read MMXU1.TotW.mag.f[MX]
            // Read at the FcDataObject level (TotW[MX]) to avoid any server-side
            // restriction on single-BDA reads for nested constructed attributes.
            readFloatMagF(assoc, model);

            // 6. read LLN0.Mod.ctlModel[CF]
            readInt8(assoc, model, "InteropLD/LLN0.Mod.ctlModel", Fc.CF,
                    "InteropLD/LLN0.Mod.ctlModel[CF]");

            // 7. read LLN0.Mod.d[DC]
            readString(assoc, model, "InteropLD/LLN0.Mod.d", Fc.DC,
                    "InteropLD/LLN0.Mod.d[DC]");

            // 8. write LLN0.Mod.stVal[ST] = 5
            writeInt32(assoc, model, "InteropLD/LLN0.Mod.stVal", Fc.ST,
                    "InteropLD/LLN0.Mod.stVal[ST]", 5);

            // 9. read-dataset InteropLD/LLN0$dsInterop
            readDataSet(assoc, model, "InteropLD/LLN0$dsInterop");

        } finally {
            // 10. conclude — always attempt a clean disconnect.
            try {
                assoc.disconnect();
            } catch (Exception ignored) {}
            JsonLines.success("conclude");
        }
    }

    // -----------------------------------------------------------------------
    // Read helpers
    // -----------------------------------------------------------------------

    private static void readBool(ClientAssociation assoc, ServerModel model,
                                 String ref, Fc fc, String target) {
        ModelNode node = model.findModelNode(ref, fc);
        if (!(node instanceof BdaBoolean bda)) {
            JsonLines.error("read", target, "model node not found or wrong type");
            return;
        }
        try {
            assoc.getDataValues((FcModelNode) bda);
            JsonLines.successReadBool(target, bda.getValue());
        } catch (Exception e) {
            JsonLines.error("read", target, e.getMessage());
        }
    }

    private static void readFloatMagF(ClientAssociation assoc, ServerModel model) {
        // Read TotW[MX] as a whole, then extract mag.f from the populated model.
        String target = "InteropLD/MMXU1.TotW.mag.f[MX]";
        ModelNode totW = model.findModelNode("InteropLD/MMXU1.TotW", Fc.MX);
        if (!(totW instanceof FcModelNode totWFc)) {
            JsonLines.error("read", target, "TotW[MX] not found in model");
            return;
        }
        try {
            assoc.getDataValues(totWFc);
            // Navigate to mag.f — values were updated in-place by getDataValues.
            ModelNode f = model.findModelNode("InteropLD/MMXU1.TotW.mag.f", Fc.MX);
            if (!(f instanceof BdaFloat32 bda)) {
                JsonLines.error("read", target, "TotW.mag.f[MX] not found after read");
                return;
            }
            JsonLines.successReadFloat(target, bda.getFloat());
        } catch (Exception e) {
            JsonLines.error("read", target, e.getMessage());
        }
    }

    private static void readInt8(ClientAssociation assoc, ServerModel model,
                                 String ref, Fc fc, String target) {
        ModelNode node = model.findModelNode(ref, fc);
        if (!(node instanceof BdaInt8 bda)) {
            JsonLines.error("read", target, "model node not found or wrong type");
            return;
        }
        try {
            assoc.getDataValues((FcModelNode) bda);
            JsonLines.successReadInt(target, bda.getValue());
        } catch (Exception e) {
            JsonLines.error("read", target, e.getMessage());
        }
    }

    private static void readString(ClientAssociation assoc, ServerModel model,
                                   String ref, Fc fc, String target) {
        ModelNode node = model.findModelNode(ref, fc);
        if (!(node instanceof BdaVisibleString bda)) {
            JsonLines.error("read", target, "model node not found or wrong type");
            return;
        }
        try {
            assoc.getDataValues((FcModelNode) bda);
            JsonLines.successReadString(target, bda.getStringValue());
        } catch (Exception e) {
            JsonLines.error("read", target, e.getMessage());
        }
    }

    // -----------------------------------------------------------------------
    // Write helper
    // -----------------------------------------------------------------------

    private static void writeInt32(ClientAssociation assoc, ServerModel model,
                                   String ref, Fc fc, String target, int value) {
        ModelNode node = model.findModelNode(ref, fc);
        if (!(node instanceof BdaInt32 bda)) {
            JsonLines.error("write", target, "model node not found or wrong type");
            return;
        }
        try {
            bda.setValue(value);
            assoc.setDataValues((FcModelNode) bda);
            JsonLines.successTarget("write", target);
        } catch (Exception e) {
            JsonLines.error("write", target, e.getMessage());
        }
    }

    // -----------------------------------------------------------------------
    // Dataset helper
    // -----------------------------------------------------------------------

    private static void readDataSet(ClientAssociation assoc, ServerModel model,
                                    String dsRef) {
        DataSet ds = model.getDataSet(dsRef);
        if (ds == null) {
            JsonLines.error("read-dataset", dsRef, "dataset not found in model");
            return;
        }
        try {
            assoc.getDataSetValues(ds);
            List<FcModelNode> members = ds.getMembers();
            StringBuilder sb = new StringBuilder("[");
            for (int i = 0; i < members.size(); i++) {
                if (i > 0) sb.append(",");
                sb.append(memberValueJson(members.get(i)));
            }
            sb.append("]");
            JsonLines.successDataSet(dsRef, sb.toString());
        } catch (Exception e) {
            JsonLines.error("read-dataset", dsRef, e.getMessage());
        }
    }

    private static String memberValueJson(FcModelNode member) {
        if (member instanceof BdaBoolean bda)      return String.valueOf(bda.getValue());
        if (member instanceof BdaInt32 bda)        return String.valueOf(bda.getValue());
        if (member instanceof BdaInt8 bda)         return String.valueOf(bda.getValue());
        if (member instanceof BdaFloat32 bda)      return String.valueOf(bda.getFloat());
        if (member instanceof BdaVisibleString bda) return JsonLines.quote(bda.getStringValue());
        return "null";
    }
}
