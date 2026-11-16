#ifndef CAMERA_H
#define CAMERA_H

#include <esp_err.h>
#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

void camera_upload_task(void *arg);
esp_err_t init_camera(void);

#ifdef __cplusplus
}
#endif

#endif // CAMERA_H
