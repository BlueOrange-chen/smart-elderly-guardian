package com.smartelderly.stream.service;

import com.smartelderly.stream.model.ServoCommand;
import com.smartelderly.stream.model.ServoStatus;
import org.springframework.stereotype.Service;

import java.time.Instant;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

@Service
public class ServoControlService {

    private final ConcurrentHashMap<String, ServoCommand> commands = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<String, ServoStatus> statuses = new ConcurrentHashMap<>();
    private final AtomicLong commandSequence = new AtomicLong();

    public ServoCommand setTargetAngle(String cameraId, int targetAngle) {
        ServoCommand command = new ServoCommand(targetAngle, commandSequence.incrementAndGet(), Instant.now());
        commands.put(cameraId, command);
        return command;
    }

    public ServoCommand getTargetAngle(String cameraId) {
        return commands.computeIfAbsent(
                cameraId,
                ignored -> new ServoCommand(0, commandSequence.incrementAndGet(), Instant.now())
        );
    }

    public ServoStatus reportStatus(String cameraId, int angle, String reason) {
        ServoStatus status = new ServoStatus(angle, reason, Instant.now());
        statuses.put(cameraId, status);
        return status;
    }

    public Optional<ServoStatus> getStatus(String cameraId) {
        return Optional.ofNullable(statuses.get(cameraId));
    }
}
