#include "TCA9548.h"

TCA9548::TCA9548(/* args */)
{
}

TCA9548::~TCA9548()
{
}

void TCA9548::begin()
{
    Wire.begin(33, 32);
}

/* Select channel */
void TCA9548::selectChannel(uint8_t channel)
{
    Wire.beginTransmission(ADDR_TCA);
    Wire.write(1 << channel);
    Wire.endTransmission();
}

void TCA9548::closeChannel()
{
    Wire.beginTransmission(ADDR_TCA);
    Wire.write(0);
    Wire.endTransmission();
}