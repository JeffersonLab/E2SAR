package org.jlab.hpdf.config;

public class ReassemblerFlags {
    public boolean useCP;
    public boolean useHostAddress;
    public int period_ms;
    public boolean validateCert;
    public float Ki, Kp, Kd, setPoint;
    public long epoch_ms;
    public int portRange; 
    public boolean withLBHeader;
    public int eventTimeout_ms;
    public int rcvSocketBufSize; 
    public float weight, min_factor, max_factor;

    public ReassemblerFlags(){
        useCP = true;
        useHostAddress = false;
        period_ms = 100;
        validateCert = true;
        Ki = 0.0f;
        Kd = 0.0f;
        Kp = 0.0f;
        setPoint = 0.0f;
        epoch_ms = 1000;
        portRange = -1;
        withLBHeader = false;
        eventTimeout_ms = 500;
        rcvSocketBufSize = 1024 * 1024 * 3;
        weight = 1.0f;
        min_factor = 0.5f;
        max_factor = 2.0f;    }
}
