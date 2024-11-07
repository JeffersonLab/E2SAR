package org.jlab.hpdf;

/**
 * This class is a wrapper for EjfatURI present in the native cpp library. To avoid multiple calls to the native library
 * the raw details are stored in the library needed to create an instance of EjfatURI
 */
public class EjfatURI{

    enum Token{
        ADMIN,
        INSTANCE,
        SESSION
    }
    
    private Token token;
    private String uri;
    private boolean preferv6;

    EjfatURI(String uri, Token token, boolean preferv6){
        this.uri = uri;
        this.token = token;
        this.preferv6 = preferv6;
    }

    EjfatURI(String uri){
        this(uri, Token.ADMIN, false);
    }

    public String getUri(){
        return uri;
    }

    public String getToken(){
        return token.name();
    }

    public int getTokenInt(){
        return token.ordinal();
    }

    public boolean getPreferv6(){
        return preferv6;
    }

    public static void main(String args[]){
        EjfatURI ejfatURI = new EjfatURI("ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20");
    }
}