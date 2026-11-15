package com.smartelderly.stream.ws;

import java.io.IOException;
import java.net.URI;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

import com.smartelderly.stream.config.StreamProperties;
import com.smartelderly.stream.model.JpegFrame;
import com.smartelderly.stream.service.CameraFrameListener;
import com.smartelderly.stream.service.CameraStreamService;
import jakarta.annotation.PreDestroy;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Component;
import org.springframework.web.server.ResponseStatusException;
import org.springframework.web.socket.BinaryMessage;
import org.springframework.web.socket.CloseStatus;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.BinaryWebSocketHandler;
import org.springframework.web.util.UriComponentsBuilder;

@Component
public class CameraFrameWebSocketHandler extends BinaryWebSocketHandler implements CameraFrameListener {

    private static final String CAMERA_ID_ATTRIBUTE = "cameraId";

    private final CameraStreamService cameraStreamService;
    private final StreamProperties properties;
    private final Map<String, Set<WebSocketSession>> sessionsByCamera = new ConcurrentHashMap<>();
    private final Map<String, SessionState> sessionStates = new ConcurrentHashMap<>();
    private final ExecutorService broadcastExecutor = Executors.newFixedThreadPool(4, daemonThreadFactory());

    public CameraFrameWebSocketHandler(CameraStreamService cameraStreamService, StreamProperties properties) {
        this.cameraStreamService = cameraStreamService;
        this.properties = properties;
        this.cameraStreamService.addFrameListener(this);
    }

    @Override
    public void afterConnectionEstablished(WebSocketSession session) throws Exception {
        String cameraId = extractCameraId(session);
        String token = extractQueryParam(session.getUri(), "token");
        if (!properties.getViewerToken().equals(token)) {
            session.close(CloseStatus.NOT_ACCEPTABLE.withReason("Invalid viewer token"));
            return;
        }
        if (!properties.isValidCameraId(cameraId)) {
            session.close(CloseStatus.BAD_DATA.withReason("Invalid camera id"));
            return;
        }

        session.getAttributes().put(CAMERA_ID_ATTRIBUTE, cameraId);
        sessionsByCamera.computeIfAbsent(cameraId, ignored -> ConcurrentHashMap.newKeySet()).add(session);
        sessionStates.put(session.getId(), new SessionState(session));
        cameraStreamService.incrementWebsocketViewer(cameraId);
        cameraStreamService.getLatestFrame(cameraId).ifPresent(frame -> offerFrame(session, frame));
    }

    @Override
    public void afterConnectionClosed(WebSocketSession session, CloseStatus status) {
        Object cameraIdValue = session.getAttributes().get(CAMERA_ID_ATTRIBUTE);
        if (cameraIdValue instanceof String cameraId) {
            Set<WebSocketSession> sessions = sessionsByCamera.get(cameraId);
            if (sessions != null) {
                sessions.remove(session);
            }
            sessionStates.remove(session.getId());
            cameraStreamService.decrementWebsocketViewer(cameraId);
        }
    }

    @Override
    public void onFrame(String cameraId, JpegFrame frame) {
        Set<WebSocketSession> sessions = sessionsByCamera.get(cameraId);
        if (sessions == null || sessions.isEmpty()) {
            return;
        }
        for (WebSocketSession session : sessions) {
            offerFrame(session, frame);
        }
    }

    @PreDestroy
    public void shutdown() {
        cameraStreamService.removeFrameListener(this);
        broadcastExecutor.shutdownNow();
    }

    private void offerFrame(WebSocketSession session, JpegFrame frame) {
        SessionState state = sessionStates.get(session.getId());
        if (state == null || !session.isOpen()) {
            return;
        }
        state.latestFrame.set(frame);
        if (state.sending.compareAndSet(false, true)) {
            broadcastExecutor.execute(() -> drainSession(state));
        }
    }

    private void drainSession(SessionState state) {
        try {
            while (state.session.isOpen()) {
                JpegFrame frame = state.latestFrame.getAndSet(null);
                if (frame == null) {
                    return;
                }
                sendFrame(state.session, frame);
            }
        } finally {
            state.sending.set(false);
            if (state.latestFrame.get() != null && state.session.isOpen() && state.sending.compareAndSet(false, true)) {
                broadcastExecutor.execute(() -> drainSession(state));
            }
        }
    }

    private void sendFrame(WebSocketSession session, JpegFrame frame) {
        if (!session.isOpen()) {
            return;
        }
        try {
            synchronized (session) {
                if (session.isOpen()) {
                    session.sendMessage(new BinaryMessage(frame.bytes()));
                }
            }
        } catch (IOException ex) {
            try {
                session.close(CloseStatus.SERVER_ERROR);
            } catch (IOException ignored) {
                // Session is already broken.
            }
            sessionStates.remove(session.getId());
        }
    }

    private String extractCameraId(WebSocketSession session) {
        String path = session.getUri() == null ? "" : session.getUri().getPath();
        String marker = "/ws/cameras/";
        int index = path.indexOf(marker);
        if (index < 0) {
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "Invalid websocket path");
        }
        return path.substring(index + marker.length());
    }

    private String extractQueryParam(URI uri, String name) {
        if (uri == null) {
            return null;
        }
        return UriComponentsBuilder.fromUri(uri).build().getQueryParams().getFirst(name);
    }

    private static ThreadFactory daemonThreadFactory() {
        return task -> {
            Thread thread = new Thread(task, "camera-frame-broadcast");
            thread.setDaemon(true);
            return thread;
        };
    }

    private static final class SessionState {

        private final WebSocketSession session;
        private final AtomicReference<JpegFrame> latestFrame = new AtomicReference<>();
        private final AtomicBoolean sending = new AtomicBoolean();

        private SessionState(WebSocketSession session) {
            this.session = session;
        }
    }
}
