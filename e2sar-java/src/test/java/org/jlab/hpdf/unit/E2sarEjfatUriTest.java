package org.jlab.hpdf.unit;

import org.jlab.hpdf.EjfatURI;
import org.jlab.hpdf.exceptions.E2sarNativeException;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.fail;

import java.net.Inet4Address;


/**
 * Maven surfire does not directtly propgate system_vars to tests so mvn -Djava.library.path='build/' test will not work
 * Use  mvn -DargLine="-Djava.library.path='build/'" test
*/
public class E2sarEjfatUriTest{
    final static String uri_string1 = "ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20";
    final static String uri_string2 = "ejfact://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20";

    final static String uri_string3 = "ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020";
    final static String uri_string4 = "ejfat://token@192.188.29.6:18020/lb/36";
    final static String uri_string4_1 = "ejfat://token@192.188.29.6:18020/";
    final static String uri_string4_2 = "ejfat://token@192.188.29.6:18020";
    final static String uri_string4_3 = "ejfat://token@192.188.29.6:18020/?sync=192.188.29.6:19020";
    final static String uri_string5 = "ejfat://token@192.188.29.6:18020/lb/36?data=192.188.29.20";
    final static String uri_string6 = "ejfat://192.188.29.6:18020/lb/36?sync=192.188.29.6:19020";

    // IPv6
    final static String uri_string7 = "ejfat://[2001:4860:0:2001::68]:18020/lb/36?data=[2001:4860:0:2021::68]&sync=[2001:4860:0:2031::68]:19020";

    // with TLS
    final static String uri_string8 = "ejfats://192.188.29.6:18020/lb/36?sync=192.188.29.6:19020";

    // with TLS and hostname
    final static String uri_string9 = "ejfats://ejfat-lb.es.net:18020/lb/36?sync=192.188.29.6:19020";

    // with session id
    final static String uri_string10 = "ejfats://ejfat-lb.es.net:18020/lb/36?sync=192.188.29.6:19020&sessionid=mysessionid";

    // with custom data port
    final static String uri_string11 = "ejfat://192.188.29.6:18020/lb/36?data=192.188.29.6:19020";

    static{
        System.loadLibrary("jnie2sar");
    }

    @Test
    void UriTest1_1(){
        try{
            EjfatURI ejfatUri = EjfatURI.createInstance(uri_string1);
            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
    }

    @Test
    void UriTest1_2(){
        try{
            EjfatURI ejfatUri = EjfatURI.createInstance(uri_string7);
            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
    }

    @Test
    void UriTest2_1(){
        try{
            EjfatURI ejfatUri = EjfatURI.createInstance(uri_string1);

            System.out.println(ejfatUri.toString(EjfatURI.Token.ADMIN) + " vs " + uri_string1);

            assertEquals("36", ejfatUri.getLbid());
            assertEquals("/192.188.29.6", ejfatUri.getCpAddr().getAddress().toString());
            assertEquals(18020, ejfatUri.getCpAddr().getPort());
            assertEquals("/192.188.29.20",ejfatUri.getDataAddrv4().getAddress().toString());
            assertEquals(19522, ejfatUri.getDataAddrv4().getPort());
            assertEquals("/192.188.29.6", ejfatUri.getSyncAddr().getAddress().toString());
            assertEquals(19020, ejfatUri.getSyncAddr().getPort());

            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
    }

    @Test
    void UriTest2_2(){
        EjfatURI ejfatUri;
        try{
            ejfatUri = EjfatURI.createInstance(uri_string3);

            System.out.println(ejfatUri.toString(EjfatURI.Token.ADMIN) + " vs " + uri_string3);

            assertEquals("token", ejfatUri.getAdminToken());
            assertEquals("36", ejfatUri.getLbid());
            assertEquals("/192.188.29.6", ejfatUri.getCpAddr().getAddress().toString());
            assertEquals(18020, ejfatUri.getCpAddr().getPort());
            assertEquals("/192.188.29.6", ejfatUri.getSyncAddr().getAddress().toString());
            assertEquals(19020, ejfatUri.getSyncAddr().getPort());

            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
        try{
            ejfatUri = EjfatURI.createInstance(uri_string3);
            ejfatUri.getDataAddrv4();
            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            return;
        }
        fail("DataAddrv4 should not be available");
    }
    
    @Test
    void UriTest2_3(){
        EjfatURI ejfatUri;
        try{
            ejfatUri = EjfatURI.createInstance(uri_string4);

            System.out.println(ejfatUri.toString(EjfatURI.Token.ADMIN) + " vs " + uri_string4);

            assertEquals("36", ejfatUri.getLbid());
            assertEquals("/192.188.29.6", ejfatUri.getCpAddr().getAddress().toString());
            assertEquals(18020, ejfatUri.getCpAddr().getPort());

            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }

        boolean dataAddrError = false;
        try{
            ejfatUri = EjfatURI.createInstance(uri_string4);
            ejfatUri.getDataAddrv4();
            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            dataAddrError = true;
        }

        try{
            ejfatUri = EjfatURI.createInstance(uri_string4);
            ejfatUri.getSyncAddr();
            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            return;
        }
        if(!dataAddrError)
            fail("DataAddrv4 should not be available");
        fail("SyncAddrv4 should not be available");
        
    }

    @Test
    void UriTest2_4(){
        EjfatURI ejfatUri;
        try{
            ejfatUri = EjfatURI.createInstance(uri_string5);

            System.out.println(ejfatUri.toString(EjfatURI.Token.ADMIN) + " vs " + uri_string5);

            assertEquals("token", ejfatUri.getAdminToken());
            assertEquals("36", ejfatUri.getLbid());
            assertEquals("/192.188.29.6", ejfatUri.getCpAddr().getAddress().toString());
            assertEquals(18020, ejfatUri.getCpAddr().getPort());
            assertEquals("/192.188.29.20", ejfatUri.getDataAddrv4().getAddress().toString());
            assertEquals(19522, ejfatUri.getDataAddrv4().getPort());

            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
        try{
            ejfatUri = EjfatURI.createInstance(uri_string5);
            ejfatUri.getSyncAddr();
            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            return;
        }
        fail("SyncAddrv4 should not be available");
    }    

    @Test
    void UriTest2_5(){
        EjfatURI ejfatUri;
        try{
            ejfatUri = EjfatURI.createInstance(uri_string6);

            System.out.println(ejfatUri.toString(EjfatURI.Token.ADMIN) + " vs " + uri_string6);

            assertEquals("36", ejfatUri.getLbid());
            assertEquals("/192.188.29.6", ejfatUri.getCpAddr().getAddress().toString());
            assertEquals(18020, ejfatUri.getCpAddr().getPort());
            assertEquals("/192.188.29.6", ejfatUri.getSyncAddr().getAddress().toString());
            assertEquals(19020, ejfatUri.getSyncAddr().getPort());

            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
        try{
            ejfatUri = EjfatURI.createInstance(uri_string6);
            ejfatUri.getAdminToken();
            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            return;
        }
        fail("Admin Token should not be available");
    }

    @Test
    void UriTest2_6(){
        EjfatURI ejfatUri;
        try{
            ejfatUri = EjfatURI.createInstance(uri_string4_1);

            System.out.println(ejfatUri.toString(EjfatURI.Token.ADMIN) + " vs " + uri_string4_1);

            assertEquals("", ejfatUri.getLbid());
            assertEquals("/192.188.29.6", ejfatUri.getCpAddr().getAddress().toString());
            assertEquals(18020, ejfatUri.getCpAddr().getPort());

            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }

        boolean dataAddrError = false;
        try{
            ejfatUri = EjfatURI.createInstance(uri_string4_1);
            ejfatUri.getDataAddrv4();
            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            dataAddrError = true;
        }

        try{
            ejfatUri = EjfatURI.createInstance(uri_string4_1);
            ejfatUri.getSyncAddr();
            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            return;
        }
        if(!dataAddrError)
            fail("DataAddrv4 should not be available");
        fail("SyncAddrv4 should not be available");
    }

    @Test
    void UriTest2_7(){
        EjfatURI ejfatUri;
        try{
            ejfatUri = EjfatURI.createInstance(uri_string4_2);

            System.out.println(ejfatUri.toString(EjfatURI.Token.ADMIN) + " vs " + uri_string4_2);

            assertEquals("token", ejfatUri.getAdminToken());
            assertEquals("", ejfatUri.getLbid());
            assertEquals("/192.188.29.6", ejfatUri.getCpAddr().getAddress().toString());
            assertEquals(18020, ejfatUri.getCpAddr().getPort());

            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
        try{
            ejfatUri = EjfatURI.createInstance(uri_string4_2);
            ejfatUri.getDataAddrv4();
            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            return;
        }
        fail("DataAddrv4 should not be available");
    }

    @Test
    void UriTest2_8(){
        EjfatURI ejfatUri;
        try{
            ejfatUri = EjfatURI.createInstance(uri_string4_3);

            System.out.println(ejfatUri.toString(EjfatURI.Token.ADMIN) + " vs " + uri_string4_3);

            assertEquals("token", ejfatUri.getAdminToken());
            assertEquals("", ejfatUri.getLbid());
            assertEquals("/192.188.29.6", ejfatUri.getCpAddr().getAddress().toString());
            assertEquals(18020, ejfatUri.getCpAddr().getPort());

            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
        try{
            ejfatUri = EjfatURI.createInstance(uri_string4_3);
            ejfatUri.getDataAddrv4();
            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            return;
        }
        fail("DataAddrv4 should not be available");
    }

    @Test
    void UriTest3(){
        // Env variable set in maven surefire config
        try{
            EjfatURI ejfatUri = EjfatURI.getFromEnv("EJFAT_URI", EjfatURI.Token.ADMIN, false);
            System.out.println(ejfatUri.toString(EjfatURI.Token.ADMIN));
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail("Could not get EjfatURI from envrionment variable");
        }
    }

    @Test
    void UriTest4(){
        // Using a different name should give an error
        try{
            EjfatURI ejfatUri = EjfatURI.getFromEnv("EJFAT_URI1", EjfatURI.Token.ADMIN, false);
        }
        catch(E2sarNativeException e){
            return;
        }
        fail("EJFAT_URI1 should not be set");
    }

    @Test
    void UriTest5(){
        try{
            EjfatURI ejfatUri = EjfatURI.createInstance(uri_string7);

            System.out.println(ejfatUri.toString(EjfatURI.Token.ADMIN) + " vs " + uri_string7);

            assertEquals("36", ejfatUri.getLbid());
            assert(!ejfatUri.hasDataAddrv4());
            assert(ejfatUri.hasDataAddrv6());
            assert(ejfatUri.hasSyncAddr());
            assertEquals("/2001:4860:0:2021:0:0:0:68", ejfatUri.getDataAddrv6().getAddress().toString());
            assertEquals("/2001:4860:0:2031:0:0:0:68", ejfatUri.getSyncAddr().getAddress().toString());
            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
    }

    @Test
    void UriTest6(){
         try{
            EjfatURI ejfatUri = EjfatURI.createInstance(uri_string8);

            System.out.println(ejfatUri.toString(EjfatURI.Token.ADMIN) + " vs " + uri_string8);

            assert(ejfatUri.getUseTls());
            
            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
    }

    @Test
    void UriTest7(){
         try{
            EjfatURI ejfatUri = EjfatURI.createInstance(uri_string9);

            System.out.println(ejfatUri.toString(EjfatURI.Token.ADMIN) + " vs " + uri_string9);

            assert(ejfatUri.getUseTls());
            assertEquals("ejfat-lb.es.net", ejfatUri.getCpHost().getHostName());
            assert(ejfatUri.getCpHost().getAddress() instanceof Inet4Address);
            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
    }

    @Test
    void UriTest8(){
        try{
            EjfatURI ejfatUri = EjfatURI.createInstance(uri_string9, EjfatURI.Token.ADMIN, true);

            System.out.println(ejfatUri.toString(EjfatURI.Token.ADMIN) + " vs " + uri_string9);

            assert(ejfatUri.getUseTls());
            assertEquals("ejfat-lb.es.net", ejfatUri.getCpHost().getHostName());
            assert(ejfatUri.getCpHost().getAddress() instanceof Inet4Address);
            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            System.out.println("Probably the host doesn't resolve to IPv6 from where you are running this test");
        }
    }

    @Test
    void UriTest9(){
         try{
            EjfatURI ejfatUri = EjfatURI.createInstance(uri_string10);

            System.out.println(ejfatUri.toString(EjfatURI.Token.ADMIN) + " vs " + uri_string10);
            
            assertEquals("mysessionid", ejfatUri.getSessionId());
            ejfatUri.free();
        }
        catch(E2sarNativeException e){
            e.printStackTrace();
            fail();
        }
    }
}