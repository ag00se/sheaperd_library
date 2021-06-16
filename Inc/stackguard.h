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
	STACKGUARD_INVALID_MPU_ADDRESS								= -0x01,
	STACKGUARD_NO_MPU_REGION_LEFT								= -0x02,
	STACKGUARD_NO_SYNCHRONIZATION_BARRIER_CALLBACKS_PROVIDED	= -0x03,
	STACKGUARD_MPU_ALREADY_ENABLED								= -0x04,
	STACKGUARD_MPU_ALREADY_DISABLED								= -0x05,
	STACKGUARD_MPU_INVALID_REGION_SIZE							= -0x06,
	STACKGUARD_INVALID_STACK_ALIGNMENT							= -0x07,
	STACKGUARD_NO_MPU_AVAILABLE									= -0x08,
	STACKGUARD_TASK_NOT_FOUND									= -0x09,

	STACKGUARD_NO_ERROR											= 0x00
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
	STACKGUARD_MPU_REGIONSIZE_4GB 	= (uint8_t)0x1FU,
	STACKGUARD_MPU_INVALID_SIZE 	= -0x01
}stackguard_mpu_regionSize_t;

/**
 * Initializes the stackguard functionality.
 * As stackguard is using the MPU a call to this function will disable a currently active MPU.
 */
stackguard_error_t stackguard_init();

/**
 * Creates a MPU region for the provided stack pointer address and stack size.
 *
 * @param taskId	Provides an id which is used as reference for adding and removing regions
 * @param sp		Provides the base address of the MPU memory region
 * @param stackSize	Provides the size of the MPU memory region. Due to the register layout the size is restricted to the values
 * 					available from the 'stackguard_mpu_regionSize_t' enum
 *
 * @return error	Possible error return are:
 * 					STACKGUARD_INVALID_MPU_ADDRESS 		(the address is not aligned to 32 bits)
 * 					STACKGUARD_MPU_INVALID_REGION_SIZE 	(the provided @param stackSize is invalid)
 * 					STACKGUARD_INVALID_STACK_ALIGNMENT 	(on Armv7 architecture: the stack pointer is not properly aligned for the provided @param stackSize)
 * 					STACKGUARD_NO_MPU_REGION_LEFT		(no free MPU region is left to be configured)
 */
stackguard_error_t stackguard_addTask(uint32_t taskId, uint32_t* sp, stackguard_mpu_regionSize_t stackSize);
stackguard_error_t stackguard_removeTask(uint32_t taskId);

void stackguard_taskSwitchIn(uint32_t taskId);
bool stackguard_isMPUEnabled();

#endif /* STACKGUARD_H_ */
