/** @file util.h
 *  @brief Provides utility functionality.
 *
 *  @author JK
 *  @bug No known bugs.
 */

#ifndef INTERNAL_UTIL_H_
#define INTERNAL_UTIL_H_

#include "sheaperd.h"

uint16_t crc16_sw_calculate(uint8_t const data[], int n);
uint32_t crc32_sw_calculate(uint8_t const data[], int n);

#endif /* INTERNAL_UTIL_H_ */
