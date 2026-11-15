package com.smartelderly.stream.model;

import java.time.Instant;

public record HlsCameraStatus(
        String cameraId,
        boolean online,
        Instant lastFrameAt,
        long frameSequence,
        boolean hlsReady
) {
}
