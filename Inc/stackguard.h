/** @file stackguard.h
 *  @brief Provides the api for the guarding of task stacks using the MPU
 *
 *  @author JK
 *  @bug No known bugs.
 */


#ifndef STACKGUARD_H_
#define STACKGUARD_H_

#include "sheaperd.h"

typedef enum {
	STACKGUARD_INVALID_MPU_ADDRESS,
	STACKGUARD_NO_MPU_REGION_LEFT,
	STACKGUARD_NO_SYNCHRONIZATION_BARRIER_CALLBACKS_PROVIDED,
	STACKGUARD_NO_ERROR
} stackguard_error_t;

/**
 * The region size used in the MPU is written to a RASR register and handled as follows:
 * size = 2 ^ (RASR.size + 1)
 *
 * Example: 32 bytes represented by 4 ('STACKGUARD_MPU_REGIONSIZE_32B' = (uint8_t)0x04U)
 *
 * size = 2 ^ (4 + 1)
 * size = 32
 *
 */
typedef enum {
	STACKGUARD_MPU_REGIONSIZE_32B 	= (uint8_t)0x04U,
	STACKGUARD_MPU_REGIONSIZE_64B 	= (uint8_t)0x05U,
	STACKGUARD_MPU_REGIONSIZE_128B	= (uint8_t)0x06U,
	STACKGUARD_MPU_REGIONSIZE_256B	= (uint8_t)0x07U,
	STACKGUARD_MPU_REGIONSIZE_512B	= (uint8_t)0x08U,
	STACKGUARD_MPU_REGIONSIZE_1KB 	= (uint8_t)0x09U,
	STACKGUARD_MPU_REGIONSIZE_2KB 	= (uint8_t)0x0AU,
	STACKGUARD_MPU_REGIONSIZE_4KB 	= (uint8_t)0x0BU,
	STACKGUARD_MPU_REGIONSIZE_8KB 	= (uint8_t)0x0CU,
	STACKGUARD_MPU_REGIONSIZE_16KB 	= (uint8_t)0x0DU,
	STACKGUARD_MPU_REGIONSIZE_32KB 	= (uint8_t)0x0EU,
	STACKGUARD_MPU_REGIONSIZE_64KB 	= (uint8_t)0x0FU,
	STACKGUARD_MPU_REGIONSIZE_128KB = (uint8_t)0x10U,
	STACKGUARD_MPU_REGIONSIZE_256KB = (uint8_t)0x11U,
	STACKGUARD_MPU_REGIONSIZE_512KB = (uint8_t)0x12U,
	STACKGUARD_MPU_REGIONSIZE_1MB 	= (uint8_t)0x13U,
	STACKGUARD_MPU_REGIONSIZE_2MB 	= (uint8_t)0x14U,
	STACKGUARD_MPU_REGIONSIZE_4MB 	= (uint8_t)0x15U,
	STACKGUARD_MPU_REGIONSIZE_8MB 	= (uint8_t)0x16U,
	STACKGUARD_MPU_REGIONSIZE_16MB 	= (uint8_t)0x17U,
	STACKGUARD_MPU_REGIONSIZE_32MB 	= (uint8_t)0x18U,
	STACKGUARD_MPU_REGIONSIZE_64MB 	= (uint8_t)0x19U,
	STACKGUARD_MPU_REGIONSIZE_128MB = (uint8_t)0x1AU,
	STACKGUARD_MPU_REGIONSIZE_256MB = (uint8_t)0x1BU,
	STACKGUARD_MPU_REGIONSIZE_512MB = (uint8_t)0x1CU,
	STACKGUARD_MPU_REGIONSIZE_1GB 	= (uint8_t)0x1DU,
	STACKGUARD_MPU_REGIONSIZE_2GB 	= (uint8_t)0x1EU,
	STACKGUARD_MPU_REGIONSIZE_4GB 	= (uint8_t)0x1FU
}stackguard_mpu_regionSize_t;

typedef void (*stackguard_synchronization_barrier_cb)(void);

/**
 * Checks if the current CORTEX-M provides a MPU and initializes it.
 *
 * @param dsb	Provides a callback for a data synchronization barrier
 * @param isb	Provides a callback for a instruction synchronization barrier
 */
sheaperd_MPUState_t stackguard_initMPU(stackguard_synchronization_barrier_cb dsb, stackguard_synchronization_barrier_cb isb);

/**
 * Creates a MPU region for the provided stack pointer address and stack size.
 *
 * @param taskId	Provides an id which is used as reference for adding and removing regions
 * @param sp		Provides the base address of the MPU memory region
 * @param stackSize	Provides the size of the MPU memory region. Due to the register layout the size is restricted to the values
 * 					available from the 'stackguard_mpu_regionSize_t' enum
 */
stackguard_error_t stackguard_addTask(uint32_t taskId, uint32_t* sp, stackguard_mpu_regionSize_t stackSize);
stackguard_error_t stackguard_removeTask();

stackguard_error_t stackguard_enableMPU();
void stackguard_taskSwitchIn(uint32_t taskId, uint32_t* sp);

#endif /* STACKGUARD_H_ */
