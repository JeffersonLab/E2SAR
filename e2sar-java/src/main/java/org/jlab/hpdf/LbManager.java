package org.jlab.hpdf;

import java.util.List;
import java.time.Instant;

import org.apache.commons.cli.Options;
import org.jlab.hpdf.exceptions.E2sarNativeException;
import org.jlab.hpdf.messages.LBOverview;
import org.jlab.hpdf.messages.LBStatus;
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
    private EjfatURI uri;
    
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

    /**
     * Native method to initialize native e2sar::LBManager
     * @param EjfatURI Java object which will be converted to CPP EjfatURI in native method
     * @param validateServer if false, skip server certificate validation (useful for self-signed testing)
     * @param useHostAddress even if hostname is provided, use host address as resolved by URI object (with preference for 
     * IPv4 by default or for IPv6 if explicitly requested)
     * @param sslCredentialOptions (Optional) containing server root certs, client key and client cert (in this order) 
     * use of SSL/TLS is governed by the URI scheme ('ejfat' vs 'ejfats').
     * @param sslCredentialOptionsfromFile (Optional) if true, assumes the contents of sslCredentialOptions are filepaths to the certificates
     * 
     * @return pointer to instance of e2sar::LBManager
     */
    private native long initLbManager(EjfatURI uri, boolean validateServer, boolean useHostAddress, String[] sslCredOpts, boolean sslCredentialOptionsfromFile);

    /**
     * Native method to reserve a new load balancer with this name until specified time. It sets the intstance
     * token on the internal URI object.
     *
     * @param lbName LB name internal to you
     * @param duration for how long it is needed as String. Internally it is converted to boost::posix_time::time_duration.
     * @param senders list of sender IP addresses
     *
     * @return - FPGA LB ID, for use in correlating logs/metrics
     */
    public native int reserveLB(long nativeLbManager, String lbName, String duration, List<String> senders) throws E2sarNativeException;

    public int reserveLB(String lbName, String duration, List<String> senders) throws E2sarNativeException{return reserveLB(nativeLbManager, lbName, duration, senders);}

    /**
     * Native method to reserve a new load balancer with this name of duration in seconds
     *
     * @param lbName LB name internal to you
     * @param seconds for how long it is needed in seconds
     * @param senders list of sender IP addresses
     *
     * @return - FPGA LB ID, for use in correlating logs/metrics
     */
    public native int reserveLB(long nativeLbManager, String lbName, double seconds, List<String> senders)throws E2sarNativeException;
    public int reserveLB(String lbName, double seconds, List<String> senders)throws E2sarNativeException {return reserveLB(nativeLbManager, lbName, seconds, senders);}

    /**
     * Native method to Get load balancer info - it updates the info inside the EjfatURI object just like reserveLB.
     * Uses admin token of the internal URI object. Note that unlike reserve it does NOT set
     * the instance token - it is not available.
     *
     * @param lbid - externally provided lb id, in this case the URI only needs to contain
     * the cp address and admin token and it will be updated to contain dataplane and sync addresses.
     * 
     */
    public native void getLB(long nativeLbManager, String lbid) throws E2sarNativeException;
    public void getLB(String lbid) throws E2sarNativeException{getLB(nativeLbManager, lbid);}

    /**
     * Get load balancer info using lb id in the URI object
     * 
     */
    public native void getLB(long nativeLbManager) throws E2sarNativeException;
    public void getLB() throws E2sarNativeException{getLB(nativeLbManager);}

    /**
     * Native method to get load balancer status including list of workers, sender IP addresses.
     * 
     * @return LBStatus message 
     * */ 
    public native LBStatus getStatus(long nativeLbManager) throws E2sarNativeException;
    public LBStatus getStatus() throws E2sarNativeException {return getStatus(nativeLbManager);}

    /**
     * Get load balancer status including list of workers, sender IP addresses.
     *
     * @param lbid - externally provided lbid, in this case the URI only needs to contain
     * cp address and admin token
     * 
     * @return - LBStatus message
     */
    public native LBStatus getStatus(long nativeLbManager, String lbid) throws E2sarNativeException;
    public LBStatus getStatus(String lbid) throws E2sarNativeException {return getStatus(nativeLbManager, lbid);}

    public native List<LBOverview> getOverview(long nativeLbManager) throws E2sarNativeException;
    public List<LBOverview> getOverview() throws E2sarNativeException {return getOverview(nativeLbManager);}

    public native void addSenders(long nativeLbManager, List<String> senders) throws E2sarNativeException;
    public void addSenders(List<String> senders) throws E2sarNativeException {addSenders(nativeLbManager, senders);}

    public native void removeSenders(long nativeLbManager, List<String> senders) throws E2sarNativeException;
    public void removeSenders(List<String> senders) throws E2sarNativeException {removeSenders(nativeLbManager, senders);}

    public native void freeLB(long nativeLbManager, String lbid) throws E2sarNativeException;
    public void freeLB(String lbid) throws E2sarNativeException {freeLB(nativeLbManager, lbid);}

    public native void freeLB(long nativeLbManager) throws E2sarNativeException;
    public void freeLB() throws E2sarNativeException {freeLB(nativeLbManager);}
    
    /**
     * Register a workernode/backend with an allocated loadbalancer. Note that this call uses
     * instance token. It sets session token and session id on the internal
     * URI object. Note that a new worker must send state immediately (within 10s)
     * or be automatically deregistered.
     *
     * @param node_name - name of the node (can be FQDN)
     * @param nodeIPandPort - a pair of ip::address and u_int16_t starting UDP port on which it listens
     * @param weight - weight given to this node in terms of processing power
     * @param source_count - how many sources we can listen to (gets converted to port range [0,14])
     * @param min_factor - multiplied with the number of slots that would be assigned evenly to determine min number of slots
     * for example, 4 nodes with a minFactor of 0.5 = (512 slots / 4) * 0.5 = min 64 slots
     * @param max_factor - multiplied with the number of slots that would be assigned evenly to determine max number of slots
     * for example, 4 nodes with a maxFactor of 2 = (512 slots / 4) * 2 = max 256 slots set to 0 to specify no maximum
     */
    public native void registerWorker(long nativeLbManager, String nodeName, String nodeIP, int port, float weight, int source_count, float min_factor, float max_factor) throws E2sarNativeException;
    public void registerWorker(String nodeName, String nodeIP, int port, float weight, int source_count, float min_factor, float max_factor) throws E2sarNativeException{
        registerWorker(nativeLbManager, nodeName, nodeIP, port, weight, source_count, min_factor, max_factor);
    }

    public native void deregisteWorker(long nativeLbManager) throws E2sarNativeException;
    public void deregisteWorker() throws E2sarNativeException {deregisteWorker(nativeLbManager);}

    public native void sendState(long nativeLbManager, float fill_percent, float control_signal, boolean isReady) throws E2sarNativeException;
    public void sendState(float fill_percent, float control_signal, boolean isReady) throws E2sarNativeException {sendState(nativeLbManager, fill_percent, control_signal, isReady);}

    public native void sendState(long nativeLbManager, float fill_percent, float control_signal, boolean isReady, Instant timestamp) throws E2sarNativeException;
    public void sendState(float fill_percent, float control_signal, boolean isReady, Instant timestamp) throws E2sarNativeException {sendState(nativeLbManager, fill_percent, control_signal, isReady, timestamp);}

    /**
     * Get the version of the load balancer (the commit string)
     *
     * @return the result with commit string, build tag and compatTag in
     */
    public native List<String> version(long nativeLbManager) throws E2sarNativeException;
    public List<String> version() throws E2sarNativeException {return version(nativeLbManager);}

    public native String getAddrString(long nativeLbManager);
    public String getAddrString(){return getAddrString(nativeLbManager);}

    public native long getInternalUri(long nativeLbManager);
    public long getInternalUri(){return getInternalUri(nativeLbManager);}

    public EjfatURI getEjfatURI(){
        if(uri == null){
            long internalUri = getInternalUri(nativeLbManager);
            uri = EjfatURI.getInternalInstance(internalUri);
        }
        return uri;
    }
    
    
}
