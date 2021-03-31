/** @file crc.c
 *  @brief Provides a primitive software crc implementation.
 *
 *	Default: CRC-32/BZIP2
 *	Polynom: 0x04C11DB7
 *	XOR-OUT: 0xFFFFFFFF
 *
 *  @author JK
 *  @bug No known bugs.
 */

#include "internal/util.h"

uint16_t crc16_sw_calculate(uint8_t const data[], int n){
	uint16_t crc = 0xFFFF;
	for (int i = 0; i < n; i++) {
		crc ^= (data[i] << 8);
		for (uint8_t j = 0; j < 8; j++) {
			if(crc & (1 << 15)){
				crc = (crc << 1) ^ SHEAPERD_CRC16_POLY;
			}else{
				crc = crc << 1;
			}
		}
	}
	return crc ^ SHEAPERD_CRC16_XOR_OUT;
}

uint32_t crc32_sw_calculate(uint8_t const data[], int n){
	uint32_t crc = 0xFFFFFFFF;
	for (int i = 0; i < n; i++) {
		crc ^= (data[i] << 24);
		for (uint8_t j = 0; j < 8; j++) {
			if(crc & (1 << 31)){
				crc = (crc << 1) ^ SHEAPERD_CRC32_POLY;
			}else{
				crc = crc << 1;
			}
		}
	}
	return crc ^ SHEAPERD_CRC32_XOR_OUT;
}
