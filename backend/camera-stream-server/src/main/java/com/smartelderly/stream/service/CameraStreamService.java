package com.smartelderly.stream.service;

import java.time.Instant;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicLong;

import com.smartelderly.stream.model.CameraStatus;
import com.smartelderly.stream.model.JpegFrame;
import org.springframework.stereotype.Service;

@Service
public class CameraStreamService {

    private final ConcurrentHashMap<String, CameraState> cameras = new ConcurrentHashMap<>();
    private final CopyOnWriteArrayList<CameraFrameListener> listeners = new CopyOnWriteArrayList<>();

    public JpegFrame publishFrame(String cameraId, byte[] jpegBytes) {
        CameraState state = cameras.computeIfAbsent(cameraId, ignored -> new CameraState());
        JpegFrame frame = state.update(jpegBytes);
        for (CameraFrameListener listener : listeners) {
            listener.onFrame(cameraId, frame);
        }
        return frame;
    }

    public Optional<JpegFrame> getLatestFrame(String cameraId) {
        CameraState state = cameras.get(cameraId);
        return state == null ? Optional.empty() : Optional.ofNullable(state.latestFrame);
    }

    public JpegFrame waitForNextFrame(String cameraId, long lastSequence, long timeoutMs) throws InterruptedException {
        CameraState state = cameras.computeIfAbsent(cameraId, ignored -> new CameraState());
        return state.waitForNextFrame(lastSequence, timeoutMs);
    }

    public List<CameraStatus> getStatuses() {
        List<CameraStatus> statuses = new ArrayList<>();
        cameras.forEach((cameraId, state) -> {
            JpegFrame frame = state.latestFrame;
            statuses.add(new CameraStatus(
                    cameraId,
                    frame == null ? 0 : frame.sequence(),
                    frame == null ? 0 : frame.bytes().length,
                    frame == null ? null : frame.receivedAt(),
                    state.websocketViewers.get()
            ));
        });
        statuses.sort(Comparator.comparing(CameraStatus::cameraId));
        return statuses;
    }

    public void addFrameListener(CameraFrameListener listener) {
        listeners.add(listener);
    }

    public void removeFrameListener(CameraFrameListener listener) {
        listeners.remove(listener);
    }

    public void incrementWebsocketViewer(String cameraId) {
        cameras.computeIfAbsent(cameraId, ignored -> new CameraState()).websocketViewers.incrementAndGet();
    }

    public void decrementWebsocketViewer(String cameraId) {
        CameraState state = cameras.get(cameraId);
        if (state != null) {
            state.websocketViewers.updateAndGet(value -> Math.max(0, value - 1));
        }
    }

    private static final class CameraState {

        private final AtomicLong sequence = new AtomicLong();
        private final AtomicLong websocketViewers = new AtomicLong();
        private volatile JpegFrame latestFrame;

        synchronized JpegFrame update(byte[] jpegBytes) {
            JpegFrame frame = new JpegFrame(jpegBytes, sequence.incrementAndGet(), Instant.now());
            latestFrame = frame;
            notifyAll();
            return frame;
        }

        synchronized JpegFrame waitForNextFrame(long lastSequence, long timeoutMs) throws InterruptedException {
            long deadline = System.currentTimeMillis() + timeoutMs;
            while ((latestFrame == null || latestFrame.sequence() <= lastSequence) && timeoutMs > 0) {
                wait(timeoutMs);
                timeoutMs = deadline - System.currentTimeMillis();
            }
            return latestFrame;
        }
    }
}
