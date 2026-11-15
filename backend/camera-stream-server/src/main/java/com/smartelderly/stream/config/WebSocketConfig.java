package com.smartelderly.stream.config;

import com.smartelderly.stream.ws.CameraFrameWebSocketHandler;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

@Configuration
@EnableWebSocket
@EnableConfigurationProperties(StreamProperties.class)
public class WebSocketConfig implements WebSocketConfigurer {

    private final CameraFrameWebSocketHandler cameraFrameWebSocketHandler;

    public WebSocketConfig(CameraFrameWebSocketHandler cameraFrameWebSocketHandler) {
        this.cameraFrameWebSocketHandler = cameraFrameWebSocketHandler;
    }

    @Override
    public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
        registry.addHandler(cameraFrameWebSocketHandler, "/ws/cameras/{cameraId}")
                .setAllowedOriginPatterns("*");
    }
}
