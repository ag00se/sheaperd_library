/** @file util.h
 *  @brief Provides utility functionality.
 *
 *  @author JK
 *  @bug No known bugs.
 */

#ifndef INTERNAL_UTIL_H_
#define INTERNAL_UTIL_H_

#include "sheaperd.h"

typedef enum {
	ERROR_MUTEX_CREATION_FAILED,
	ERROR_MUTEX_DELETION_FAILED,
	ERROR_MUTEX_IS_NULL,
	ERROR_MUTEX_ACQUIRE_FAILED,
	ERROR_MUTEX_RELEASE_FAILED,

	ERROR_NO_ERROR
} util_error_t;

util_error_t util_initMutex(osMutexId_t* mutexId, const osMutexAttr_t* mutexAttr);
util_error_t util_acquireMutex(osMutexId_t mutexId, uint32_t timeout);
util_error_t util_releaseMutex(osMutexId_t mutexId);

uint16_t util_crc16_sw_calculate(uint8_t const data[], int n);
uint32_t util_crc32_sw_calculate(uint8_t const data[], int n);

#endif /* INTERNAL_UTIL_H_ */
