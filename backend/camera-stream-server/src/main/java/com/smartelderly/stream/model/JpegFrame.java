package com.smartelderly.stream.model;

import java.time.Instant;

public record JpegFrame(
        byte[] bytes,
        long sequence,
        Instant receivedAt
) {
}
