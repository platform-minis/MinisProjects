import os, sys, io
from machine import Pin, I2C
import time

# AHT20 + BMP280 via I2C
# Wiring:
#   SDA  -> GP13
#   SCL  -> GP14
#   VCC  -> 3.3 V
#   GND  -> GND
#
# AHT20  I2C addr 0x38
#   Temperature : -40 to 85 C   +/-0.3 C   resolution 0.01 C
#   Humidity    :   0 to 100 %  +/-2 %     resolution 0.024 %
#
# BMP280 I2C addr 0x76 (SDO=GND) or 0x77 (SDO=3.3V)
#   Temperature : -40 to 85 C   +/-1 C
#   Pressure    : 300 to 1100 hPa  +/-1 hPa

_i2c = I2C(0, sda=Pin(13), scl=Pin(14), freq=400000)
_AHT20_ADDR = 0x38
_BMP280_ADDR = 0x76
_bmp280_cal = None


def _u16(b, i):
    return b[i] | (b[i + 1] << 8)


def _s16(b, i):
    v = _u16(b, i)
    return v - 65536 if v > 32767 else v


def _aht20_init():
    _i2c.writeto(_AHT20_ADDR, bytes([0xBE, 0x08, 0x00]))
    time.sleep_ms(20)


def _aht20_read():
    _i2c.writeto(_AHT20_ADDR, bytes([0xAC, 0x33, 0x00]))
    time.sleep_ms(80)
    d = _i2c.readfrom(_AHT20_ADDR, 6)
    rh = (d[1] << 12) | (d[2] << 4) | (d[3] >> 4)
    rt = ((d[3] & 0x0F) << 16) | (d[4] << 8) | d[5]
    temp = round(rt / 1048576.0 * 200.0 - 50.0, 1)
    hum = round(rh / 1048576.0 * 100.0, 1)
    return temp, hum


def _bmp280_init():
    # osrs_t=2, osrs_p=5, mode=3 (normal)
    _i2c.writeto_mem(_BMP280_ADDR, 0xF4, bytes([0xB7]))
    time.sleep_ms(10)


def _bmp280_read_cal():
    c = _i2c.readfrom_mem(_BMP280_ADDR, 0x88, 24)
    return (
        _u16(c, 0), _s16(c, 2), _s16(c, 4),
        _u16(c, 6), _s16(c, 8), _s16(c, 10),
        _s16(c, 12), _s16(c, 14), _s16(c, 16),
        _u16(c, 18), _s16(c, 20), _s16(c, 22),
    )


def _bmp280_read(cal):
    r = _i2c.readfrom_mem(_BMP280_ADDR, 0xF7, 6)
    adc_p = (r[0] << 12) | (r[1] << 4) | (r[2] >> 4)
    adc_t = (r[3] << 12) | (r[4] << 4) | (r[5] >> 4)
    v1 = (adc_t / 16384.0 - cal[0] / 1024.0) * cal[1]
    v2 = ((adc_t / 131072.0 - cal[0] / 8192.0) ** 2) * cal[2]
    tf = v1 + v2
    temp = round(tf / 5120.0, 1)
    v1 = tf / 2.0 - 64000.0
    v2 = v1 * v1 * cal[8] / 32768.0 + v1 * cal[7] * 2.0
    v2 = v2 / 4.0 + cal[6] * 65536.0
    v1 = (cal[5] * v1 * v1 / 524288.0 + cal[4] * v1) / 524288.0
    v1 = (1.0 + v1 / 32768.0) * cal[3]
    if v1 == 0:
        return temp, 0.0
    p = 1048576.0 - adc_p
    p = (p - v2 / 4096.0) * 6250.0 / v1
    v1 = cal[11] * p * p / 2147483648.0
    v2 = p * cal[10] / 32768.0
    pres = round((p + (v1 + v2 + cal[9]) / 16.0) / 100.0, 1)
    return temp, pres


def setup():
    global _bmp280_cal
    _aht20_init()
    _bmp280_init()
    _bmp280_cal = _bmp280_read_cal()
    print('AHT20 + BMP280 ready   SDA=GP13  SCL=GP14')


def loop():
    temp_a, hum = _aht20_read()
    temp_b, pres = _bmp280_read(_bmp280_cal)
    print('AHT20:  Temp=' + str(temp_a) + ' C   Hum=' + str(hum) + ' %')
    print('BMP280: Temp=' + str(temp_b) + ' C   Pres=' + str(pres) + ' hPa')
    time.sleep_ms(2000)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
