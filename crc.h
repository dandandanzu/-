#ifndef CRC_H
#define CRC_H

#include <QtGlobal>

class CRC
{
public:
    CRC();
};

#ifdef __cplusplus
extern "C" {
#endif

uint16_t Modbus_CRC16(uint8_t *puchMsg, uint16_t usDataLen);

#ifdef __cplusplus
}
#endif

#endif // CRC_H
