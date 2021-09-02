/** @file stackguard.h
 *  @brief Provides the api for the guarding of task stacks using the MPU
 *
 *  @author JK
 *  @bug No known bugs.
 */


#ifndef STACKGUARD_H_
#define STACKGUARD_H_

#include "memory_protection.h"
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
	STACKGUARD_MUTEX_ACQUIRE_FAILED								= -0x10,
	STACKGUARD_INVALID_REGION_NUMBER							= -0x11,

	STACKGUARD_NO_ERROR											= 0x00
} stackguard_error_t;

// see:ARMv7-M Architecture Reference Manual https://developer.arm.com/documentation/ddi0403/ed
#pragma pack(1)
typedef struct {
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r12;
  uint32_t lr;
  uint32_t return_address;
  uint32_t xpsr;
} stackguard_stackFrame_t;
#pragma pack()

typedef void (*stackguarg_memFault_cb)(uint32_t faultAddress, stackguard_stackFrame_t stackFrame);

/**
 * Initializes the stackguard functionality.
 * As stackguard is using the MPU a call to this function will disable a currently active MPU.
 */
stackguard_error_t stackguard_init(stackguarg_memFault_cb memFaultCallback);

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
stackguard_error_t stackguard_addTask(uint32_t taskId, uint32_t* sp, mpu_regionSize_t stackSize, mpu_access_permission_t initialAP, bool xn);
stackguard_error_t stackguard_addTaskByteSize(uint32_t taskId, uint32_t* sp, uint32_t stackSize, mpu_access_permission_t initialAP, bool xn);
stackguard_error_t stackguard_removeTask(uint32_t taskId);

stackguard_error_t stackguard_guard();

void stackguard_taskSwitchIn(uint32_t taskId, bool enableMPU);

#endif /* STACKGUARD_H_ */
