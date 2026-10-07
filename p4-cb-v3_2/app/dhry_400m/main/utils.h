#ifndef __UTILS_H__
#define	__UTILS_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RAM_FUNC

#define PRINTF(...)  printf(__VA_ARGS__)

uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t t);

#ifdef __cplusplus
}
#endif

#endif
