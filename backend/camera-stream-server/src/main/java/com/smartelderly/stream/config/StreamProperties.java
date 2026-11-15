package com.smartelderly.stream.config;

import java.util.regex.Pattern;

import org.springframework.boot.context.properties.ConfigurationProperties;

@ConfigurationProperties(prefix = "stream")
public class StreamProperties {

    private String ingestToken = "change-me";
    private String viewerToken = "change-me";
    private String controlToken = "change-me";
    private int maxFrameBytes = 2 * 1024 * 1024;
    private String cameraIdPattern = "[A-Za-z0-9_-]{1,64}";
    private long mjpegWaitTimeoutMs = 5000;
    private String ffmpegPath = "ffmpeg";
    private boolean hlsAutoStart = true;
    private String hlsRootDir = System.getProperty("java.io.tmpdir") + "/esp32-camera-hls";
    private int hlsFps = 10;
    private int hlsSegmentSeconds = 1;
    private int hlsListSize = 5;
    private int hlsWidth = 640;
    private String hlsVideoBitrate = "650k";
    private String hlsMaxrate = "850k";
    private String hlsBufsize = "1300k";
    private long hlsStartupTimeoutMs = 8000;
    private long hlsInactiveTimeoutMs = 30000;

    public boolean isValidCameraId(String cameraId) {
        return cameraId != null && Pattern.matches(cameraIdPattern, cameraId);
    }

    public String getIngestToken() {
        return ingestToken;
    }

    public void setIngestToken(String ingestToken) {
        this.ingestToken = ingestToken;
    }

    public String getViewerToken() {
        return viewerToken;
    }

    public void setViewerToken(String viewerToken) {
        this.viewerToken = viewerToken;
    }

    public String getControlToken() {
        return controlToken;
    }

    public void setControlToken(String controlToken) {
        this.controlToken = controlToken;
    }

    public int getMaxFrameBytes() {
        return maxFrameBytes;
    }

    public void setMaxFrameBytes(int maxFrameBytes) {
        this.maxFrameBytes = maxFrameBytes;
    }

    public String getCameraIdPattern() {
        return cameraIdPattern;
    }

    public void setCameraIdPattern(String cameraIdPattern) {
        this.cameraIdPattern = cameraIdPattern;
    }

    public long getMjpegWaitTimeoutMs() {
        return mjpegWaitTimeoutMs;
    }

    public void setMjpegWaitTimeoutMs(long mjpegWaitTimeoutMs) {
        this.mjpegWaitTimeoutMs = mjpegWaitTimeoutMs;
    }

    public String getFfmpegPath() {
        return ffmpegPath;
    }

    public void setFfmpegPath(String ffmpegPath) {
        this.ffmpegPath = ffmpegPath;
    }

    public boolean isHlsAutoStart() {
        return hlsAutoStart;
    }

    public void setHlsAutoStart(boolean hlsAutoStart) {
        this.hlsAutoStart = hlsAutoStart;
    }

    public String getHlsRootDir() {
        return hlsRootDir;
    }

    public void setHlsRootDir(String hlsRootDir) {
        this.hlsRootDir = hlsRootDir;
    }

    public int getHlsFps() {
        return hlsFps;
    }

    public void setHlsFps(int hlsFps) {
        this.hlsFps = hlsFps;
    }

    public int getHlsSegmentSeconds() {
        return hlsSegmentSeconds;
    }

    public void setHlsSegmentSeconds(int hlsSegmentSeconds) {
        this.hlsSegmentSeconds = hlsSegmentSeconds;
    }

    public int getHlsListSize() {
        return hlsListSize;
    }

    public void setHlsListSize(int hlsListSize) {
        this.hlsListSize = hlsListSize;
    }

    public int getHlsWidth() {
        return hlsWidth;
    }

    public void setHlsWidth(int hlsWidth) {
        this.hlsWidth = hlsWidth;
    }

    public String getHlsVideoBitrate() {
        return hlsVideoBitrate;
    }

    public void setHlsVideoBitrate(String hlsVideoBitrate) {
        this.hlsVideoBitrate = hlsVideoBitrate;
    }

    public String getHlsMaxrate() {
        return hlsMaxrate;
    }

    public void setHlsMaxrate(String hlsMaxrate) {
        this.hlsMaxrate = hlsMaxrate;
    }

    public String getHlsBufsize() {
        return hlsBufsize;
    }

    public void setHlsBufsize(String hlsBufsize) {
        this.hlsBufsize = hlsBufsize;
    }

    public long getHlsStartupTimeoutMs() {
        return hlsStartupTimeoutMs;
    }

    public void setHlsStartupTimeoutMs(long hlsStartupTimeoutMs) {
        this.hlsStartupTimeoutMs = hlsStartupTimeoutMs;
    }

    public long getHlsInactiveTimeoutMs() {
        return hlsInactiveTimeoutMs;
    }

    public void setHlsInactiveTimeoutMs(long hlsInactiveTimeoutMs) {
        this.hlsInactiveTimeoutMs = hlsInactiveTimeoutMs;
    }
}
