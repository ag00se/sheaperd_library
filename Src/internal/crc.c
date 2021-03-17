/** @file crc.c
 *  @brief Provides a primitive software crc implementation.
 *
 *  @author JK
 *  @bug No known bugs.
 */

#include "internal/util.h"

//TODO:CHECK THIS
uint32_t crc32_sw_calculate(uint8_t const data[], int n){
	uint32_t crc = ~0;
	for(int i = 0; i < n; i++){
		printf("%x\r\n", data[i]);
		if(((crc >> 31) & 1) != data[i]){
			crc = (crc << 1) ^ SHEAPERD_CRC32_POLY;
		}else{
			crc = (crc << 1);
		}
	}
	printf("%d\r\n", crc);
	return crc^SHEAPERD_CRC32_XOR_OUT;
}
