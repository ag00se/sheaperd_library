/** @file cmsis_ti_rtos.c
 *  @brief Provides a cmsis2 interface port for ti rtos APIs. (Only the currently needed functions are ported)
 *
 *  @author JK
 *  @bug No known bugs.
 */

#include "sheaperd.h"

#ifdef SHEAPERD_CMSIS_PORT_TI
#include "cmsis_os2.h"
#include <ti/sysbios/knl/Semaphore.h>

osMutexId_t osMutexNew(const osMutexAttr_t *attr) {
    Semaphore_Params params;
    Semaphore_Params_init(&params);
    params.instance->name = attr->name;
    return Semaphore_create(1, &params, NULL);
}

osStatus_t osMutexDelete(osMutexId_t mutex_id) {
    Semaphore_delete(mutex_id);
    return osOK;
}

osStatus_t osMutexAcquire(osMutexId_t mutex_id, uint32_t timeout) {
    return Semaphore_pend(mutex_id, timeout) == true ? osOK : osErrorTimeout;
}

osStatus_t osMutexRelease(osMutexId_t mutex_id) {
    Semaphore_post(mutex_id);
    return osOK;
}
#endif
