/** @file memory_protection.h
 *  @brief Provides apis for configuring memory regions and access permissions using the MPU
 *
 *  @author JK
 *  @bug No known bugs.
 */

#ifndef INC_MEMORY_PROTECTION_H_
#define INC_MEMORY_PROTECTION_H_

#include "sheaperd.h"

#define MPU_DEFAULT_TEX		0x00

#define MPU_ACTIVATE_REGION(region)				\
do{												\
	mpu_region_t* r = &region;					\
	r->enabled = true;							\
	memory_protection_configureRegion(r, true); \
} while(0)

#define MPU_DEACTIVATE_REGION(region)			\
do{												\
	mpu_region_t* r = &region;					\
	r->enabled = false;							\
	memory_protection_configureRegion(r, true); \
} while(0)

/**
 * The region size used in the MPU is written to the RASR register and handled as follows:
 * size = 2 ^ (RASR.size + 1)
 *
 * Example: 32 bytes represented by 4 ('MPU_REGIONSIZE_32B' = (uint8_t)0x04U)
 *
 * size = 2 ^ (4 + 1)
 * size = 32
 *
 */
typedef enum {
	REGIONSIZE_32B 		= (uint8_t)0x04,
	REGIONSIZE_64B 		= (uint8_t)0x05,
	REGIONSIZE_128B		= (uint8_t)0x06,
	REGIONSIZE_256B		= (uint8_t)0x07,
	REGIONSIZE_512B		= (uint8_t)0x08,
	REGIONSIZE_1KB 		= (uint8_t)0x09,
	REGIONSIZE_2KB 		= (uint8_t)0x0A,
	REGIONSIZE_4KB 		= (uint8_t)0x0B,
	REGIONSIZE_8KB 		= (uint8_t)0x0C,
	REGIONSIZE_16KB 	= (uint8_t)0x0D,
	REGIONSIZE_32KB 	= (uint8_t)0x0E,
	REGIONSIZE_64KB 	= (uint8_t)0x0F,
	REGIONSIZE_128KB 	= (uint8_t)0x10,
	REGIONSIZE_256KB 	= (uint8_t)0x11,
	REGIONSIZE_512KB 	= (uint8_t)0x12,
	REGIONSIZE_1MB 		= (uint8_t)0x13,
	REGIONSIZE_2MB 		= (uint8_t)0x14,
	REGIONSIZE_4MB 		= (uint8_t)0x15,
	REGIONSIZE_8MB 		= (uint8_t)0x16,
	REGIONSIZE_16MB 	= (uint8_t)0x17,
	REGIONSIZE_32MB 	= (uint8_t)0x18,
	REGIONSIZE_64MB 	= (uint8_t)0x19,
	REGIONSIZE_128MB 	= (uint8_t)0x1A,
	REGIONSIZE_256MB	= (uint8_t)0x1B,
	REGIONSIZE_512MB 	= (uint8_t)0x1C,
	REGIONSIZE_1GB 		= (uint8_t)0x1D,
	REGIONSIZE_2GB 		= (uint8_t)0x1E,
	REGIONSIZE_4GB 		= (uint8_t)0x1F,
} mpu_regionSize_t;

typedef enum {
	MPU_REGION_ALL_ACCESS_DENIED				= (uint8_t)0x00,
	MPU_REGION_PRIVELEGED_RW					= (uint8_t)0x01,
	MPU_REGION_PRIVELEGED_RW_UNPRIVILEGED_RO	= (uint8_t)0x02,
	MPU_REGION_ALL_ACCESS_ALLOWED				= (uint8_t)0x03,
	MPU_REGION_PRIVELEGED_RO					= (uint8_t)0x05,
	MPU_REGION_PRIVELEGED_RO_UNPRIVILEGED_RO	= (uint8_t)0x06
} mpu_access_permission_t;

typedef enum {
	NO_MPU_AVAILABLE					= -0x01,
	INVALID_REGION_ADDRESS				= -0x02,
	INVALID_REGION_ADDRESS_ALIGNMENT	= -0x03,
	INVALID_REGION_NUMBER				= -0x04,

	NO_ERROR							= 0x00
} mpu_error_t;

typedef struct {
	uint32_t address;
	bool enabled;
	uint8_t number;
	uint8_t srd;
	mpu_regionSize_t size;
	mpu_access_permission_t ap;
	bool cachable;
	bool bufferable;
	bool shareable;
	uint8_t tex;
	bool xn;
} mpu_region_t;

mpu_error_t memory_protection_enableMPU();
mpu_error_t memory_protection_disableMPU();

bool memory_protection_isMPUEnabled();
mpu_error_t memory_protection_configureRegion(mpu_region_t* region, bool activateMPU);
uint8_t memory_protection_getNumberOfMPURegions();

#endif /* INC_MEMORY_PROTECTION_H_ */
