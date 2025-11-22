#pragma once

typedef struct Packet Packet;

void CBReportToCBMonitor_Update(void);

//may return NULL
Packet *CBReportToCBMonitor_GetPacket(int iCmd);

void CBReportToCBMonitor_ReportState(void);

void CBReportToCBMonitor_BuildEnded(enumCBResult eResult);

void CBReportToCBMonitor_BuildStarting(void);

void CBReportToCBMonitor_ReportSVNAndGimme(U32 iSVNRev, U32 iGimmeTime);

void CBReportToCBMonitor_ReportBuildComment(void);

