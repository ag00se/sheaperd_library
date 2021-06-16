/** @file util.c
 *  @brief Provides utility function for the sheaperd library.
 *
 *  @author JK
 *  @bug No known bugs.
 */

#include "internal/util.h"

util_error_t util_initMutex(osMutexId_t* mutexId, const osMutexAttr_t* mutexAttr){
#ifdef SHEAPERD_CMSIS_2
	if(mutexId != NULL && *mutexId != 0){
		osStatus_t status = osMutexDelete(*mutexId);
		if(status != osOK){
			return ERROR_MUTEX_DELETION_FAILED;
		}
	}
	*mutexId = osMutexNew(mutexAttr);
	if(*mutexId == NULL){
		return ERROR_MUTEX_CREATION_FAILED;
	}
#endif
	return ERROR_NO_ERROR;
}

util_error_t util_acquireMutex(osMutexId_t mutexId, uint32_t timeout){
#ifdef SHEAPERD_CMSIS_2
	if(mutexId == NULL){
    	return ERROR_MUTEX_IS_NULL;
	}
	osStatus_t status = osMutexAcquire(mutexId, timeout);
    if (status != osOK)  {
    	return ERROR_MUTEX_ACQUIRE_FAILED;
    }
#endif
    return ERROR_NO_ERROR;
}

util_error_t util_releaseMutex(osMutexId_t mutexId){
#ifdef SHEAPERD_CMSIS_2
	if (mutexId == NULL) {
    	return ERROR_MUTEX_IS_NULL;
	}
	osStatus_t status = osMutexRelease(mutexId);
	if (status != osOK) {
    	return ERROR_MUTEX_RELEASE_FAILED;
	}
#endif
	return ERROR_NO_ERROR;
}

uint16_t util_crc16_sw_calculate(uint8_t const data[], int n){
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

uint32_t util_crc32_sw_calculate(uint8_t const data[], int n){
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