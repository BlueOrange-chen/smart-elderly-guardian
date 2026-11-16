idf_component_register(SRCS "rgb_led.c" "voice_audio.c" "voice_input.c" "voice_output.c" "voice_stream.c" "servo.c" "max30102.c" "dht11.c" "aliot_dm.c" "mqtt_aliot.c" "camera.c" "main.c" 
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_http_client esp_websocket_client esp_audio_codec esp_wifi esp_event esp_netif nvs_flash mqtt json esp32-camera)
