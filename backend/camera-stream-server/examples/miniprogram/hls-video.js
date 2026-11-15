const config = require('../../config')

Page({
  data: {
    cameraHlsUrl: config.cameraHlsUrl || '',
    videoState: '等待播放'
  },

  onLoad() {
    if (!this.data.cameraHlsUrl) {
      wx.showModal({
        title: '尚未配置 HLS',
        content: '请在 config.js 中填写 cameraHlsUrl，例如 /api/live/room-1.m3u8?token=观看密钥。',
        showCancel: false
      })
    }
  },

  onVideoError(e) {
    console.error('HLS 播放失败', e.detail)
    this.setData({ videoState: '播放失败，请检查 m3u8 地址、合法域名和 HTTPS 配置' })
  },

  onVideoWaiting() {
    this.setData({ videoState: '正在缓冲' })
  },

  onVideoPlay() {
    this.setData({ videoState: '实时画面' })
  }
})
