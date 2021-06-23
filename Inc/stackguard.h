/** @file stackguard.h
 *  @brief Provides the api for the guarding of task stacks using the MPU
 *
 *  @author JK
 *  @bug No known bugs.
 */


#ifndef STACKGUARD_H_
#define STACKGUARD_H_

#include "sheaperd.h"
#include "memory_protection.h"

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
	STACKGUARD_MUTEX_ACQUIRE_FAILED								= -0x10,
	STACKGUARD_INVALID_REGION_NUMBER							= -0x11,

	STACKGUARD_NO_ERROR											= 0x00
} stackguard_error_t;

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
 * 					STACKGUARD_INVALID_STACK_ALIGNMENT 	(on Armv7 architecture: the stack pointer is not properly aligned for the provided @param stackSize)
 * 					STACKGUARD_NO_MPU_REGION_LEFT		(no free MPU region is left to be configured)
 * 					STACKGUARD_MUTEX_ACQUIRE_FAILED		(could not acquire the mutex)
 */
stackguard_error_t stackguard_addTask(uint32_t taskId, uint32_t* sp, mpu_regionSize_t stackSize);
stackguard_error_t stackguard_removeTask(uint32_t taskId);

stackguard_error_t stackguard_guard();

void stackguard_taskSwitchIn(uint32_t taskId);

#endif /* STACKGUARD_H_ */
