package org.jlab.hpdf;

import org.jlab.hpdf.EjfatURI;
import org.jlab.hpdf.LbManager;
import org.jlab.hpdf.exceptions.E2sarNativeException;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.fail;

import java.util.List;
import java.util.ArrayList;


/**
 * Maven surfire does not directtly propgate system_vars to tests so mvn -Djava.library.path='build/' test will not work
 * Use  mvn -DargLine="-Djava.library.path='build/'" test
*/
public class E2sarLbCpTest{
    final static String uriString = "ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20";

    static{
        System.loadLibrary("jnie2sar");
    }

    @Test
    void LBMTest1(){
        // test generating ssl options
        String[] sslOpts = {"root cert", "priv key", "cert"};

        try{
            EjfatURI ejfatUri = EjfatURI.createInstance(uriString);
            LbManager lbManager = new LbManager(ejfatUri, false, false, sslOpts, false);
            ejfatUri.free();
            lbManager.free();
        } 
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
    }

    @Test
    void LBMTest2(){
        try{
            EjfatURI ejfatUri = EjfatURI.createInstance(uriString);
            LbManager lbManager = new LbManager(ejfatUri, true, false);
            ejfatUri.free();
            lbManager.free();
        } 
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
    }
}