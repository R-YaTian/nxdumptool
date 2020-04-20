/*
 * Copyright (c) 2020 DarkMatterCore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#ifndef __GAMECARD_H__
#define __GAMECARD_H__

#include "fs_ext.h"

#define GAMECARD_HEAD_MAGIC         0x48454144              /* "HEAD" */
#define GAMECARD_CERT_MAGIC         0x43455254              /* "CERT" */
#define GAMECARD_HFS0_MAGIC         0x48465330              /* "HFS0" */

#define GAMECARD_MEDIA_UNIT_SIZE    0x200

#define GAMECARD_HFS_PARTITION_NAME(x)  ((x) == GameCardHashFileSystemPartitionType_Update ? "update" : ((x) == GameCardHashFileSystemPartitionType_Logo ? "logo" : \
                                        ((x) == GameCardHashFileSystemPartitionType_Normal ? "normal" : ((x) == GameCardHashFileSystemPartitionType_Secure ? "secure" : "unknown"))))

typedef enum {
    GameCardKekIndex_Version0      = 0,
    GameCardKekIndex_VersionForDev = 1
} GameCardKekIndex;

typedef struct {
    u8 kek_index          : 4;  ///< GameCardKekIndex.
    u8 titlekey_dec_index : 4;
} GameCardKeyFlags;

typedef enum {
    GameCardRomSize_1GiB  = 0xFA,
    GameCardRomSize_2GiB  = 0xF8,
    GameCardRomSize_4GiB  = 0xF0,
    GameCardRomSize_8GiB  = 0xE0,
    GameCardRomSize_16GiB = 0xE1,
    GameCardRomSize_32GiB = 0xE2
} GameCardRomSize;

typedef struct {
    u8 autoboot                              : 1;
    u8 history_erase                         : 1;
    u8 repair_tool                           : 1;
    u8 different_region_cup_to_terra_device  : 1;
    u8 different_region_cup_to_global_device : 1;
} GameCardFlags;

typedef enum {
    GameCardSelSec_ForT1 = 0,
    GameCardSelSec_ForT2 = 1
} GameCardSelSec;

typedef enum {
    GameCardFwVersion_Dev         = 0,
    GameCardFwVersion_Prod        = 1,
    GameCardFwVersion_Since400NUP = 2
} GameCardFwVersion;

typedef enum {
    GameCardAccCtrl_25MHz = 0xA10011,
    GameCardAccCtrl_50MHz = 0xA10010
} GameCardAccCtrl;

typedef enum {
    GameCardCompatibilityType_Normal = 0,
    GameCardCompatibilityType_Terra  = 1
} GameCardCompatibilityType;

typedef struct {
	u64 fw_version;         ///< GameCardFwVersion.
	u32 acc_ctrl;           ///< GameCardAccCtrl.
    u32 wait_1_time_read;   ///< Always 0x1388.
	u32 wait_2_time_read;   ///< Always 0.
	u32 wait_1_time_write;  ///< Always 0.
	u32 wait_2_time_write;  ///< Always 0.
    u32 fw_mode;
	u32 upp_version;
    u8 compatibility_type;  ///< GameCardCompatibilityType.
	u8 reserved_1[0x3];
	u64 upp_hash;
	u64 upp_id;             ///< Must match GAMECARD_UPDATE_TID.
	u8 reserved_2[0x38];
} GameCardExtendedHeader;

typedef struct {
    u8 signature[0x100];                            ///< RSA-2048 PKCS #1 signature over the rest of the header.
    u32 magic;                                      ///< "HEAD".
    u32 secure_area_start_address;                  ///< Expressed in GAMECARD_MEDIA_UNIT_SIZE blocks.
    u32 backup_area_start_address;                  ///< Always 0xFFFFFFFF.
    GameCardKeyFlags key_flags;
    u8 rom_size;                                    ///< GameCardRomSize.
    u8 header_version;
    GameCardFlags flags;
    u64 package_id;
    u32 valid_data_end_address;                     ///< Expressed in GAMECARD_MEDIA_UNIT_SIZE blocks.
    u8 reserved[0x4];
    u8 iv[0x10];
    u64 partition_fs_header_address;                ///< Root HFS0 header offset.
    u64 partition_fs_header_size;                   ///< Root HFS0 header size.
    u8 partition_fs_header_hash[SHA256_HASH_SIZE];
    u8 initial_data_hash[SHA256_HASH_SIZE];
    u32 sel_sec;                                    ///< GameCardSelSec.
    u32 sel_t1_key_index;
    u32 sel_key_index;
    u32 normal_area_end_address;                    ///< Expressed in GAMECARD_MEDIA_UNIT_SIZE blocks.
    GameCardExtendedHeader extended_header;         ///< Encrypted using AES-128-CBC with 'xci_header_key', which can't dumped through current methods.
} GameCardHeader;

typedef struct {
    u32 magic;              ///< "HFS0".
    u32 entry_count;
    u32 name_table_size;
    u8 reserved[0x4];
} GameCardHashFileSystemHeader;

typedef struct {
    u64 offset;
    u64 size;
    u32 name_offset;
    u32 hash_target_size;
    u64 hash_target_offset;
    u8 hash[SHA256_HASH_SIZE];
} GameCardHashFileSystemEntry;

typedef enum {
    GameCardHashFileSystemPartitionType_Update  = 0,
    GameCardHashFileSystemPartitionType_Logo    = 1,    ///< Only available in GameCardFwVersion_Since400NUP gamecards.
    GameCardHashFileSystemPartitionType_Normal  = 2,
    GameCardHashFileSystemPartitionType_Secure  = 3
} GameCardHashFileSystemPartitionType;

/// Initializes data needed to access raw gamecard storage areas.
/// Also spans a background thread to automatically detect gamecard status changes and to cache data from the inserted gamecard.
Result gamecardInitialize(void);

/// Deinitializes data generated by gamecardInitialize().
/// This includes destroying the background gamecard detection thread and freeing all cached gamecard data.
void gamecardExit(void);

/// Used to check if a gamecard has been inserted and if info could be loaded from it.
bool gamecardIsReady(void);

/// Used to read data from the inserted gamecard.
/// All required handles, changes between normal <-> secure storage areas and proper offset calculations are managed internally.
/// offset + read_size should never exceed the value returned by gamecardGetTotalSize().
bool gamecardRead(void *out, u64 read_size, u64 offset);

/// Miscellaneous functions.
bool gamecardGetHeader(GameCardHeader *out);
bool gamecardGetTotalSize(u64 *out);
bool gamecardGetTrimmedSize(u64 *out);
bool gamecardGetRomCapacity(u64 *out); ///< Not the same as gamecardGetTotalSize().
bool gamecardGetCertificate(FsGameCardCertificate *out);
bool gamecardGetBundledFirmwareUpdateVersion(u32 *out);

bool gamecardGetOffsetAndSizeFromHashFileSystemPartitionEntryByName(u8 hfs_partition_type, const char *name, u64 *out_offset, u64 *out_size); ///< GameCardHashFileSystemPartitionType.

#endif /* __GAMECARD_H__ */