package org.jlab.hpdf.messages;

import java.time.Instant;

public class WorkerStatus {
    public String name;
    public float fillPercent;
    public float controlSignal;
    public int slotsAssigned;
    public Instant lastUpdated;

    public WorkerStatus(String name, float fillPercent, float controlSignal, int slotsAssigned, Instant lastUpdated){
        this.name = name;
        this.fillPercent = fillPercent;
        this.controlSignal = controlSignal;
        this.slotsAssigned = slotsAssigned;
        this.lastUpdated = lastUpdated;
    }

    public WorkerStatus(String name, float fillPercent, float controlSignal, int slotsAssigned, String lastUpdated){
        this(name,fillPercent,controlSignal,slotsAssigned,Instant.parse(lastUpdated));
    }
}
