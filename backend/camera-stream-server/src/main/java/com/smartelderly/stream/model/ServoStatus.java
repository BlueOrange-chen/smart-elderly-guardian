package com.smartelderly.stream.model;

import com.fasterxml.jackson.annotation.JsonProperty;

import java.time.Instant;

public record ServoStatus(
        int angle,
        String reason,
        @JsonProperty("reported_at") Instant reportedAt
) {
}
