#include "spi_slave.h"
#include "spi.h"
#include "GPIO_Init.h"
#include "variants.h"
#include "CircularQueue.h"
#include <stddef.h>

#if defined(ST7920_SPI)
// TODO:
// now support SPI2 and PB12 CS only
// more compatibility changes are needed

#if ST7920_SPI == _SPI1
  #define ST7920_SPI_NUM          SPI0
  #define ST7920_SPI_RCU          RCU_SPI0
  #define ST7920_SPI_RST          RCU_SPI0RST
  #define ST7920_SPI_IRQ          SPI0_IRQn
  #define ST7920_SPI_IRQHandler   SPI0_IRQHandler
#elif ST7920_SPI == _SPI2
  #define ST7920_SPI_NUM          SPI1
  #define ST7920_SPI_RCU          RCU_SPI1
  #define ST7920_SPI_RST          RCU_SPI1RST
  #define ST7920_SPI_IRQ          SPI1_IRQn
  #define ST7920_SPI_IRQHandler   SPI1_IRQHandler
#elif ST7920_SPI == _SPI3
  #define ST7920_SPI_NUM          SPI2
  #define ST7920_SPI_RCU          RCU_SPI2
  #define ST7920_SPI_RST          RCU_SPI2RST
  #define ST7920_SPI_IRQ          SPI2_IRQn
  #define ST7920_SPI_IRQHandler   SPI2_IRQHandler
#endif

volatile CIRCULAR_QUEUE *spi_queue = NULL;
volatile uint32_t ui32SpiActivated;

static inline void SPI_Enable(uint8_t cpol, uint8_t cpha)
{
  ++ui32SpiActivated;
  SPI_CTL0(ST7920_SPI_NUM) = (0<<15)        // 0:2-line 1: 1-line
                           | (0<<14)        // in bidirectional mode 0:read only 1: read/write
                           | (0<<13)        // 0:disable CRC 1:enable CRC
                           | (0<<12)        // 0:Data phase (no CRC phase) 1:Next transfer is CRC (CRC phase)
                           | (0<<11)        // 0:8-bit data frame 1:16-bit data frame
                           | (1<<10)        // 0:Full duplex 1:Receive-only
                           | (1<<9)         // 0:enable NSS 1:disable NSS (Software slave management)
                           | (0<<8)         // This bit has an effect only when the SSM bit is set.
                           | (0<<7)         // 0:MSB 1:LSB
                           | (7<<3)         // bit3-5 baudrate divider
                           | (0<<2)         // 0:Slave 1:Master
                           | (cpol<<1)      // CPOL
                           | (cpha<<0);     // CPHA

  SPI_CTL1(ST7920_SPI_NUM) |= 1<<6;         // RX buffer not empty interrupt enable
  SPI_CTL0(ST7920_SPI_NUM) |= 1<<6;         // Enable SPI
}

void SPI_Slave(CIRCULAR_QUEUE *queue)
{
  // initializes the initial queue indexes before the queue is used.
  // Otherwise, dirty values will let the system probably freeze when the queue is used
  spi_queue = queue;
  spi_queue->index_r = spi_queue->index_w = 0;

  // Reset SPI
  rcu_periph_reset_enable(ST7920_SPI_RST);
  rcu_periph_reset_disable(ST7920_SPI_RST);

  // Init SPI
  SPI_Slave_GPIO_Init(ST7920_SPI);
  GPIO_InitSet(PB12, MGPIO_MODE_IPU, 0);

  // Configure SPI interrupt
  nvic_irq_enable(ST7920_SPI_IRQ, 1U, 0U);

  // Enable SPI clock
  rcu_periph_clock_enable(ST7920_SPI_RCU);

  // Connect GPIOB12 to the interrupt line and enable CS interrupt
  gpio_exti_source_select(GPIO_EVENT_PORT_GPIOB, GPIO_EVENT_PIN_12);
  exti_init(EXTI_12, EXTI_INTERRUPT, EXTI_TRIG_BOTH);
  exti_interrupt_flag_clear(EXTI_12);
  nvic_irq_enable(EXTI10_15_IRQn, 0U, 1U);

  // Check if we need to enable the SPI interface
  if ((GPIO_ISTAT(GPIOB) & (1<<12)) != 0) {
    SPI_Enable((GPIO_ISTAT(GPIOB) & (1<<13)) != 0, 1);
  }
}

void SPI_SlaveDeinit(void) {
  // Disable interrupts
  nvic_irq_disable(ST7920_SPI_IRQ);
  nvic_irq_disable(EXTI10_15_IRQn);

  // Reset SPI
  rcu_periph_reset_enable(ST7920_SPI_RST);
  rcu_periph_reset_disable(ST7920_SPI_RST);
  spi_queue = NULL;
}

void EXTI10_15_IRQHandler(void)
{
  if (exti_interrupt_flag_get(EXTI_12) != RESET) {
    // Check CS pin
    if ((GPIO_ISTAT(GPIOB) & (1<<12)) != 0) {
      // Enable SPI
      SPI_Enable((GPIO_ISTAT(GPIOB) & (1<<13)) != 0, 1);
    } else {
      // Reset SPI
      rcu_periph_reset_enable(ST7920_SPI_RST);
      rcu_periph_reset_disable(ST7920_SPI_RST);
    }

    // Clear interrupt status register
    exti_interrupt_flag_clear(EXTI_12);
  }
}

void ST7920_SPI_IRQHandler(void)
{
  if (spi_queue != NULL) {
    spi_queue->data[spi_queue->index_w] = SPI_DATA(ST7920_SPI_NUM);
    spi_queue->index_w = (spi_queue->index_w + 1) % CIRCULAR_QUEUE_SIZE;
  }
}

bool SPI_SlaveGetData(uint8_t *data)
{
  if (spi_queue != NULL && spi_queue->index_r != spi_queue->index_w)
  {
    *data = spi_queue->data[spi_queue->index_r];
    spi_queue->index_r = (spi_queue->index_r + 1) % CIRCULAR_QUEUE_SIZE;
    return true;
  }
  return false;
}
#endif
