package org.jlab.hpdf.config;

public class SegmenterFlags{
    public boolean dpV6; 
    public boolean zeroCopy;
    public boolean connectedSocket;
    public boolean useCP;
    public boolean zeroRate;
    public boolean usecAsEventNum;
    public int syncPeriodMs;
    public int syncPeriods;
    public int mtu;
    public long numSendSockets;
    public int sndSocketBufSize;

    public SegmenterFlags(){
        this.dpV6 = false;
        this.zeroCopy = false;
        this.connectedSocket = true;
        this.useCP = true;
        this.zeroRate = false;
        this.usecAsEventNum = true;
        this.syncPeriodMs = 1000;
        this.syncPeriods = 2;
        this.mtu = 1500;
        this.numSendSockets = 4;
        this.sndSocketBufSize = 1024 * 1024 * 3;
    }
}