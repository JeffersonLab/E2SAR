package org.jlab.hpdf.messages;

import java.nio.ByteBuffer;

public class ReassembledEvent {
    public ByteBuffer byteBuffer;
    public long eventNum;
    public int dataId;

    public ReassembledEvent(ByteBuffer byteBuffer, long eventNum, int dataId){
        this.byteBuffer = byteBuffer;
        this.eventNum = eventNum;
        this.dataId = dataId;
    }
}
