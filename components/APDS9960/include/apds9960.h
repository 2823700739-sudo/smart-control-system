#ifndef __APDS9960_APP_H_
#define __APDS9960_APP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *apds9960_handle_t;

apds9960_handle_t init_apds9960(void);

extern volatile int g_gesture_override;
extern volatile int g_gesture_brightness;

#ifdef __cplusplus
}
#endif

#endif
