package org.jlab.hpdf;

import java.net.InetSocketAddress;

import org.jlab.hpdf.exceptions.E2sarNativeException;

/**
 * This class is a wrapper for EjfatURI present in the native cpp library. To avoid multiple calls to the native library
 * the raw details are stored in the library needed to create an instance of EjfatURI
 */
public class EjfatURI{
    static{
        System.loadLibrary("jnie2sar");
    }

    public enum Token{
        ADMIN,
        INSTANCE,
        SESSION
    }
    
    private long nativeEjfatURI;

    private EjfatURI(long nativeEjfatURI){
        this.nativeEjfatURI = nativeEjfatURI;
    }

    public static EjfatURI createInstance(String uri, Token token, boolean preferv6){
        return new EjfatURI(initEjfatUri(uri, token.ordinal(), preferv6));
    }

    public static EjfatURI createInstance(String uri){
        return createInstance(uri, Token.ADMIN, false);
    }

    public static EjfatURI getFromEnv(String envVariable, Token token, boolean preferv6) throws E2sarNativeException{
        String uri = System.getenv(envVariable);
        if(uri == null || uri.isEmpty()){
            throw new E2sarNativeException("Environment variable : " + envVariable + " is not set");
        }
        return createInstance(uri,token,preferv6);
    }

    public static EjfatURI getFromFile(String fileName, Token token, boolean preferv6) throws E2sarNativeException{
        return new EjfatURI(getUriFromFile(fileName, token.ordinal(), preferv6));
    }

    private static native long initEjfatUri(String envVariable, int token, boolean preferv6);

    private static native long getUriFromFile(String fileName, int t, boolean preferv6) throws E2sarNativeException;

     /** check if TLS should be used */
    public native boolean getUseTls();

    public native void setInstanceToken(String t);

    public native void setSessionToken(String t);

    public native String getSessionToken() throws E2sarNativeException;

    public native String getAdminToken() throws E2sarNativeException;

    public native void setLbName(String lbName);

    public native void setLbid(String lbid);

    public native void setSessionId(String sessionId);

    public native void setSyncAddr(InetSocketAddress socketAddress);

    public native void setDataAddr(InetSocketAddress socketAddress);

    public native String getLbName();

    public native String getLbid();

    public native String getSessionId();

    public native InetSocketAddress getCpAddr() throws E2sarNativeException;

    public native boolean hasDataAddrv4();

    public native boolean hasDataAddrv6();

    public native boolean hasDataAddr();

    public native boolean hasSyncAddr();

    public native InetSocketAddress getDataAddrv4() throws E2sarNativeException;

    public native InetSocketAddress getDataAddrv6() throws E2sarNativeException;

    public native InetSocketAddress getSyncAddr() throws E2sarNativeException;

    public String toString(Token t){
        return toString(t.ordinal());
    }

    private native String toString(int t);

}