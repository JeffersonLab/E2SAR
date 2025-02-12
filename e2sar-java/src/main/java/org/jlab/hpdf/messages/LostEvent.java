package org.jlab.hpdf.messages;

public class LostEvent {
    public long eventNum;
    public int dataId;

    public LostEvent(long eventNum, int dataId){
        this.eventNum = eventNum;
        this.dataId = dataId;
    }
}
