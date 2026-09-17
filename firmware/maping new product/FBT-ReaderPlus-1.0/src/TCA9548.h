#ifndef _TCA9548_H_
#define _TCA9548_H_

#include <Wire.h>

#define ADDR_TCA 0x70

class TCA9548
{
private:
    /* data */
public:
    TCA9548(/* args */);
    ~TCA9548();
    void begin();
    void selectChannel(uint8_t channel);
    void closeChannel();
};

#endif