package org.jlab.hpdf.messages;

import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.UnknownHostException;

public class LBOverview {
    public String name;
    public String lbid;
    public InetSocketAddress syncAddressAndPort;
    public InetAddress dataIPv4;
    public InetAddress dataIPv6;
    public int fpgaLbid;
    public LBStatus status;

    public LBOverview(String name, String lbid, InetSocketAddress syncAddressAndPort, InetAddress dataIPv4, 
    InetAddress dataIPv6, int fpgaLbid, LBStatus status){
        this.name = name;
        this.lbid = lbid;
        this.syncAddressAndPort = syncAddressAndPort;
        this.dataIPv4 = dataIPv4;
        this.dataIPv6 = dataIPv6;
        this.fpgaLbid = fpgaLbid;
        this.status = status;
    }

    public LBOverview(String name, String lbid, String syncAddress, int syncPort, String dataIPv4, 
    String dataIPv6, int fpgaLbid, LBStatus status) throws UnknownHostException {
        this(name, lbid, InetSocketAddress.createUnresolved(syncAddress, syncPort),
        InetAddress.getByName(dataIPv4), InetAddress.getByName(dataIPv6),
        fpgaLbid, status);
    }
}
