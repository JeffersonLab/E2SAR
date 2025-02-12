package org.jlab.hpdf.unit;

import static org.junit.jupiter.api.Assertions.fail;

import java.net.InetAddress;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.Optional;

import org.jlab.hpdf.EjfatURI;
import org.jlab.hpdf.Segmenter;
import org.jlab.hpdf.Reassembler;
import org.jlab.hpdf.config.ReassemblerFlags;
import org.jlab.hpdf.config.SegmenterFlags;
import org.jlab.hpdf.exceptions.E2sarNativeException;
import org.jlab.hpdf.messages.SendStats;
import org.junit.jupiter.api.Test;
import org.jlab.hpdf.messages.LostEvent;
import org.jlab.hpdf.messages.ReassembledEvent;
import org.jlab.hpdf.messages.RecvStats;

public class E2sarReassemblerTest {

    static{
        System.loadLibrary("jnie2sar");
    }
    
    // this is a test that uses local host to send/receive fragments
    // it does NOT use control plane
    @Test
    void ReasTest1(){
        System.out.println("DPReasTest1: Test segmentation and reassembly on local host with no control plane (no segmentation)");
        
        // create URI for segmenter - since we will turn off CP only the data part of the query is used
        String segUriString = "ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1:10000";
        // create URI for reassembler - since we turn off CP, none of it is actually used
        String reasUriString = "ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1";

        EjfatURI segUri = null;
        EjfatURI reasUri = null;
        try{
            segUri = EjfatURI.createInstance(segUriString);
            reasUri = EjfatURI.createInstance(reasUriString);
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }

        // create segmenter with no control plane
        SegmenterFlags sflags = new SegmenterFlags();

        sflags.syncPeriodMs= 1000; // in ms
        sflags.syncPeriods = 5; // number of sync periods to use for sync
        sflags.useCP = false; // turn off CP

        int dataId = 0x0505;
        long eventSrcId = 0x11223344;

        Segmenter seg = new Segmenter(segUri, dataId, eventSrcId, sflags);

        // create reassembler with no control plane
        ReassemblerFlags rflags = new ReassemblerFlags();

        rflags.useCP = false; // turn off CP
        rflags.withLBHeader = true; // LB header will be attached since there is no LB

        InetAddress inetAddress = InetAddress.getLoopbackAddress();
        int listenPort = 10000;
        Reassembler reas = new Reassembler(reasUri, inetAddress, listenPort, 1, rflags);

        System.out.println("This reassembler has " + reas.getNumRecvThreads() + " receive threads and is listening on ports " + reas.getRecvPorts().get(0) + ":" +
            reas.getRecvPorts().get(1) + " using portRange " + reas.getPortRange());

        try {
            seg.openAndStart();
        } catch (E2sarNativeException e) {
            e.printStackTrace();
            fail("Error encountered opening sockets and starting segmenter threads");
        }

        try {
            reas.openAndStart();
        } catch (E2sarNativeException e) {
            e.printStackTrace();
            fail("Error encountered opening sockets and starting reassembler threads");
        }

        String eventString = "THIS IS A VERY LONG EVENT MESSAGE WE WANT TO SEND EVERY 1 SECONDS.";
        System.out.println("The event data is string '" + eventString + "' of length " + eventString.length());
        //
        // send one event message per 1 seconds that fits into a single frame
        //
        SendStats sendStats = seg.getSendStats();
        if(sendStats.eventDatagramErrCount != 0){
            System.out.println("Error encountered after opening send socket: " + sendStats.lastErrorNo);
        }

        try{
            for(int i=0;i<5;i++){
                byte[] bytes = eventString.getBytes(StandardCharsets.UTF_8);
                ByteBuffer buffer = ByteBuffer.allocateDirect(bytes.length);
                buffer.put(bytes);
                buffer.flip();

                seg.addToSendQueueDirect(buffer, 0, 0, 0);
                Thread.sleep(1000);
            }
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
        catch (InterruptedException e){

        }

        sendStats = seg.getSendStats();
        System.out.println("Sent " + sendStats.eventDatagramCount + " data frames");
        assert(sendStats.eventDatagramCount == 5);
        assert(sendStats.eventDatagramErrCount == 0);

        for(int i = 0; i < 5; i++){
            Optional<ReassembledEvent> event = reas.getEvent();
            if(!event.isPresent()){
                System.out.println("No message received, continuing");
            }
            else{
                ReassembledEvent reassembledEvent = event.get();
                ByteBuffer buffer = reassembledEvent.byteBuffer;
                byte[] byteArray = new byte[buffer.remaining()];
                buffer.get(byteArray);
                String receivedEvent = new String(byteArray, StandardCharsets.UTF_8);
                System.out.println("Received message: " + receivedEvent + " of length " + receivedEvent.length() + " with event number " + reassembledEvent.eventNum + 
                    " and data id " + reassembledEvent.dataId);
                reas.freeDirectBytebBuffer(reassembledEvent);
            }
        }

        RecvStats recvStats = reas.getStats();
        assert(recvStats.enqueueLoss == 0);
        assert(recvStats.eventSuccess == 5);
        assert(recvStats.lastErrorNo == 0);
        assert(recvStats.grpcErrCount == 0);
        assert(recvStats.lastE2sarError == 0);

        Optional<LostEvent> lostEvenOptional = reas.getLostEvent();
        if(lostEvenOptional.isPresent()){
            LostEvent lostEvent = lostEvenOptional.get();
            System.out.println("LOST EVENT " + lostEvent.eventNum + ":" + lostEvent.dataId);
        }
        else{
            System.out.println("NO EVENT LOSS");
        }

        segUri.free();
        reasUri.free();
        seg.free();
        reas.free();
    }

    @Test
    void ReasTest2(){
        System.out.println("DPReasTest2: Test segmentation and reassembly on local host with no control plane (basic segmentation)");
        
        // create URI for segmenter - since we will turn off CP only the data part of the query is used
        String segUriString = "ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1:10000";
        // create URI for reassembler - since we turn off CP, none of it is actually used
        String reasUriString = "ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1";

        EjfatURI segUri = null;
        EjfatURI reasUri = null;
        try{
            segUri = EjfatURI.createInstance(segUriString);
            reasUri = EjfatURI.createInstance(reasUriString);
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }

        // create segmenter with no control plane
        SegmenterFlags sflags = new SegmenterFlags();

        sflags.syncPeriodMs= 1000; // in ms
        sflags.syncPeriods = 5; // number of sync periods to use for sync
        sflags.useCP = false; // turn off CP
        sflags.mtu = 80; // make MTU ridiculously small to force SAR to work

        int dataId = 0x0505;
        long eventSrcId = 0x11223344;

        Segmenter seg = new Segmenter(segUri, dataId, eventSrcId, sflags);

        // create reassembler with no control plane
        ReassemblerFlags rflags = new ReassemblerFlags();

        rflags.useCP = false; // turn off CP
        rflags.withLBHeader = true; // LB header will be attached since there is no LB

        InetAddress inetAddress = InetAddress.getLoopbackAddress();
        int listenPort = 10000;
        Reassembler reas = new Reassembler(reasUri, inetAddress, listenPort, 1, rflags);

        System.out.println("This reassembler has " + reas.getNumRecvThreads() + " receive threads and is listening on ports " + reas.getRecvPorts().get(0) + ":" +
            reas.getRecvPorts().get(1) + " using portRange " + reas.getPortRange());

        try {
            seg.openAndStart();
        } catch (E2sarNativeException e) {
            e.printStackTrace();
            fail("Error encountered opening sockets and starting segmenter threads");
        }

        try {
            reas.openAndStart();
        } catch (E2sarNativeException e) {
            e.printStackTrace();
            fail("Error encountered opening sockets and starting reassembler threads");
        }

        String eventString = "THIS IS A VERY LONG EVENT MESSAGE WE WANT TO SEND EVERY 1 SECONDS.";
        System.out.println("The event data is string '" + eventString + "' of length " + eventString.length());
        //
        // send one event message per 1 seconds that fits into a single frame
        //
        SendStats sendStats = seg.getSendStats();
        if(sendStats.eventDatagramErrCount != 0){
            System.out.println("Error encountered after opening send socket: " + sendStats.lastErrorNo);
        }

        try{
            for(int i=0;i<5;i++){
                byte[] bytes = eventString.getBytes(StandardCharsets.UTF_8);
                ByteBuffer buffer = ByteBuffer.allocateDirect(bytes.length);
                buffer.put(bytes);
                buffer.flip();

                seg.addToSendQueueDirect(buffer, 0, 0, 0);
                Thread.sleep(1000);
            }
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
        catch (InterruptedException e){

        }

        sendStats = seg.getSendStats();
        System.out.println("Sent " + sendStats.eventDatagramCount + " data frames");
        assert(sendStats.eventDatagramCount == 25);
        assert(sendStats.eventDatagramErrCount == 0);

        for(int i = 0; i < 5; i++){
            Optional<ReassembledEvent> event = reas.getEvent();
            if(!event.isPresent()){
                System.out.println("No message received, continuing");
            }
            else{
                ReassembledEvent reassembledEvent = event.get();
                ByteBuffer buffer = reassembledEvent.byteBuffer;
                byte[] byteArray = new byte[buffer.remaining()];
                buffer.get(byteArray);
                String receivedEvent = new String(byteArray, StandardCharsets.UTF_8);
                System.out.println("Received message: " + receivedEvent + " of length " + receivedEvent.length() + " with event number " + reassembledEvent.eventNum + 
                    " and data id " + reassembledEvent.dataId);
                reas.freeDirectBytebBuffer(reassembledEvent);
            }
        }

        RecvStats recvStats = reas.getStats();
        assert(recvStats.enqueueLoss == 0);
        assert(recvStats.eventSuccess == 5);
        assert(recvStats.lastErrorNo == 0);
        assert(recvStats.grpcErrCount == 0);
        assert(recvStats.lastE2sarError == 0);

        Optional<LostEvent> lostEvenOptional = reas.getLostEvent();
        if(lostEvenOptional.isPresent()){
            LostEvent lostEvent = lostEvenOptional.get();
            System.out.println("LOST EVENT " + lostEvent.eventNum + ":" + lostEvent.dataId);
        }
        else{
            System.out.println("NO EVENT LOSS");
        }

        segUri.free();
        reasUri.free();
        seg.free();
        reas.free();
    }

    @Test
    void ReasTest3(){
        System.out.println("DPReasTest3: Test creationg of reassemblers with different parameters)");

        // create URI for reassembler - since we turn off CP, none of it is actually used
        String reasUriString = "ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1";

        EjfatURI reasUri = null;
        try{
            reasUri = EjfatURI.createInstance(reasUriString);
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }

        // create reassembler with no control plane
        ReassemblerFlags rflags = new ReassemblerFlags();

        rflags.useCP = false; // turn off CP
        rflags.withLBHeader = true; // LB header will be attached since there is no LB

        InetAddress inetAddress = InetAddress.getLoopbackAddress();
        int listenPort = 19522;

        // one thread
        Reassembler reas = new Reassembler(reasUri, inetAddress, listenPort, 1, rflags);
        System.out.println("This reassembler has " + reas.getNumRecvThreads() + " receive threads and is listening on ports " + reas.getRecvPorts().get(0) + ":" +
            reas.getRecvPorts().get(1) + " using portRange " + reas.getPortRange());
        assert(reas.getNumRecvThreads() == 1);
        assert(reas.getRecvPorts().get(0) == 19522);
        assert(reas.getRecvPorts().get(1) == 19522);
        assert(reas.getPortRange() == 0);
        reas.free();

        // 4 thread
        reas = new Reassembler(reasUri, inetAddress, listenPort, 4, rflags);
        System.out.println("This reassembler has " + reas.getNumRecvThreads() + " receive threads and is listening on ports " + reas.getRecvPorts().get(0) + ":" +
            reas.getRecvPorts().get(1) + " using portRange " + reas.getPortRange());
        assert(reas.getNumRecvThreads() == 4);
        assert(reas.getRecvPorts().get(0) == 19522);
        assert(reas.getRecvPorts().get(1) == 19525);
        assert(reas.getPortRange() == 2);
        reas.free();

        // 7 thread
        reas = new Reassembler(reasUri, inetAddress, listenPort, 7, rflags);
        System.out.println("This reassembler has " + reas.getNumRecvThreads() + " receive threads and is listening on ports " + reas.getRecvPorts().get(0) + ":" +
            reas.getRecvPorts().get(1) + " using portRange " + reas.getPortRange());
        assert(reas.getNumRecvThreads() == 7);
        assert(reas.getRecvPorts().get(0) == 19522);
        assert(reas.getRecvPorts().get(1) == 19529);
        assert(reas.getPortRange() == 3);
        reas.free();

        // 4 threads with portRange override
        rflags.portRange = 10;
        reas = new Reassembler(reasUri, inetAddress, listenPort, 4, rflags);
        System.out.println("This reassembler has " + reas.getNumRecvThreads() + " receive threads and is listening on ports " + reas.getRecvPorts().get(0) + ":" +
            reas.getRecvPorts().get(1) + " using portRange " + reas.getPortRange());
        assert(reas.getNumRecvThreads() == 4);
        assert(reas.getRecvPorts().get(0) == 19522);
        assert(reas.getRecvPorts().get(1) == 20545);
        assert(reas.getPortRange() == 10);
        reas.free();

        // 4 threads with low portRange override
        rflags.portRange = 1;
        reas = new Reassembler(reasUri, inetAddress, listenPort, 4, rflags);
        System.out.println("This reassembler has " + reas.getNumRecvThreads() + " receive threads and is listening on ports " + reas.getRecvPorts().get(0) + ":" +
            reas.getRecvPorts().get(1) + " using portRange " + reas.getPortRange());
        assert(reas.getNumRecvThreads() == 4);
        assert(reas.getRecvPorts().get(0) == 19522);
        assert(reas.getRecvPorts().get(1) == 19523);
        assert(reas.getPortRange() == 1);
        reas.free();
        
        reasUri.free();
    }
}
