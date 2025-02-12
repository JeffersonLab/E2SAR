package org.jlab.hpdf;

import org.jlab.hpdf.config.SegmenterFlags;
import org.jlab.hpdf.messages.SyncStats;
import org.jlab.hpdf.messages.SendStats;
import org.jlab.hpdf.exceptions.E2sarNativeException;

import java.nio.ByteBuffer;

public class Segmenter{

    static{
        System.loadLibrary("jnie2sar");
    }
    
    private long nativeSegmenter;

    private native long initSegmentor(EjfatURI dpUri, int dataId, long eventSrcId, SegmenterFlags sFlags);

    private native long initSegmentor(EjfatURI dpUri, int dataId, long eventSrcId, String iniFile) throws E2sarNativeException;

    private native long initSegmentor(EjfatURI dpUri, int dataId, long eventSrcId);

    public Segmenter(EjfatURI dpUri, int dataId, long eventSrcId, SegmenterFlags sFlags){
        nativeSegmenter = initSegmentor(dpUri, dataId, eventSrcId, sFlags);
    }

    public Segmenter(EjfatURI dpUri, int dataId, long eventSrcId, String iniFile) throws E2sarNativeException{
        nativeSegmenter = initSegmentor(dpUri, dataId, eventSrcId, iniFile);
    }

    public Segmenter(EjfatURI dpUri, int dataId, long eventSrcId){
        nativeSegmenter = initSegmentor(dpUri, dataId, eventSrcId);
    }

    private native void openAndStart(long nativeSegmenter) throws E2sarNativeException;
    public void openAndStart() throws E2sarNativeException{ openAndStart(this.nativeSegmenter);}

    private native void sendEventDirect(long nativeSegmenter, ByteBuffer buffer, int capacity, long eventNumber, int dataId, int entropy) throws E2sarNativeException;
    public void sendEventDirect(ByteBuffer buffer, long eventNumber, int dataId, int entropy) throws E2sarNativeException{
        if(!buffer.isDirect()){
            throw new E2sarNativeException("This method only supports direct ByteBuffers");
        }
        sendEventDirect(nativeSegmenter, buffer, buffer.capacity(), eventNumber, dataId, entropy);
    }

    private native void addToSendQueueDirect(long nativeSegmenter, ByteBuffer buffer, int capacity, long eventNumber, int dataId, int entropy) throws E2sarNativeException;
    public void addToSendQueueDirect(ByteBuffer buffer, long eventNumber, int dataId, int entropy) throws E2sarNativeException{
        if(!buffer.isDirect()){
            throw new E2sarNativeException("This method only supports direct ByteBuffers");
        }
        addToSendQueueDirect(nativeSegmenter, buffer, buffer.capacity(), eventNumber, dataId, entropy);
    }

    private native int getMTU(long nativeSegmenter);
    public int getMTU() { return getMTU(nativeSegmenter);}

    private native long getMaxPayloadLength(long nativeSegmenter);
    public long getMaxPayloadLength() { return getMaxPayloadLength(nativeSegmenter);}

    private native SyncStats getSyncStats(long nativeSegmenter);
    public SyncStats getSyncStats() {return getSyncStats(nativeSegmenter);}
    
    private native SendStats getSendStats(long nativeSegmenter);
    public SendStats getSendStats() { return getSendStats(nativeSegmenter);}

    private native void freeNativePointer(long nativeSegmenter);

    public void free(){
        if(nativeSegmenter != 0){
            freeNativePointer(nativeSegmenter);
            nativeSegmenter = 0;
        }
    }
}