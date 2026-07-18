/*
 * static_model.c
 *
 * automatically generated from fixtures/iec61850/interop.icd
 */
#include "static_model.h"

static void initializeValues();

extern DataSet iedModelds_InteropLD_LLN0_dsInterop;


extern DataSetEntry iedModelds_InteropLD_LLN0_dsInterop_fcda0;
extern DataSetEntry iedModelds_InteropLD_LLN0_dsInterop_fcda1;

DataSetEntry iedModelds_InteropLD_LLN0_dsInterop_fcda0 = {
  "InteropLD",
  false,
  "GGIO1$ST$SPS1$stVal", 
  -1,
  NULL,
  NULL,
  &iedModelds_InteropLD_LLN0_dsInterop_fcda1
};

DataSetEntry iedModelds_InteropLD_LLN0_dsInterop_fcda1 = {
  "InteropLD",
  false,
  "LLN0$ST$Mod$stVal", 
  -1,
  NULL,
  NULL,
  NULL
};

DataSet iedModelds_InteropLD_LLN0_dsInterop = {
  "InteropLD",
  "LLN0$dsInterop",
  2,
  &iedModelds_InteropLD_LLN0_dsInterop_fcda0,
  NULL
};

LogicalDevice iedModel_InteropLD = {
    LogicalDeviceModelType,
    "InteropLD",
    (ModelNode*) &iedModel,
    NULL,
    (ModelNode*) &iedModel_InteropLD_LLN0,
    NULL
};

LogicalNode iedModel_InteropLD_LLN0 = {
    LogicalNodeModelType,
    "LLN0",
    (ModelNode*) &iedModel_InteropLD,
    (ModelNode*) &iedModel_InteropLD_GGIO1,
    (ModelNode*) &iedModel_InteropLD_LLN0_Mod,
};

DataObject iedModel_InteropLD_LLN0_Mod = {
    DataObjectModelType,
    "Mod",
    (ModelNode*) &iedModel_InteropLD_LLN0,
    (ModelNode*) &iedModel_InteropLD_LLN0_Beh,
    (ModelNode*) &iedModel_InteropLD_LLN0_Mod_stVal,
    0,
    -1
};

DataAttribute iedModel_InteropLD_LLN0_Mod_stVal = {
    DataAttributeModelType,
    "stVal",
    (ModelNode*) &iedModel_InteropLD_LLN0_Mod,
    (ModelNode*) &iedModel_InteropLD_LLN0_Mod_q,
    NULL,
    0,
    -1,
    IEC61850_FC_ST,
    IEC61850_INT32,
    0 + TRG_OPT_DATA_CHANGED,
    NULL,
    0};

DataAttribute iedModel_InteropLD_LLN0_Mod_q = {
    DataAttributeModelType,
    "q",
    (ModelNode*) &iedModel_InteropLD_LLN0_Mod,
    (ModelNode*) &iedModel_InteropLD_LLN0_Mod_t,
    NULL,
    0,
    -1,
    IEC61850_FC_ST,
    IEC61850_QUALITY,
    0 + TRG_OPT_QUALITY_CHANGED,
    NULL,
    0};

DataAttribute iedModel_InteropLD_LLN0_Mod_t = {
    DataAttributeModelType,
    "t",
    (ModelNode*) &iedModel_InteropLD_LLN0_Mod,
    (ModelNode*) &iedModel_InteropLD_LLN0_Mod_ctlModel,
    NULL,
    0,
    -1,
    IEC61850_FC_ST,
    IEC61850_TIMESTAMP,
    0,
    NULL,
    0};

DataAttribute iedModel_InteropLD_LLN0_Mod_ctlModel = {
    DataAttributeModelType,
    "ctlModel",
    (ModelNode*) &iedModel_InteropLD_LLN0_Mod,
    (ModelNode*) &iedModel_InteropLD_LLN0_Mod_d,
    NULL,
    0,
    -1,
    IEC61850_FC_CF,
    IEC61850_ENUMERATED,
    0,
    NULL,
    0};

DataAttribute iedModel_InteropLD_LLN0_Mod_d = {
    DataAttributeModelType,
    "d",
    (ModelNode*) &iedModel_InteropLD_LLN0_Mod,
    NULL,
    NULL,
    0,
    -1,
    IEC61850_FC_DC,
    IEC61850_VISIBLE_STRING_255,
    0,
    NULL,
    0};

DataObject iedModel_InteropLD_LLN0_Beh = {
    DataObjectModelType,
    "Beh",
    (ModelNode*) &iedModel_InteropLD_LLN0,
    NULL,
    (ModelNode*) &iedModel_InteropLD_LLN0_Beh_stVal,
    0,
    -1
};

DataAttribute iedModel_InteropLD_LLN0_Beh_stVal = {
    DataAttributeModelType,
    "stVal",
    (ModelNode*) &iedModel_InteropLD_LLN0_Beh,
    (ModelNode*) &iedModel_InteropLD_LLN0_Beh_q,
    NULL,
    0,
    -1,
    IEC61850_FC_ST,
    IEC61850_INT32,
    0 + TRG_OPT_DATA_CHANGED,
    NULL,
    0};

DataAttribute iedModel_InteropLD_LLN0_Beh_q = {
    DataAttributeModelType,
    "q",
    (ModelNode*) &iedModel_InteropLD_LLN0_Beh,
    (ModelNode*) &iedModel_InteropLD_LLN0_Beh_t,
    NULL,
    0,
    -1,
    IEC61850_FC_ST,
    IEC61850_QUALITY,
    0 + TRG_OPT_QUALITY_CHANGED,
    NULL,
    0};

DataAttribute iedModel_InteropLD_LLN0_Beh_t = {
    DataAttributeModelType,
    "t",
    (ModelNode*) &iedModel_InteropLD_LLN0_Beh,
    NULL,
    NULL,
    0,
    -1,
    IEC61850_FC_ST,
    IEC61850_TIMESTAMP,
    0,
    NULL,
    0};

LogicalNode iedModel_InteropLD_GGIO1 = {
    LogicalNodeModelType,
    "GGIO1",
    (ModelNode*) &iedModel_InteropLD,
    (ModelNode*) &iedModel_InteropLD_MMXU1,
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPS1,
};

DataObject iedModel_InteropLD_GGIO1_SPS1 = {
    DataObjectModelType,
    "SPS1",
    (ModelNode*) &iedModel_InteropLD_GGIO1,
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPCSO1,
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPS1_stVal,
    0,
    -1
};

DataAttribute iedModel_InteropLD_GGIO1_SPS1_stVal = {
    DataAttributeModelType,
    "stVal",
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPS1,
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPS1_q,
    NULL,
    0,
    -1,
    IEC61850_FC_ST,
    IEC61850_BOOLEAN,
    0 + TRG_OPT_DATA_CHANGED,
    NULL,
    0};

DataAttribute iedModel_InteropLD_GGIO1_SPS1_q = {
    DataAttributeModelType,
    "q",
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPS1,
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPS1_t,
    NULL,
    0,
    -1,
    IEC61850_FC_ST,
    IEC61850_QUALITY,
    0 + TRG_OPT_QUALITY_CHANGED,
    NULL,
    0};

DataAttribute iedModel_InteropLD_GGIO1_SPS1_t = {
    DataAttributeModelType,
    "t",
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPS1,
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPS1_d,
    NULL,
    0,
    -1,
    IEC61850_FC_ST,
    IEC61850_TIMESTAMP,
    0,
    NULL,
    0};

DataAttribute iedModel_InteropLD_GGIO1_SPS1_d = {
    DataAttributeModelType,
    "d",
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPS1,
    NULL,
    NULL,
    0,
    -1,
    IEC61850_FC_DC,
    IEC61850_VISIBLE_STRING_255,
    0,
    NULL,
    0};

DataObject iedModel_InteropLD_GGIO1_SPCSO1 = {
    DataObjectModelType,
    "SPCSO1",
    (ModelNode*) &iedModel_InteropLD_GGIO1,
    NULL,
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPCSO1_stVal,
    0,
    -1
};

DataAttribute iedModel_InteropLD_GGIO1_SPCSO1_stVal = {
    DataAttributeModelType,
    "stVal",
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPCSO1,
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPCSO1_q,
    NULL,
    0,
    -1,
    IEC61850_FC_ST,
    IEC61850_BOOLEAN,
    0 + TRG_OPT_DATA_CHANGED,
    NULL,
    0};

DataAttribute iedModel_InteropLD_GGIO1_SPCSO1_q = {
    DataAttributeModelType,
    "q",
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPCSO1,
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPCSO1_t,
    NULL,
    0,
    -1,
    IEC61850_FC_ST,
    IEC61850_QUALITY,
    0 + TRG_OPT_QUALITY_CHANGED,
    NULL,
    0};

DataAttribute iedModel_InteropLD_GGIO1_SPCSO1_t = {
    DataAttributeModelType,
    "t",
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPCSO1,
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPCSO1_ctlModel,
    NULL,
    0,
    -1,
    IEC61850_FC_ST,
    IEC61850_TIMESTAMP,
    0,
    NULL,
    0};

DataAttribute iedModel_InteropLD_GGIO1_SPCSO1_ctlModel = {
    DataAttributeModelType,
    "ctlModel",
    (ModelNode*) &iedModel_InteropLD_GGIO1_SPCSO1,
    NULL,
    NULL,
    0,
    -1,
    IEC61850_FC_CF,
    IEC61850_ENUMERATED,
    0,
    NULL,
    0};

LogicalNode iedModel_InteropLD_MMXU1 = {
    LogicalNodeModelType,
    "MMXU1",
    (ModelNode*) &iedModel_InteropLD,
    NULL,
    (ModelNode*) &iedModel_InteropLD_MMXU1_TotW,
};

DataObject iedModel_InteropLD_MMXU1_TotW = {
    DataObjectModelType,
    "TotW",
    (ModelNode*) &iedModel_InteropLD_MMXU1,
    NULL,
    (ModelNode*) &iedModel_InteropLD_MMXU1_TotW_mag,
    0,
    -1
};

DataAttribute iedModel_InteropLD_MMXU1_TotW_mag = {
    DataAttributeModelType,
    "mag",
    (ModelNode*) &iedModel_InteropLD_MMXU1_TotW,
    (ModelNode*) &iedModel_InteropLD_MMXU1_TotW_q,
    (ModelNode*) &iedModel_InteropLD_MMXU1_TotW_mag_f,
    0,
    -1,
    IEC61850_FC_MX,
    IEC61850_CONSTRUCTED,
    0 + TRG_OPT_DATA_CHANGED,
    NULL,
    0};

DataAttribute iedModel_InteropLD_MMXU1_TotW_mag_f = {
    DataAttributeModelType,
    "f",
    (ModelNode*) &iedModel_InteropLD_MMXU1_TotW_mag,
    NULL,
    NULL,
    0,
    -1,
    IEC61850_FC_MX,
    IEC61850_FLOAT32,
    0 + TRG_OPT_DATA_CHANGED,
    NULL,
    0};

DataAttribute iedModel_InteropLD_MMXU1_TotW_q = {
    DataAttributeModelType,
    "q",
    (ModelNode*) &iedModel_InteropLD_MMXU1_TotW,
    (ModelNode*) &iedModel_InteropLD_MMXU1_TotW_t,
    NULL,
    0,
    -1,
    IEC61850_FC_MX,
    IEC61850_QUALITY,
    0 + TRG_OPT_QUALITY_CHANGED,
    NULL,
    0};

DataAttribute iedModel_InteropLD_MMXU1_TotW_t = {
    DataAttributeModelType,
    "t",
    (ModelNode*) &iedModel_InteropLD_MMXU1_TotW,
    NULL,
    NULL,
    0,
    -1,
    IEC61850_FC_MX,
    IEC61850_TIMESTAMP,
    0,
    NULL,
    0};

extern ReportControlBlock iedModel_InteropLD_LLN0_report0;

ReportControlBlock iedModel_InteropLD_LLN0_report0 = {&iedModel_InteropLD_LLN0, "urcb0101", "interop_urcb01", false, "dsInterop", 1, 17, 39, 0, 0, {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}, NULL};







IedModel iedModel = {
    "InteropIED",
    &iedModel_InteropLD,
    &iedModelds_InteropLD_LLN0_dsInterop,
    &iedModel_InteropLD_LLN0_report0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    initializeValues
};

static void
initializeValues()
{
}
