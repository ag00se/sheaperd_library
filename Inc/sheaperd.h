/** @file sheaperd.h
 *  @brief Provides main includes and typedefs for the sheaperd library
 *
 *  @author JK
 *  @bug No known bugs.
 */

#ifndef SHEAPERD_H_
#define SHEAPERD_H_

#include "internal/def.h"
#include "internal/opt.h"
#include "internal/util.h"
#include "sheap.h"

//typedef struct {
//
//} MPU_Type;

typedef enum {
	SHEAPERD_MPU_NOT_AVAILABLE,
	SHEAPERD_MPU_INITIALIZED,
	SHEAPERD_MPU_INTIALIZATION_FAILED,
	SHEAPERD_MPU_NOT_SUPPORTED_ATM
} Sheaperd_MPUState_t;

//typedef enum {
//	SHEAPERD_MPU_M0PLUS,
//	SHEAPERD_MPU_M3_M4_M7,
//	SHEAPERD_MPU_M23,
//	SHEAPERD_MPU_M33_M35P
//} Sheaperd_MPUVersion_t;

#endif /* SHEAPERD_H_ */
