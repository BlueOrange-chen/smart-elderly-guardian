package com.smartelderly.stream.web;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.smartelderly.stream.config.StreamProperties;
import com.smartelderly.stream.model.ServoCommand;
import com.smartelderly.stream.model.ServoStatus;
import com.smartelderly.stream.service.ServoControlService;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RequestHeader;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.server.ResponseStatusException;

@RestController
@RequestMapping("/api/cameras/{cameraId}/servo")
public class ServoControlController {

    private static final int MIN_ANGLE = -90;
    private static final int MAX_ANGLE = 90;

    private final ServoControlService servoControlService;
    private final StreamProperties properties;

    public ServoControlController(ServoControlService servoControlService, StreamProperties properties) {
        this.servoControlService = servoControlService;
        this.properties = properties;
    }

    @PostMapping("/command")
    public ServoCommand setCommand(
            @PathVariable String cameraId,
            @RequestHeader(name = "X-Stream-Token", required = false) String headerToken,
            @RequestParam(name = "token", required = false) String token,
            @RequestBody ServoCommandRequest request
    ) {
        requireCameraId(cameraId);
        requireControlToken(headerToken != null ? headerToken : token);
        requireAngle(request.targetAngle());
        return servoControlService.setTargetAngle(cameraId, request.targetAngle());
    }

    @GetMapping("/command")
    public ServoCommand getCommand(
            @PathVariable String cameraId,
            @RequestHeader(name = "X-Stream-Token", required = false) String headerToken,
            @RequestParam(name = "token", required = false) String token
    ) {
        requireCameraId(cameraId);
        requireIngestToken(headerToken != null ? headerToken : token);
        return servoControlService.getTargetAngle(cameraId);
    }

    @PostMapping("/status")
    public ResponseEntity<Void> reportStatus(
            @PathVariable String cameraId,
            @RequestHeader(name = "X-Stream-Token", required = false) String headerToken,
            @RequestParam(name = "token", required = false) String token,
            @RequestBody ServoStatusRequest request
    ) {
        requireCameraId(cameraId);
        requireIngestToken(headerToken != null ? headerToken : token);
        requireAngle(request.angle());
        String reason = request.reason() == null || request.reason().isBlank() ? "status" : request.reason();
        if (reason.length() > 64) {
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "Reason is too long");
        }
        servoControlService.reportStatus(cameraId, request.angle(), reason);
        return ResponseEntity.noContent().build();
    }

    @GetMapping("/status")
    public ServoStatus getStatus(
            @PathVariable String cameraId,
            @RequestHeader(name = "X-Stream-Token", required = false) String headerToken,
            @RequestParam(name = "token", required = false) String token
    ) {
        requireCameraId(cameraId);
        requireControlToken(headerToken != null ? headerToken : token);
        return servoControlService.getStatus(cameraId)
                .orElseThrow(() -> new ResponseStatusException(HttpStatus.NOT_FOUND, "No servo status reported"));
    }

    private void requireCameraId(String cameraId) {
        if (!properties.isValidCameraId(cameraId)) {
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "Invalid camera id");
        }
    }

    private void requireAngle(int angle) {
        if (angle < MIN_ANGLE || angle > MAX_ANGLE) {
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "Angle must be between -90 and 90");
        }
    }

    private void requireControlToken(String token) {
        if (!properties.getControlToken().equals(token)) {
            throw new ResponseStatusException(HttpStatus.UNAUTHORIZED, "Invalid control token");
        }
    }

    private void requireIngestToken(String token) {
        if (!properties.getIngestToken().equals(token)) {
            throw new ResponseStatusException(HttpStatus.UNAUTHORIZED, "Invalid ingest token");
        }
    }

    public record ServoCommandRequest(@JsonProperty("target_angle") int targetAngle) {
    }

    public record ServoStatusRequest(int angle, String reason) {
    }
}
