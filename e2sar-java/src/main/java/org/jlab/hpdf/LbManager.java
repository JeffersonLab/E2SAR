package org.jlab.hpdf;

/**
 * This is a JNI wrapper class for LBManager in cpp. 
 */
public class LbManager {

    static{
        System.loadLibrary("jnie2sar");
    }
    /**
     *  stores the pointer of the native LBManager created.
     */
    private long nativeLbManager;
    
    /**
     * @param EjfatURI Java object which will be converted to CPP EjfatURI in native method
     * @param validateServer if false, skip server certificate validation (useful for self-signed testing)
     * @param useHostAddress even if hostname is provided, use host address as resolved by URI object (with preference for 
     * IPv4 by default or for IPv6 if explicitly requested)
     */
    public LbManager(EjfatURI uri, boolean validateServer, boolean useHostAddress){
        this(uri,validateServer,useHostAddress, new String[3], false);
    }

    /**
     * @param EjfatURI Java object which will be converted to CPP EjfatURI in native method
     * @param validateServer if false, skip server certificate validation (useful for self-signed testing)
     * @param useHostAddress even if hostname is provided, use host address as resolved by URI object (with preference for 
     * IPv4 by default or for IPv6 if explicitly requested)
     * @param sslCredentialOptions (Optional) containing server root certs, client key and client cert (in this order) 
     * use of SSL/TLS is governed by the URI scheme ('ejfat' vs 'ejfats').
     * @param sslCredentialOptionsfromFile (Optional) if true, assumes the contents of sslCredentialOptions are filepaths to the certificates
     */
    public LbManager(EjfatURI uri, boolean validateServer, boolean useHostAddress, String[] sslCredOpts, boolean sslCredOptsFromFile){
        nativeLbManager = initLbManager(uri,validateServer,useHostAddress,sslCredOpts,sslCredOptsFromFile);
    }

    private native long initLbManager(EjfatURI uri, boolean validateServer, boolean useHostAddress, String[] sslCredOpts, boolean sslCredentialOptionsfromFile);

    public native void reserveLB(String lbName, String duration, String[] senders);

    public native void reserveLB(String lbName, double seconds, String[] senders);



    public static void main(String args[]){
        EjfatURI uri = new EjfatURI("ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20");
        LbManager lbManager = new LbManager(uri, false, false);
    }
    
}
