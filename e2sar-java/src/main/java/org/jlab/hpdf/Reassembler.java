package org.jlab.hpdf;

import java.net.InetAddress;
import java.nio.ByteBuffer;
import java.util.List;
import java.util.Optional;
import java.util.HashSet;

import org.jlab.hpdf.config.ReassemblerFlags;
import org.jlab.hpdf.exceptions.E2sarNativeException;
import org.jlab.hpdf.messages.LostEvent;
import org.jlab.hpdf.messages.ReassembledEvent;
import org.jlab.hpdf.messages.RecvStats;

public class Reassembler {
    
    static{
        System.loadLibrary("jnie2sar");
    }

    private long nativeReassembler;
    private HashSet<ByteBuffer> allocatedBuffers;

    private native long initReassembler(EjfatURI dpUri, InetAddress ipAddress, int startingPort,  List<Integer> cpuCoreList, ReassemblerFlags rFlags);

    private native long initReassembler(EjfatURI dpUri, InetAddress ipAddress, int startingPort,  List<Integer> cpuCoreList, String iniFile) throws E2sarNativeException;

    private native long initReassembler(EjfatURI dpUri, InetAddress ipAddress, int startingPort,  List<Integer> cpuCoreList);

    private native long initReassembler(EjfatURI dpUri, InetAddress ipAddress, int startingPort, long numReceiveThreads, ReassemblerFlags rFlags);

    private native long initReassembler(EjfatURI dpUri, InetAddress ipAddress, int startingPort, long numReceiveThreads, String iniFile) throws E2sarNativeException;

    private native long initReassembler(EjfatURI dpUri, InetAddress ipAddress, int startingPort, long numReceiveThreads);

    public Reassembler(EjfatURI dpUri, InetAddress ipAddress, int startingPort,  List<Integer> cpuCoreList, ReassemblerFlags rFlags){
        nativeReassembler = initReassembler(dpUri, ipAddress, startingPort, cpuCoreList, rFlags);
        allocatedBuffers = new HashSet<>();
    }

    public Reassembler(EjfatURI dpUri, InetAddress ipAddress, int startingPort,  List<Integer> cpuCoreList, String iniFile) throws E2sarNativeException{
        nativeReassembler = initReassembler(dpUri, ipAddress, startingPort, cpuCoreList, iniFile);
        allocatedBuffers = new HashSet<>();
    }

    public Reassembler(EjfatURI dpUri, InetAddress ipAddress, int startingPort,  List<Integer> cpuCoreList) throws E2sarNativeException{
        nativeReassembler = initReassembler(dpUri, ipAddress, startingPort, cpuCoreList);
        allocatedBuffers = new HashSet<>();
    }

    public Reassembler(EjfatURI dpUri, InetAddress ipAddress, int startingPort, int numReceiveThreads, ReassemblerFlags rFlags){
        nativeReassembler = initReassembler(dpUri, ipAddress, startingPort, numReceiveThreads, rFlags);
        allocatedBuffers = new HashSet<>();
    }

    public Reassembler(EjfatURI dpUri, InetAddress ipAddress, int startingPort, int numReceiveThreads, String iniFile) throws E2sarNativeException{
        nativeReassembler = initReassembler(dpUri, ipAddress, startingPort, numReceiveThreads, iniFile);
        allocatedBuffers = new HashSet<>();
    }

    public Reassembler(EjfatURI dpUri, InetAddress ipAddress, int startingPort, int numReceiveThreads){
        nativeReassembler = initReassembler(dpUri, ipAddress, startingPort, numReceiveThreads);
        allocatedBuffers = new HashSet<>();
    }

    private native void registerWorker(long nativeReassembler, String nodeName) throws E2sarNativeException;
    public void registerWorker(String nodeName) throws E2sarNativeException { registerWorker(nativeReassembler, nodeName);}

    private native void deregisterWorker(long nativeReassembler) throws E2sarNativeException;
    public void deregisterWorker() throws E2sarNativeException { deregisterWorker(nativeReassembler);}

    private native void openAndStart(long nativeReassembler) throws E2sarNativeException;
    public void openAndStart() throws E2sarNativeException { openAndStart(nativeReassembler);} 

    private native Optional<ReassembledEvent> getEvent(long nativeReassembler);
    public Optional<ReassembledEvent> getEvent(){ return getEvent(nativeReassembler);}

    private native Optional<ReassembledEvent> recvEvent(long nativeReassembler, long waitMs);
    public Optional<ReassembledEvent> recvEvent(long waitMs) { return recvEvent(nativeReassembler, waitMs);}

    private native RecvStats getStats(long nativeReassembler);
    public RecvStats getStats() { return getStats(nativeReassembler);}

    private native Optional<LostEvent> getLostEvent(long nativeReassembler);
    public Optional<LostEvent> getLostEvent() { return getLostEvent(nativeReassembler);}

    private native long getNumRecvThreads(long nativeReassembler);
    public long getNumRecvThreads() {return getNumRecvThreads(nativeReassembler);}

    private native List<Integer> getRecvPorts(long nativeReassembler);
    public List<Integer> getRecvPorts() { return getRecvPorts(nativeReassembler);}

    private native int getPortRange(long nativeReassembler);
    public int getPortRange() { return getPortRange(nativeReassembler);}

    private native void freeDirectBytebBuffer(long nativeReassembler, ByteBuffer buffer);
    public void freeDirectBytebBuffer(ReassembledEvent event){
        if(allocatedBuffers.contains(event.byteBuffer)){
            freeDirectBytebBuffer(nativeReassembler, event.byteBuffer);
            allocatedBuffers.remove(event.byteBuffer);
        }
    }

    private native void freeNativePointer(long nativeReassembler);
    public void free(){
        if(nativeReassembler != 0){
            for(ByteBuffer buffer : allocatedBuffers){
                freeDirectBytebBuffer(nativeReassembler, buffer);
            }
            freeNativePointer(nativeReassembler);
            nativeReassembler = 0;
        }
    }
}
