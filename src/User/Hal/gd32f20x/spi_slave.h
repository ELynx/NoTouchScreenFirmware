#ifndef _SPI_SLAVE_H_
#define _SPI_SLAVE_H_

#include <stdbool.h>
#include <stdint.h>
#include "CircularQueue.h"

#ifdef __cplusplus
extern "C" {
#endif

void SPI_Slave(CIRCULAR_QUEUE *queue);
void SPI_SlaveDeinit(void);
bool SPI_SlaveGetData(uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif
