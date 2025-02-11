package org.jlab.hpdf.messages;

public class SendStats{
    public long eventDatagramCount;
    public long eventDatagramErrCount;
    public int lastErrorNo;

    public SendStats(long eventDatagramCount, long eventDatagramErrCount, int lastErrorNo){
        this.eventDatagramCount = eventDatagramCount;
        this.eventDatagramErrCount = eventDatagramErrCount;
        this.lastErrorNo = lastErrorNo;
    }
}