package com.smartelderly.stream.service;

import com.smartelderly.stream.model.JpegFrame;

public interface CameraFrameListener {

    void onFrame(String cameraId, JpegFrame frame);
}
