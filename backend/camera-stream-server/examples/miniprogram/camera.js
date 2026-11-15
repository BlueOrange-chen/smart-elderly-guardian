const config = require('../../config')
const userService = require('../../services/mock-user-service')
const SERVO_MIN_ANGLE = -40
const SERVO_MAX_ANGLE = 40
const SERVO_STEP = 10
const DEFAULT_REFRESH_MS = 350

function buildSnapshotUrl() {
  const baseUrl = config.cameraSnapshotUrl || config.cameraStreamUrl || ''
  if (!baseUrl) return ''
  const separator = baseUrl.includes('?') ? '&' : '?'
  return `${baseUrl}${separator}_t=${Date.now()}`
}

Page({
  data: {
    frameSrc: '',
    streamState: '等待连接',
    isPlaying: false,
    controlling: false,
    lastAction: '摄像头已居中',
    currentAngle: 0,
    anglePercent: 50,
    canTurnLeft: true,
    canTurnRight: true,
    frameCount: 0,
    isRecording: false,
    isUploadingVoice: false,
    voiceStatus: '点击麦克风开始说话',
    elderlyMode: false
  },

  onLoad() {
    const user = userService.getCurrentUser()
    if (!user) return wx.reLaunch({ url: '/pages/auth/login/login' })
    this.refreshMs = config.cameraRefreshMs || DEFAULT_REFRESH_MS
    this.setData({ elderlyMode: user.role === 'elderly' })
    this.setupRecorder()
  },
  onReady() { this.pageReady = true; this.startStream() },
  onShow() { if (this.pageReady && !this.data.isPlaying) this.startStream() },
  onHide() { this.stopStream(false); this.stopRecordingIfNeeded(true) },
  onUnload() { this.destroyed = true; this.stopStream(false); this.stopRecordingIfNeeded(true) },

  startStream() {
    const snapshotUrl = buildSnapshotUrl()
    if (!snapshotUrl) {
      this.showConfigTip()
      return
    }
    this.setData({ streamState: '正在连接', isPlaying: true }, () => this.refreshSnapshot())
  },

  refreshSnapshot() {
    if (!this.data.isPlaying || this.destroyed) return
    const snapshotUrl = buildSnapshotUrl()
    if (!snapshotUrl) {
      this.stopStream()
      this.showConfigTip()
      return
    }
    this.setData({
      frameSrc: snapshotUrl,
      streamState: '实时画面',
      frameCount: this.data.frameCount + 1
    })
    this.refreshTimer = setTimeout(() => this.refreshSnapshot(), this.refreshMs)
  },

  stopStream(updateState = true) {
    if (this.refreshTimer) {
      clearTimeout(this.refreshTimer)
      this.refreshTimer = null
    }
    if (updateState) this.setData({ streamState: '画面已暂停', isPlaying: false })
    else this.setData({ isPlaying: false })
  },

  togglePlay() { if (this.data.isPlaying) this.stopStream(); else this.startStream() },
  reconnect() { this.stopStream(false); this.setData({ frameSrc: '', frameCount: 0 }, () => this.startStream()) },
  handleEmptyTap() { if (buildSnapshotUrl()) this.reconnect(); else this.showConfigTip() },
  onFrameError(e) {
    console.error('快照显示失败', e.detail)
    this.setData({ streamState: '画面加载失败，正在重试' })
  },
  showConfigTip() {
    wx.showModal({
      title: '尚未配置视频流',
      content: '请在 config.js 中填写 cameraSnapshotUrl，指向服务器 /api/cameras/room-1/snapshot.jpg。',
      showCancel: false
    })
  },

  controlCamera(e) {
    if (this.data.controlling) return
    const direction = e.currentTarget.dataset.direction
    let displayAngle = this.data.currentAngle
    if (direction === 'left') displayAngle -= SERVO_STEP
    if (direction === 'right') displayAngle += SERVO_STEP
    if (direction === 'center') displayAngle = 0
    displayAngle = Math.max(SERVO_MIN_ANGLE, Math.min(SERVO_MAX_ANGLE, displayAngle))
    if (displayAngle === this.data.currentAngle && direction !== 'center') {
      wx.showToast({ title: direction === 'left' ? '已到左侧最大角度' : '已到右侧最大角度', icon: 'none' })
      return
    }

    // 硬件正负方向与界面左右相反，因此发送绝对角度时取反。
    const hardwareTargetAngle = -displayAngle
    this.setData({ controlling: true })
    wx.request({
      url: config.cameraControlUrl,
      method: 'POST',
      header: { 'Content-Type': 'application/json', 'X-Stream-Token': config.cameraControlToken },
      data: { target_angle: hardwareTargetAngle },
      success: (res) => {
        if (res.statusCode >= 200 && res.statusCode < 300) {
          this.setData({
            currentAngle: displayAngle,
            anglePercent: (displayAngle - SERVO_MIN_ANGLE) / (SERVO_MAX_ANGLE - SERVO_MIN_ANGLE) * 100,
            canTurnLeft: displayAngle > SERVO_MIN_ANGLE,
            canTurnRight: displayAngle < SERVO_MAX_ANGLE,
            lastAction: displayAngle === 0 ? '摄像头已居中' : `当前水平角度 ${displayAngle > 0 ? '+' : ''}${displayAngle}°`
          })
        } else {
          console.error('舵机控制失败', res.statusCode, res.data)
          wx.showToast({ title: `控制失败：${res.statusCode}`, icon: 'none' })
        }
      },
      fail: (err) => { console.error('舵机请求失败', err); wx.showToast({ title: '无法连接摄像头', icon: 'none' }) },
      complete: () => this.setData({ controlling: false })
    })
  },

  setupRecorder() {
    this.recorder = wx.getRecorderManager()
    this.recorder.onStart(() => {
      this.recordStartedAt = Date.now()
      this.setData({ isRecording: true, voiceStatus: '正在录音，再次点击即可发送' })
    })
    this.recorder.onStop((res) => {
      const cancelled = this.cancelCurrentRecording
      this.cancelCurrentRecording = false
      this.setData({ isRecording: false })
      if (cancelled) {
        this.setData({ voiceStatus: '录音已取消' })
        return
      }
      if (Date.now() - this.recordStartedAt < 600) {
        this.setData({ voiceStatus: '说话时间太短，请重新录音' })
        return
      }
      this.uploadVoice(res.tempFilePath)
    })
    this.recorder.onError((err) => {
      console.error('录音失败', err)
      this.setData({ isRecording: false, voiceStatus: '无法录音，请检查麦克风权限' })
    })
  },

  toggleRecording() {
    if (this.data.isUploadingVoice) return
    if (this.data.isRecording) {
      this.recorder.stop()
      return
    }
    this.cancelCurrentRecording = false
    this.recorder.start({ duration: 60000, sampleRate: 16000, numberOfChannels: 1, encodeBitRate: 48000, format: 'mp3' })
  },

  stopRecordingIfNeeded(cancelled) {
    if (!this.data.isRecording || !this.recorder) return
    this.cancelCurrentRecording = cancelled
    this.recorder.stop()
  },

  uploadVoice(filePath) {
    if (!config.cameraAudioUrl) {
      this.setData({ voiceStatus: '尚未配置语音接收接口' })
      return
    }
    this.setData({ isUploadingVoice: true, voiceStatus: '正在发送到音响设备…' })
    wx.uploadFile({
      url: config.cameraAudioUrl,
      filePath,
      name: 'audio',
      header: { 'X-Stream-Token': config.cameraControlToken },
      formData: { format: 'mp3' },
      success: (res) => {
        if (res.statusCode >= 200 && res.statusCode < 300) this.setData({ voiceStatus: '语音已发送并等待播放' })
        else { console.error('语音发送失败', res.statusCode, res.data); this.setData({ voiceStatus: `语音发送失败：${res.statusCode}` }) }
      },
      fail: (err) => { console.error('语音上传失败', err); this.setData({ voiceStatus: '语音发送失败，请检查网络' }) },
      complete: () => this.setData({ isUploadingVoice: false })
    })
  }
})
