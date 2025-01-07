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

    public static EjfatURI getInternalInstance(long nativeEjfatURI){
        return new EjfatURI(nativeEjfatURI);
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
    public boolean getUseTls(){return this.getUseTls(nativeEjfatURI);}
    private native boolean getUseTls(long nativeEjfatURI);

    public void setInstanceToken(String t){setInstanceToken(nativeEjfatURI, t);}
    private native void setInstanceToken(long nativeEjfatURI, String t);

    public void setSessionToken(String t){setInstanceToken(nativeEjfatURI, t);}
    private native void setSessionToken(long nativeEjfatURI, String t);

    public String getSessionToken() throws E2sarNativeException{return getSessionToken(nativeEjfatURI);}
    private native String getSessionToken(long nativeEjfatURI) throws E2sarNativeException;

    public String getAdminToken() throws E2sarNativeException{return getAdminToken(nativeEjfatURI);}
    private native String getAdminToken(long nativeEjfatURI) throws E2sarNativeException;

    public void setLbName(String lbName){setLbName(nativeEjfatURI, lbName);}
    private native void setLbName(long nativeEjfatURI, String lbName);

    public void setLbid(String lbid){}
    private native void setLbid(long nativeEjfatURI, String lbid);

    public void setSessionId(String sessionId){setSessionId(nativeEjfatURI, sessionId);}
    private native void setSessionId(long nativeEjfatURI, String sessionId);

    public void setSyncAddr(InetSocketAddress socketAddress){setSyncAddr(nativeEjfatURI, socketAddress);}
    private native void setSyncAddr(long nativeEjfatURI, InetSocketAddress socketAddress);

    public void setDataAddr(InetSocketAddress socketAddress){setDataAddr(nativeEjfatURI, socketAddress);}
    private native void setDataAddr(long nativeEjfatURI, InetSocketAddress socketAddress);

    public String getLbName(){return getLbName(nativeEjfatURI);}
    private native String getLbName(long nativeEjfatURI);

    public String getLbid(){return getLbid();}
    private native String getLbid(long nativeEjfatURI);

    public String getSessionId(){return getSessionId(nativeEjfatURI);}
    private native String getSessionId(long nativeEjfatURI);

    public InetSocketAddress getCpAddr() throws E2sarNativeException{return getCpAddr(nativeEjfatURI);}
    private native InetSocketAddress getCpAddr(long nativeEjfatURI) throws E2sarNativeException;

    public boolean hasDataAddrv4(){return hasDataAddr(nativeEjfatURI);}
    private native boolean hasDataAddrv4(long nativeEjfatURI);

    public boolean hasDataAddrv6(){return hasDataAddr(nativeEjfatURI);}
    private native boolean hasDataAddrv6(long nativeEjfatURI);

    public boolean hasDataAddr(){return hasDataAddr(nativeEjfatURI);}
    private native boolean hasDataAddr(long nativeEjfatURI);

    public boolean hasSyncAddr(){return hasSyncAddr(nativeEjfatURI);}
    private native boolean hasSyncAddr(long nativeEjfatURI);

    public InetSocketAddress getDataAddrv4() throws E2sarNativeException{return getDataAddrv4(nativeEjfatURI);}
    private native InetSocketAddress getDataAddrv4(long nativeEjfatURI) throws E2sarNativeException;

    public InetSocketAddress getDataAddrv6() throws E2sarNativeException{return getDataAddrv6(nativeEjfatURI);}
    private native InetSocketAddress getDataAddrv6(long nativeEjfatURI) throws E2sarNativeException;

    public InetSocketAddress getSyncAddr() throws E2sarNativeException{return getSyncAddr(nativeEjfatURI);}
    private native InetSocketAddress getSyncAddr(long nativeEjfatURI) throws E2sarNativeException;

    public String toString(Token t){
        return toString(nativeEjfatURI, t.ordinal());
    }

    private native String toString(long nativeEjfatURI, int t);

    private native void freeNativePointer(long nativeEjfatURI);

    public void free(){
        if(nativeEjfatURI != 0){
            freeNativePointer(nativeEjfatURI);
            nativeEjfatURI = 0;
        }
    }



}