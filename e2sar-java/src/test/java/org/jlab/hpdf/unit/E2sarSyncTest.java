package org.jlab.hpdf.unit;

import static org.junit.jupiter.api.Assertions.fail;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;

import org.jlab.hpdf.EjfatURI;
import org.jlab.hpdf.Segmenter;
import org.jlab.hpdf.config.SegmenterFlags;
import org.jlab.hpdf.exceptions.E2sarNativeException;
import org.jlab.hpdf.messages.SendStats;
import org.jlab.hpdf.messages.SyncStats;
import org.junit.jupiter.api.Test;

/**
 * these tests test the sync thread and the sending ofthe sync messages. 
 * They generaly require external captureto verify sent data. 
 * It doesn't require having UDPLBd runningas it takes the sync address directly from the EJFAT_URI
 */
public class E2sarSyncTest{

    static{
        System.loadLibrary("jnie2sar");
    }

    @Test
    void SyncTest1(){
        System.out.println("DPSyncTest1: test sync thread sending 10 sync frames (once a second for 10 seconds)");
        EjfatURI uri = null;
        try{
            uri = EjfatURI.createInstance("ejfat://useless@192.168.100.1:9876/lb/1?sync=10.251.100.122:12345&data=10.250.100.123");
            System.out.println("Running data test for 10 seconds against sync" + uri.getSyncAddr().toString());
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
            System.out.println("Running sync test for 10 seconds " + uri.getSyncAddr().toString());
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail("Could not open send sockets");
        }

        try{
            Thread.sleep(10000);
        }
        catch(InterruptedException e){

        }
        
        SyncStats syncStats = segmenter.getSyncStats();
        System.out.println("Sent " + syncStats.syncMsgCount + " sync frames");
        assert(syncStats.syncMsgCount >= 10);
        assert(syncStats.syncErrCount == 0);

        segmenter.free();
        uri.free();
    }


}