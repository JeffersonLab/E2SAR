package org.jlab.hpdf;

public class E2sarUtil{

    static{
        System.loadLibrary("jnie2sar");
    }

    public static native String getE2sarVersion();

    public static void main(String args[]){
        System.out.println("E2sar version: " + getE2sarVersion());
    }
    
}