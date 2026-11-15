package com.smartelderly.stream.model;

import java.time.Instant;

public record CameraStatus(
        String cameraId,
        long frameSequence,
        int lastFrameBytes,
        Instant lastFrameAt,
        long websocketViewers
) {
}
