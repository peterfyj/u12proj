#ifndef __KERN_DRIVER_CLOCK_H__
#define __KERN_DRIVER_CLOCK_H__

extern volatile unsigned long ticks;

void clock_init(void);

#endif /* !__KERN_DRIVER_CLOCK_H__ */

