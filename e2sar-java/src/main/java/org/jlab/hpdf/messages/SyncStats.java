package org.jlab.hpdf.messages;

public class SyncStats{
    public long syncMsgCount;
    public long syncErrCount;
    public int lastErrorNo;

    public SyncStats(long syncMsgCount, long syncErrCount, int lastErrorNo){
        this.syncMsgCount = syncMsgCount;
        this.syncErrCount = syncErrCount;
        this.lastErrorNo = lastErrorNo;
    }
}