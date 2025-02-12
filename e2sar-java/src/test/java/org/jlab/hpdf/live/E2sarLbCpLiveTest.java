package org.jlab.hpdf.live;

import static org.junit.jupiter.api.Assertions.fail;

import org.jlab.hpdf.EjfatURI;
import org.jlab.hpdf.LbManager;
import org.jlab.hpdf.exceptions.E2sarNativeException;
import org.junit.jupiter.api.Test;

import java.util.List;
import java.util.ArrayList;

/**
 * these tests necessarily depend on EJFAT_URI environment variable setting as information about live LBCP can't be baked into the test itself
 */
public class E2sarLbCpLiveTest {
    
    @Test
    void LBMTest1(){
        EjfatURI uri = null;
        LbManager lbman = null;
        try {
            uri = EjfatURI.getFromEnv("EJFAT_URI", EjfatURI.Token.ADMIN, false);
            lbman = new LbManager(uri, false, false);
        } catch (E2sarNativeException e) {
            e.printStackTrace();
            fail();
        }

        String duration = "01";
        String lbname = "mylb";
        List<String> senders = new ArrayList<>();
        senders.add("192.168.100.1");
        senders.add("192.168.100.2");

        try {
            int fpgaId = lbman.reserveLB(lbname, duration, senders);
            assert(lbman.getEjfatURI().getInstanceToken().length() > 0);
        } catch (E2sarNativeException e) {
            e.printStackTrace();
            fail();
        }
        
        assert(lbman.getEjfatURI().hasSyncAddr());
        assert(lbman.getEjfatURI().hasDataAddr());

        // call free - this will correctly use the admin token (even though instance token
        // is added by reserve call and updated URI inside with LB ID added to it
        try {
            lbman.freeLB();
        } catch (E2sarNativeException e) {
            e.printStackTrace();
            fail();
        }
        uri.free();
        lbman.free();
    }
}
