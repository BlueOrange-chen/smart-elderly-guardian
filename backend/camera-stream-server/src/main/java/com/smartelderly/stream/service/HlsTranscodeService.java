package com.smartelderly.stream.service;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;

import com.smartelderly.stream.config.StreamProperties;
import com.smartelderly.stream.model.HlsCameraStatus;
import com.smartelderly.stream.model.JpegFrame;
import jakarta.annotation.PostConstruct;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Service;
import org.springframework.web.server.ResponseStatusException;

@Service
public class HlsTranscodeService implements CameraFrameListener {

    private static final Logger log = LoggerFactory.getLogger(HlsTranscodeService.class);
    private static final int COPY_BUFFER_BYTES = 8192;

    private final CameraStreamService cameraStreamService;
    private final StreamProperties properties;
    private final ConcurrentHashMap<String, HlsWorker> workers = new ConcurrentHashMap<>();

    public HlsTranscodeService(CameraStreamService cameraStreamService, StreamProperties properties) {
        this.cameraStreamService = cameraStreamService;
        this.properties = properties;
    }

    @PostConstruct
    void registerFrameListener() {
        cameraStreamService.addFrameListener(this);
    }

    @Override
    public void onFrame(String cameraId, JpegFrame frame) {
        if (!properties.isHlsAutoStart()) {
            return;
        }
        if (!properties.isValidCameraId(cameraId)) {
            return;
        }
        workers.compute(cameraId, (ignored, current) -> {
            if (current != null && current.isRunning()) {
                return current;
            }
            HlsWorker worker = new HlsWorker(cameraId, cameraDir(cameraId));
            try {
                worker.start();
                return worker;
            } catch (ResponseStatusException ex) {
                log.warn("Failed to warm up HLS for camera {}", cameraId, ex);
                return current;
            }
        });
    }

    public HlsCameraStatus status(String cameraId) {
        Optional<JpegFrame> latest = cameraStreamService.getLatestFrame(cameraId);
        boolean online = latest
                .map(frame -> Duration.between(frame.receivedAt(), Instant.now()).toMillis() <= properties.getHlsInactiveTimeoutMs())
                .orElse(false);
        return new HlsCameraStatus(
                cameraId,
                online,
                latest.map(JpegFrame::receivedAt).orElse(null),
                latest.map(JpegFrame::sequence).orElse(0L),
                isReady(cameraId)
        );
    }

    public String getPlaylist(String cameraId, String token) {
        HlsWorker worker = ensureWorker(cameraId);
        Path playlist = worker.playlistPath();
        waitForPlaylist(playlist);

        try {
            String content = Files.readString(playlist, StandardCharsets.UTF_8);
            return appendTokenToSegments(cameraId, content, token);
        } catch (IOException ex) {
            throw new ResponseStatusException(HttpStatus.SERVICE_UNAVAILABLE, "HLS playlist is not ready", ex);
        }
    }

    public Path segmentPath(String cameraId, String segmentName) {
        if (!segmentName.matches("seg_[0-9]+\\.ts")) {
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "Invalid HLS segment name");
        }
        ensureWorker(cameraId);
        Path path = cameraDir(cameraId).resolve(segmentName).normalize();
        if (!path.startsWith(cameraDir(cameraId))) {
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "Invalid HLS segment path");
        }
        if (!Files.isRegularFile(path)) {
            throw new ResponseStatusException(HttpStatus.NOT_FOUND, "HLS segment is not ready");
        }
        return path;
    }

    public boolean isReady(String cameraId) {
        return Files.isRegularFile(cameraDir(cameraId).resolve("index.m3u8"));
    }

    private HlsWorker ensureWorker(String cameraId) {
        return workers.compute(cameraId, (ignored, current) -> {
            if (current != null && current.isRunning()) {
                return current;
            }
            HlsWorker worker = new HlsWorker(cameraId, cameraDir(cameraId));
            worker.start();
            return worker;
        });
    }

    private void waitForPlaylist(Path playlist) {
        long deadline = System.currentTimeMillis() + properties.getHlsStartupTimeoutMs();
        while (System.currentTimeMillis() < deadline) {
            if (Files.isRegularFile(playlist)) {
                return;
            }
            try {
                Thread.sleep(200);
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
                throw new ResponseStatusException(HttpStatus.SERVICE_UNAVAILABLE, "Interrupted while waiting for HLS", ex);
            }
        }
        throw new ResponseStatusException(HttpStatus.SERVICE_UNAVAILABLE, "HLS playlist is not ready");
    }

    private Path cameraDir(String cameraId) {
        return Path.of(properties.getHlsRootDir()).resolve(cameraId).normalize();
    }

    private String appendTokenToSegments(String cameraId, String playlist, String token) {
        StringBuilder result = new StringBuilder(playlist.length() + 128);
        String encodedToken = token.replace("%", "%25").replace("&", "%26").replace("=", "%3D").replace("?", "%3F");
        String[] lines = playlist.split("\\R", -1);
        for (String line : lines) {
            if (line.endsWith(".ts")) {
                result.append(cameraId).append('/').append(line).append("?token=").append(encodedToken);
            } else {
                result.append(line);
            }
            result.append('\n');
        }
        return result.toString();
    }

    private List<String> ffmpegCommand(Path cameraDir) {
        int fps = Math.max(1, properties.getHlsFps());
        int segmentSeconds = Math.max(1, properties.getHlsSegmentSeconds());
        int listSize = Math.max(3, properties.getHlsListSize());
        int gop = Math.max(1, fps * segmentSeconds);
        String scale = "fps=" + fps + ",scale=" + Math.max(16, properties.getHlsWidth()) + ":-2";

        return List.of(
                properties.getFfmpegPath(),
                "-hide_banner",
                "-loglevel", "warning",
                "-fflags", "nobuffer",
                "-flags", "low_delay",
                "-use_wallclock_as_timestamps", "1",
                "-f", "mjpeg",
                "-r", String.valueOf(fps),
                "-i", "pipe:0",
                "-an",
                "-vf", scale,
                "-c:v", "libx264",
                "-preset", "veryfast",
                "-tune", "zerolatency",
                "-profile:v", "baseline",
                "-level", "3.1",
                "-pix_fmt", "yuv420p",
                "-b:v", properties.getHlsVideoBitrate(),
                "-maxrate", properties.getHlsMaxrate(),
                "-bufsize", properties.getHlsBufsize(),
                "-g", String.valueOf(gop),
                "-keyint_min", String.valueOf(gop),
                "-sc_threshold", "0",
                "-bf", "0",
                "-f", "hls",
                "-hls_time", String.valueOf(segmentSeconds),
                "-hls_list_size", String.valueOf(listSize),
                "-hls_delete_threshold", "2",
                "-hls_start_number_source", "epoch",
                "-hls_flags", "delete_segments+omit_endlist+independent_segments+program_date_time+temp_file",
                "-hls_segment_filename", cameraDir.resolve("seg_%06d.ts").toString(),
                cameraDir.resolve("index.m3u8").toString()
        );
    }

    private final class HlsWorker {
        private final String cameraId;
        private final Path cameraDir;
        private final AtomicBoolean running = new AtomicBoolean();
        private volatile Process process;

        private HlsWorker(String cameraId, Path cameraDir) {
            this.cameraId = cameraId;
            this.cameraDir = cameraDir;
        }

        private Path playlistPath() {
            return cameraDir.resolve("index.m3u8");
        }

        private boolean isRunning() {
            return running.get() && process != null && process.isAlive();
        }

        private void start() {
            try {
                Files.createDirectories(cameraDir);
                deleteOldHlsFiles();
                process = new ProcessBuilder(ffmpegCommand(cameraDir)).start();
                running.set(true);
                log.info("Started HLS FFmpeg worker for camera {}", cameraId);
                startDrainThread(process.getErrorStream());
                startFeederThread(process.getOutputStream());
                startWatcherThread();
            } catch (IOException ex) {
                running.set(false);
                throw new ResponseStatusException(HttpStatus.SERVICE_UNAVAILABLE, "FFmpeg is not available", ex);
            }
        }

        private void startDrainThread(InputStream errorStream) {
            Thread thread = new Thread(() -> drainQuietly(errorStream), "hls-ffmpeg-stderr-" + cameraId);
            thread.setDaemon(true);
            thread.start();
        }

        private void startFeederThread(OutputStream ffmpegInputStream) {
            Thread thread = new Thread(() -> feedFrames(ffmpegInputStream), "hls-ffmpeg-feeder-" + cameraId);
            thread.setDaemon(true);
            thread.start();
        }

        private void startWatcherThread() {
            Thread thread = new Thread(() -> {
                try {
                    process.waitFor();
                } catch (InterruptedException ex) {
                    Thread.currentThread().interrupt();
                } finally {
                    running.set(false);
                    workers.remove(cameraId, this);
                    log.info("HLS FFmpeg worker stopped for camera {}", cameraId);
                }
            }, "hls-ffmpeg-watch-" + cameraId);
            thread.setDaemon(true);
            thread.start();
        }

        private void feedFrames(OutputStream ffmpegInputStream) {
            int fps = Math.max(1, properties.getHlsFps());
            long frameIntervalMs = Math.max(20, 1000L / fps);
            JpegFrame lastFrame = null;
            long lastSequence = 0;

            try (OutputStream input = ffmpegInputStream) {
                while (running.get() && !Thread.currentThread().isInterrupted()) {
                    JpegFrame nextFrame = cameraStreamService.waitForNextFrame(cameraId, lastSequence, frameIntervalMs);
                    if (nextFrame != null && nextFrame.sequence() > lastSequence) {
                        lastFrame = nextFrame;
                        lastSequence = nextFrame.sequence();
                    }
                    if (lastFrame == null) {
                        continue;
                    }
                    if (Duration.between(lastFrame.receivedAt(), Instant.now()).toMillis() > properties.getHlsInactiveTimeoutMs()) {
                        log.info("Stopping HLS for camera {} because no frame arrived for {} ms", cameraId, properties.getHlsInactiveTimeoutMs());
                        stop();
                        return;
                    }
                    input.write(lastFrame.bytes());
                    input.flush();
                }
            } catch (IOException | InterruptedException ignored) {
                Thread.currentThread().interrupt();
            } finally {
                stop();
            }
        }

        private void stop() {
            running.set(false);
            if (process != null) {
                process.destroyForcibly();
            }
        }

        private void deleteOldHlsFiles() throws IOException {
            if (!Files.isDirectory(cameraDir)) {
                return;
            }
            try (var paths = Files.list(cameraDir)) {
                paths.filter(path -> {
                            String name = path.getFileName().toString();
                            return name.equals("index.m3u8") || name.endsWith(".ts") || name.endsWith(".tmp");
                        })
                        .forEach(path -> {
                            try {
                                Files.deleteIfExists(path);
                            } catch (IOException ignored) {
                                // A stale segment can be deleted by the next cleanup round.
                            }
                        });
            }
        }
    }

    private void drainQuietly(InputStream inputStream) {
        byte[] buffer = new byte[COPY_BUFFER_BYTES];
        try (InputStream input = inputStream) {
            while (input.read(buffer) != -1) {
                // Drain stderr to avoid blocking FFmpeg.
            }
        } catch (IOException ignored) {
            // Process is ending.
        }
    }
}
