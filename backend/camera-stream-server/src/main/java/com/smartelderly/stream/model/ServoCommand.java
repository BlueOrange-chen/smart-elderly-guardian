package com.smartelderly.stream.model;

import com.fasterxml.jackson.annotation.JsonProperty;

import java.time.Instant;

public record ServoCommand(
        @JsonProperty("target_angle") int targetAngle,
        @JsonProperty("command_id") long commandId,
        @JsonProperty("updated_at") Instant updatedAt
) {
}
