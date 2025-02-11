package org.jlab.hpdf;

import org.jlab.hpdf.EjfatURI;
import org.jlab.hpdf.LbManager;
import org.jlab.hpdf.config.SegmenterFlags;
import org.jlab.hpdf.exceptions.E2sarNativeException;
import org.jlab.hpdf.messages.SendStats;
import org.jlab.hpdf.messages.SyncStats;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.fail;

import java.util.List;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;

// these tests test the send thread and the sending of
// the event messages. They generaly require external capture
// to verify sent data. It doesn't require having UDPLBd running
// as it takes the sync address directly from the EJFAT_URI
public class E2sarSegmenterTest {

    static{
        System.loadLibrary("jnie2sar");
    }

    @Test
    void SegTest1(){
        System.out.println("DPSegTest1: test segmenter (and sync thread) by sending 5 events via event queue with default MTU");
        EjfatURI uri = null;
        try{
            uri = EjfatURI.createInstance("ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.254.1:12345&data=10.250.100.123");
            
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
        int dataId = 0x0505;
        long eventSrcId = 0x11223344;
        SegmenterFlags sFlags = new SegmenterFlags();
        sFlags.syncPeriodMs = 1000; // in ms
        sFlags.syncPeriods = 5; // number of sync periods to use for sync

        Segmenter segmenter = new Segmenter(uri, dataId, eventSrcId, sFlags);
        
        try{
            segmenter.openAndStart();
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail("Could not open send sockets");
        }
        String eventString = "THIS IS A VERY LONG EVENT MESSAGE WE WANT TO SEND EVERY 2 SECONDS.";
        System.out.println("The event data is string '" + eventString + "' of length " + eventString.length());

        SendStats sendStats = segmenter.getSendStats();
        if(sendStats.eventDatagramErrCount != 0){
            System.out.println("Error encountered after opening send socket: " + sendStats.lastErrorNo);
        }

        try{
            for(int i=0;i<5;i++){
                byte[] bytes = eventString.getBytes(StandardCharsets.UTF_8);
                ByteBuffer buffer = ByteBuffer.allocateDirect(bytes.length);
                buffer.put(bytes);
                buffer.flip();

                segmenter.addToSendQueueDirect(buffer, 0, 0, 0);
                Thread.sleep(2000);
            }
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
        catch (InterruptedException e){

        }
        SyncStats syncStats = segmenter.getSyncStats();
        sendStats = segmenter.getSendStats();

        if(syncStats.syncErrCount != 0){
            System.out.println("Error encountered sending sync frames:" + syncStats.lastErrorNo);
        }
        System.out.println("Sent " + syncStats.syncMsgCount + " sync frames");
        System.out.println("Sent " + sendStats.eventDatagramCount + " data frames");

        assert(syncStats.syncMsgCount >= 10);
        assert(syncStats.syncErrCount == 0);
        assert(sendStats.eventDatagramCount == 5);
        assert(sendStats.eventDatagramErrCount == 0);

        segmenter.free();
    }

    @Test
    void SegTest2(){
        
    }
}
