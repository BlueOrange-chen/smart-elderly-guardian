package com.smartelderly.stream;

import static org.hamcrest.Matchers.is;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.content;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.header;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.autoconfigure.web.servlet.AutoConfigureMockMvc;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.http.MediaType;
import org.springframework.test.web.servlet.MockMvc;

@SpringBootTest(properties = {
        "stream.ingest-token=test-ingest",
        "stream.viewer-token=test-viewer",
        "stream.control-token=test-control",
        "stream.hls-auto-start=false"
})
@AutoConfigureMockMvc
class CameraStreamControllerTest {

    private static final byte[] JPEG = new byte[] {
            (byte) 0xFF, (byte) 0xD8, 0x01, 0x02, 0x03, (byte) 0xFF, (byte) 0xD9
    };

    @Autowired
    private MockMvc mockMvc;

    @Test
    void acceptsJpegFrameAndServesSnapshot() throws Exception {
        mockMvc.perform(post("/api/cameras/room-1/frame")
                        .header("X-Stream-Token", "test-ingest")
                        .contentType(MediaType.IMAGE_JPEG)
                        .content(JPEG))
                .andExpect(status().isNoContent())
                .andExpect(header().string("Connection", "close"))
                .andExpect(header().string("X-Frame-Sequence", "1"))
                .andExpect(header().string("X-Frame-Bytes", String.valueOf(JPEG.length)));

        mockMvc.perform(get("/api/cameras/room-1/snapshot.jpg")
                        .param("token", "test-viewer"))
                .andExpect(status().isOk())
                .andExpect(content().contentType(MediaType.IMAGE_JPEG))
                .andExpect(content().bytes(JPEG));
    }

    @Test
    void acceptsIngestTokenInQueryString() throws Exception {
        mockMvc.perform(post("/api/cameras/room-query/frame")
                        .param("token", "test-ingest")
                        .contentType(MediaType.APPLICATION_OCTET_STREAM)
                        .content(JPEG))
                .andExpect(status().isNoContent())
                .andExpect(header().string("Connection", "close"));
    }

    @Test
    void rejectsInvalidToken() throws Exception {
        mockMvc.perform(post("/api/cameras/room-1/frame")
                        .header("X-Stream-Token", "wrong")
                        .contentType(MediaType.IMAGE_JPEG)
                        .content(JPEG))
                .andExpect(status().isUnauthorized());
    }

    @Test
    void forwardsServoCommandAndAcceptsHardwareStatus() throws Exception {
        mockMvc.perform(post("/api/cameras/room-1/servo/command")
                        .param("token", "test-control")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"target_angle\":45}"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.target_angle", is(45)))
                .andExpect(jsonPath("$.command_id").isNumber());

        mockMvc.perform(get("/api/cameras/room-1/servo/command")
                        .param("token", "test-ingest"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.target_angle", is(45)));

        mockMvc.perform(post("/api/cameras/room-1/servo/status")
                        .param("token", "test-ingest")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"angle\":45,\"reason\":\"moved\"}"))
                .andExpect(status().isNoContent());

        mockMvc.perform(get("/api/cameras/room-1/servo/status")
                        .param("token", "test-control"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.angle", is(45)))
                .andExpect(jsonPath("$.reason", is("moved")));
    }

    @Test
    void rejectsServoAngleOutsideSafeRange() throws Exception {
        mockMvc.perform(post("/api/cameras/room-1/servo/command")
                        .param("token", "test-control")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"target_angle\":91}"))
                .andExpect(status().isBadRequest());
    }
}
