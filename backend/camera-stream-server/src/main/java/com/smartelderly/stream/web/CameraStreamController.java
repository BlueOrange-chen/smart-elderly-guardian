package com.smartelderly.stream.web;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.ByteArrayOutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import com.smartelderly.stream.config.StreamProperties;
import com.smartelderly.stream.model.CameraStatus;
import com.smartelderly.stream.model.HlsCameraStatus;
import com.smartelderly.stream.model.JpegFrame;
import com.smartelderly.stream.service.CameraStreamService;
import com.smartelderly.stream.service.HlsTranscodeService;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import org.springframework.http.CacheControl;
import org.springframework.http.HttpHeaders;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.util.StreamUtils;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestHeader;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.server.ResponseStatusException;

@RestController
@RequestMapping("/api")
public class CameraStreamController {

    private static final String BOUNDARY = "esp32frame";

    private final CameraStreamService cameraStreamService;
    private final HlsTranscodeService hlsTranscodeService;
    private final StreamProperties properties;

    public CameraStreamController(
            CameraStreamService cameraStreamService,
            HlsTranscodeService hlsTranscodeService,
            StreamProperties properties
    ) {
        this.cameraStreamService = cameraStreamService;
        this.hlsTranscodeService = hlsTranscodeService;
        this.properties = properties;
    }

    @PostMapping(
            path = "/cameras/{cameraId}/frame",
            consumes = MediaType.ALL_VALUE
    )
    public ResponseEntity<Void> uploadFrame(
            @PathVariable String cameraId,
            @RequestHeader(name = "X-Stream-Token", required = false) String token,
            @RequestParam(name = "token", required = false) String queryToken,
            HttpServletRequest request
    ) {
        requireCameraId(cameraId);
        requireIngestToken(token != null ? token : queryToken);
        byte[] body = readJpegBody(request);
        validateJpeg(body);

        JpegFrame frame = cameraStreamService.publishFrame(cameraId, body);
        return ResponseEntity.noContent()
                .header(HttpHeaders.CONNECTION, "close")
                .header("X-Frame-Sequence", String.valueOf(frame.sequence()))
                .header("X-Frame-Bytes", String.valueOf(frame.bytes().length))
                .build();
    }

    @GetMapping(path = "/cameras/{cameraId}/snapshot.jpg", produces = MediaType.IMAGE_JPEG_VALUE)
    public ResponseEntity<byte[]> snapshot(
            @PathVariable String cameraId,
            @RequestParam(name = "token", required = false) String token
    ) {
        requireCameraId(cameraId);
        requireViewerToken(token);
        JpegFrame frame = cameraStreamService.getLatestFrame(cameraId)
                .orElseThrow(() -> new ResponseStatusException(HttpStatus.NOT_FOUND, "No frame received for camera"));

        return ResponseEntity.ok()
                .cacheControl(CacheControl.noStore())
                .header(HttpHeaders.PRAGMA, "no-cache")
                .body(frame.bytes());
    }

    @GetMapping("/cameras/{cameraId}/mjpeg")
    public void mjpeg(
            @PathVariable String cameraId,
            @RequestParam(name = "token", required = false) String token,
            HttpServletResponse response
    ) throws IOException, InterruptedException {
        requireCameraId(cameraId);
        requireViewerToken(token);

        response.setStatus(HttpServletResponse.SC_OK);
        response.setContentType("multipart/x-mixed-replace; boundary=" + BOUNDARY);
        response.setHeader(HttpHeaders.CACHE_CONTROL, "no-store, no-cache, must-revalidate, max-age=0");
        response.setHeader(HttpHeaders.PRAGMA, "no-cache");

        OutputStream outputStream = response.getOutputStream();
        long lastSequence = 0;
        while (!Thread.currentThread().isInterrupted()) {
            JpegFrame frame = cameraStreamService.waitForNextFrame(
                    cameraId,
                    lastSequence,
                    properties.getMjpegWaitTimeoutMs()
            );
            if (frame == null || frame.sequence() <= lastSequence) {
                continue;
            }
            lastSequence = frame.sequence();
            writeMjpegPart(outputStream, frame);
            outputStream.flush();
        }
    }

    @GetMapping("/cameras")
    public List<CameraStatus> cameras(@RequestParam(name = "token", required = false) String token) {
        requireViewerToken(token);
        return cameraStreamService.getStatuses();
    }

    @GetMapping("/cameras/{cameraId}/status")
    public HlsCameraStatus cameraStatus(
            @PathVariable String cameraId,
            @RequestParam(name = "token", required = false) String token
    ) {
        requireCameraId(cameraId);
        requireViewerToken(token);
        return hlsTranscodeService.status(cameraId);
    }

    @GetMapping(value = "/live/{cameraId}.m3u8", produces = "application/vnd.apple.mpegurl")
    public ResponseEntity<String> hlsPlaylist(
            @PathVariable String cameraId,
            @RequestParam(name = "token", required = false) String token
    ) {
        requireCameraId(cameraId);
        requireViewerToken(token);
        String playlist = hlsTranscodeService.getPlaylist(cameraId, token);
        return ResponseEntity.ok()
                .header(HttpHeaders.CACHE_CONTROL, "no-cache, no-store, must-revalidate")
                .header(HttpHeaders.PRAGMA, "no-cache")
                .body(playlist);
    }

    @GetMapping(value = "/live/{cameraId}/{segmentName:.+}", produces = "video/mp2t")
    public ResponseEntity<byte[]> hlsSegment(
            @PathVariable String cameraId,
            @PathVariable String segmentName,
            @RequestParam(name = "token", required = false) String token,
            @RequestHeader(name = "Range", required = false) String rangeHeader
    ) throws IOException {
        requireCameraId(cameraId);
        requireViewerToken(token);
        Path segment = hlsTranscodeService.segmentPath(cameraId, segmentName);
        byte[] bytes = Files.readAllBytes(segment);
        if (rangeHeader != null && rangeHeader.startsWith("bytes=")) {
            return rangedSegment(bytes, rangeHeader);
        }
        return ResponseEntity.ok()
                .header(HttpHeaders.CACHE_CONTROL, "private, max-age=5")
                .header(HttpHeaders.ACCEPT_RANGES, "bytes")
                .contentType(MediaType.parseMediaType("video/mp2t"))
                .body(bytes);
    }

    private void writeMjpegPart(OutputStream outputStream, JpegFrame frame) throws IOException {
        String header = "--" + BOUNDARY + "\r\n"
                + "Content-Type: image/jpeg\r\n"
                + "Content-Length: " + frame.bytes().length + "\r\n"
                + "X-Frame-Sequence: " + frame.sequence() + "\r\n\r\n";
        outputStream.write(header.getBytes());
        StreamUtils.copy(frame.bytes(), outputStream);
        outputStream.write("\r\n".getBytes());
    }

    private ResponseEntity<byte[]> rangedSegment(byte[] bytes, String rangeHeader) {
        String range = rangeHeader.substring("bytes=".length());
        int dash = range.indexOf('-');
        if (dash < 0) {
            throw new ResponseStatusException(HttpStatus.REQUESTED_RANGE_NOT_SATISFIABLE, "Invalid range");
        }
        long start = parseRangeNumber(range.substring(0, dash), 0);
        long end = parseRangeNumber(range.substring(dash + 1), bytes.length - 1L);
        if (start < 0 || end < start || start >= bytes.length) {
            throw new ResponseStatusException(HttpStatus.REQUESTED_RANGE_NOT_SATISFIABLE, "Invalid range");
        }
        end = Math.min(end, bytes.length - 1L);
        int length = (int) (end - start + 1);
        byte[] body = new byte[length];
        System.arraycopy(bytes, (int) start, body, 0, length);
        return ResponseEntity.status(HttpStatus.PARTIAL_CONTENT)
                .header(HttpHeaders.CACHE_CONTROL, "private, max-age=5")
                .header(HttpHeaders.ACCEPT_RANGES, "bytes")
                .header(HttpHeaders.CONTENT_RANGE, "bytes " + start + "-" + end + "/" + bytes.length)
                .contentType(MediaType.parseMediaType("video/mp2t"))
                .body(body);
    }

    private long parseRangeNumber(String value, long defaultValue) {
        if (value == null || value.isBlank()) {
            return defaultValue;
        }
        try {
            return Long.parseLong(value);
        } catch (NumberFormatException ex) {
            throw new ResponseStatusException(HttpStatus.REQUESTED_RANGE_NOT_SATISFIABLE, "Invalid range", ex);
        }
    }

    private void requireCameraId(String cameraId) {
        if (!properties.isValidCameraId(cameraId)) {
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "Invalid camera id");
        }
    }

    private void requireIngestToken(String token) {
        if (!properties.getIngestToken().equals(token)) {
            throw new ResponseStatusException(HttpStatus.UNAUTHORIZED, "Invalid ingest token");
        }
    }

    private void requireViewerToken(String token) {
        if (!properties.getViewerToken().equals(token)) {
            throw new ResponseStatusException(HttpStatus.UNAUTHORIZED, "Invalid viewer token");
        }
    }

    private byte[] readJpegBody(HttpServletRequest request) {
        long contentLength = request.getContentLengthLong();
        if (contentLength == 0) {
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "Frame is empty");
        }
        if (contentLength > properties.getMaxFrameBytes()) {
            throw new ResponseStatusException(HttpStatus.PAYLOAD_TOO_LARGE, "Frame is too large");
        }

        int initialSize = contentLength > 0
                ? (int) Math.min(contentLength, properties.getMaxFrameBytes())
                : 8192;
        ByteArrayOutputStream outputStream = new ByteArrayOutputStream(initialSize);
        byte[] buffer = new byte[8192];
        int total = 0;
        try (InputStream inputStream = request.getInputStream()) {
            int read;
            while ((read = inputStream.read(buffer)) != -1) {
                total += read;
                if (total > properties.getMaxFrameBytes()) {
                    throw new ResponseStatusException(HttpStatus.PAYLOAD_TOO_LARGE, "Frame is too large");
                }
                outputStream.write(buffer, 0, read);
            }
        } catch (IOException ex) {
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "Client disconnected while uploading frame", ex);
        }
        return outputStream.toByteArray();
    }

    private void validateJpeg(byte[] body) {
        if (body == null || body.length < 4) {
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "Frame is empty");
        }
        if (body.length > properties.getMaxFrameBytes()) {
            throw new ResponseStatusException(HttpStatus.PAYLOAD_TOO_LARGE, "Frame is too large");
        }
        boolean startsWithJpegMarker = (body[0] & 0xFF) == 0xFF && (body[1] & 0xFF) == 0xD8;
        boolean endsWithJpegMarker = (body[body.length - 2] & 0xFF) == 0xFF && (body[body.length - 1] & 0xFF) == 0xD9;
        if (!startsWithJpegMarker || !endsWithJpegMarker) {
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "Frame is not a complete JPEG image");
        }
    }
}
