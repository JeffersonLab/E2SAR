package org.jlab.hpdf.messages;

import java.time.Instant;
import java.util.List;

public class LBStatus{
    Instant timestamp;
    long currentEpoch;
    long currentPredictedEventNumber;
    List<WorkerStatus> workers;
    List<String> senderAddresses;
    Instant expiresAt;

    public LBStatus(Instant timestamp, Instant expiresAt, long currentEpoch, long currentPredictedEventNumber, List<WorkerStatus> workers, 
    List<String> senderAddresses){
        this.timestamp = timestamp;
        this.currentEpoch = currentEpoch;
        this.currentPredictedEventNumber = currentPredictedEventNumber;
        this.workers = workers;
        this.senderAddresses = senderAddresses;
    }

    public LBStatus(String timestamp, String expiresAt, long currentEpoch, long currentPredictedEventNumber, List<WorkerStatus> workers, 
    List<String> senderAddresses){
        this(Instant.parse(timestamp), Instant.parse(expiresAt), currentEpoch, currentPredictedEventNumber, workers, senderAddresses);
    }
}