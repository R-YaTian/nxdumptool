/*
 * nca.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * nxdumptool is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#ifndef __NCA_H__
#define __NCA_H__

#include "tik.h"

#define NCA_FS_HEADER_COUNT                         4
#define NCA_FULL_HEADER_LENGTH                      (sizeof(NcaHeader) + (sizeof(NcaFsHeader) * NCA_FS_HEADER_COUNT))

#define NCA_NCA0_MAGIC                              0x4E434130                  /* "NCA0". */
#define NCA_NCA2_MAGIC                              0x4E434132                  /* "NCA2". */
#define NCA_NCA3_MAGIC                              0x4E434133                  /* "NCA3". */

#define NCA_USED_KEY_AREA_SIZE                      sizeof(NcaDecryptedKeyArea) /* Four keys, 0x40 bytes. */

#define NCA_HIERARCHICAL_SHA256_MAX_REGION_COUNT    5

#define NCA_IVFC_MAGIC                              0x49564643                  /* "IVFC". */
#define NCA_IVFC_MAX_LEVEL_COUNT                    7
#define NCA_IVFC_LEVEL_COUNT                        (NCA_IVFC_MAX_LEVEL_COUNT - 1)
#define NCA_IVFC_BLOCK_SIZE(x)                      (1U << (x))

#define NCA_BKTR_MAGIC                              0x424B5452                  /* "BKTR". */

#define NCA_FS_SECTOR_SIZE                          0x200
#define NCA_FS_SECTOR_OFFSET(x)                     ((u64)(x) * NCA_FS_SECTOR_SIZE)

#define NCA_AES_XTS_SECTOR_SIZE                     0x200

typedef enum {
    NcaDistributionType_Download = 0,
    NcaDistributionType_GameCard = 1
} NcaDistributionType;

typedef enum {
    NcaContentType_Program    = 0,
    NcaContentType_Meta       = 1,
    NcaContentType_Control    = 2,
    NcaContentType_Manual     = 3,
    NcaContentType_Data       = 4,
    NcaContentType_PublicData = 5
} NcaContentType;

typedef enum {
    NcaKeyGenerationOld_100_230 = 0,
    NcaKeyGenerationOld_300     = 2
} NcaKeyGenerationOld;

typedef enum {
    NcaKeyAreaEncryptionKeyIndex_Application = 0,
    NcaKeyAreaEncryptionKeyIndex_Ocean       = 1,
    NcaKeyAreaEncryptionKeyIndex_System      = 2
} NcaKeyAreaEncryptionKeyIndex;

typedef struct {
    u32 NcaSdkAddOnVersion_Relstep : 8;
    u32 NcaSdkAddOnVersion_Micro   : 8;
    u32 NcaSdkAddOnVersion_Minor   : 8;
    u32 NcaSdkAddOnVersion_Major   : 8;
} NcaSdkAddOnVersion;

/// 'NcaKeyGeneration_Current' will always point to the last known key generation value.
typedef enum {
    NcaKeyGeneration_301_302  = 3,
    NcaKeyGeneration_400_410  = 4,
    NcaKeyGeneration_500_510  = 5,
    NcaKeyGeneration_600_610  = 6,
    NcaKeyGeneration_620      = 7,
    NcaKeyGeneration_700_801  = 8,
    NcaKeyGeneration_810_811  = 9,
    NcaKeyGeneration_900_901  = 10,
    NcaKeyGeneration_910_1004 = 11,
    NcaKeyGeneration_Current  = NcaKeyGeneration_910_1004
} NcaKeyGeneration;

typedef struct {
    u32 start_sector;   ///< Expressed in NCA_FS_SECTOR_SIZE sectors.
    u32 end_sector;     ///< Expressed in NCA_FS_SECTOR_SIZE sectors.
    u32 hash_sector;
    u8 reserved[0x4];
} NcaFsInfo;

typedef struct {
    u8 hash[SHA256_HASH_SIZE];
} NcaFsHeaderHash;

/// Encrypted NCA key area used to hold NCA FS section encryption keys. Zeroed out if the NCA uses titlekey crypto.
/// Only the first 4 key entries are encrypted.
/// If a particular key entry is unused, it is zeroed out before this area is encrypted.
typedef struct {
    u8 aes_xts_1[AES_128_KEY_SIZE];     ///< AES-128-XTS key 0 used for NCA FS sections with NcaEncryptionType_AesXts crypto.
    u8 aes_xts_2[AES_128_KEY_SIZE];     ///< AES-128-XTS key 1 used for NCA FS sections with NcaEncryptionType_AesXts crypto.
    u8 aes_ctr[AES_128_KEY_SIZE];       ///< AES-128-CTR key used for NCA FS sections with NcaEncryptionType_AesCtr crypto.
    u8 aes_ctr_ex[AES_128_KEY_SIZE];    ///< AES-128-CTR key used for NCA FS sections with NcaEncryptionType_AesCtrEx crypto.
    u8 aes_ctr_hw[AES_128_KEY_SIZE];    ///< Unused AES-128-CTR key.
    u8 reserved[0xB0];
} NcaEncryptedKeyArea;

/// First 0x400 bytes from every NCA.
typedef struct {
    u8 main_signature[0x100];                               ///< RSA-PSS signature over header with fixed key.
    u8 acid_signature[0x100];                               ///< RSA-PSS signature over header with key in NPDM.
    u32 magic;                                              ///< "NCA0" / "NCA2" / "NCA3".
    u8 distribution_type;                                   ///< NcaDistributionType.
    u8 content_type;                                        ///< NcaContentType.
    u8 key_generation_old;                                  ///< NcaKeyGenerationOld.
    u8 kaek_index;                                          ///< NcaKeyAreaEncryptionKeyIndex.
    u64 content_size;
    u64 program_id;
    u32 content_index;
    NcaSdkAddOnVersion sdk_addon_version;
    u8 key_generation;                                      ///< NcaKeyGeneration.
    u8 main_signature_key_generation;
    u8 reserved[0xE];
    FsRightsId rights_id;                                   ///< Used for titlekey crypto.
    NcaFsInfo fs_info[NCA_FS_HEADER_COUNT];                 ///< Start and end sectors for each NCA FS section.
    NcaFsHeaderHash fs_header_hash[NCA_FS_HEADER_COUNT];    ///< SHA-256 hashes calculated over each NCA FS section header.
    NcaEncryptedKeyArea encrypted_key_area;
} NcaHeader;

typedef enum {
    NcaFsType_RomFs       = 0,
    NcaFsType_PartitionFs = 1
} NcaFsType;

typedef enum {
    NcaHashType_Auto                  = 0,
    NcaHashType_None                  = 1,
    NcaHashType_HierarchicalSha256    = 2,  ///< Used by NcaFsType_PartitionFs.
    NcaHashType_HierarchicalIntegrity = 3   ///< Used by NcaFsType_RomFs.
} NcaHashType;

typedef enum {
    NcaEncryptionType_Auto     = 0,
    NcaEncryptionType_None     = 1,
    NcaEncryptionType_AesXts   = 2,
    NcaEncryptionType_AesCtr   = 3,
    NcaEncryptionType_AesCtrEx = 4
} NcaEncryptionType;

typedef struct {
    u64 offset;
    u64 size;
} NcaRegion;

/// Used by NcaFsType_PartitionFs and NCA0 NcaFsType_RomFs.
typedef struct {
    u8 master_hash[SHA256_HASH_SIZE];
    u32 hash_block_size;
    u32 hash_region_count;
    NcaRegion hash_region[NCA_HIERARCHICAL_SHA256_MAX_REGION_COUNT];
} NcaHierarchicalSha256Data;

typedef struct {
    u64 offset;
    u64 size;
    u32 block_order;    ///< Use NCA_IVFC_BLOCK_SIZE to calculate the actual block size using this value.
    u8 reserved[0x4];
} NcaHierarchicalIntegrityVerificationLevelInformation;

typedef struct {
    u8 value[0x20];
} NcaSignatureSalt;

#pragma pack(push, 1)
typedef struct {
    u32 max_level_count;                                                                            ///< Always NCA_IVFC_MAX_LEVEL_COUNT.
    NcaHierarchicalIntegrityVerificationLevelInformation level_information[NCA_IVFC_LEVEL_COUNT];
    NcaSignatureSalt signature_salt;
} NcaInfoLevelHash;
#pragma pack(pop)

/// Used by NcaFsType_RomFs.
typedef struct {
    u32 magic;                          ///< "IVFC".
    u32 version;
    u32 master_hash_size;               ///< Always SHA256_HASH_SIZE.
    NcaInfoLevelHash info_level_hash;
    u8 master_hash[SHA256_HASH_SIZE];
} NcaIntegrityMetaInfo;

typedef struct {
    union {
        struct {
            ///< Used if hash_type == NcaHashType_HierarchicalSha256 (NcaFsType_PartitionFs and NCA0 NcaFsType_RomFs).
            NcaHierarchicalSha256Data hierarchical_sha256_data;
            u8 reserved_1[0x80];
        };
        struct {
            ///< Used if hash_type == NcaHashType_HierarchicalIntegrity (NcaFsType_RomFs).
            NcaIntegrityMetaInfo integrity_meta_info;
            u8 reserved_2[0x18];
        };
    };
} NcaHashData;

typedef struct {
    u32 magic;          ///< "BKTR".
    u32 version;        ///< offset_count / node_count ?
    u32 entry_count;
    u8 reserved[0x4];
} NcaBucketTreeHeader;

typedef struct {
    u64 offset;
    u64 size;
    NcaBucketTreeHeader header;
} NcaBucketInfo;

/// Only used for NcaEncryptionType_AesCtrEx (PatchRomFs).
typedef struct {
    NcaBucketInfo indirect_bucket;
    NcaBucketInfo aes_ctr_ex_bucket;
} NcaPatchInfo;

typedef struct {
    union {
        u8 value[0x8];
        struct {
            u32 generation;
            u32 secure_value;
        };
    };
} NcaAesCtrUpperIv;

/// Used in NCAs with sparse storage.
typedef struct {
    NcaBucketInfo sparse_bucket;
    u64 physical_offset;
    u16 generation;
    u8 reserved[0x6];
} NcaSparseInfo;

/// Four NCA FS headers are placed right after the 0x400 byte long NCA header in NCA2 and NCA3.
/// NCA0 place the FS headers at the start sector from the NcaFsInfo entries.
typedef struct {
    u16 version;
    u8 fs_type;                         ///< NcaFsType.
    u8 hash_type;                       ///< NcaHashType.
    u8 encryption_type;                 ///< NcaEncryptionType.
    u8 reserved_1[0x3];
    NcaHashData hash_data;
    NcaPatchInfo patch_info;
    NcaAesCtrUpperIv aes_ctr_upper_iv;
    NcaSparseInfo sparse_info;
    u8 reserved_2[0x88];
} NcaFsHeader;

typedef enum {
    NcaFsSectionType_PartitionFs = 0,   ///< NcaFsType_PartitionFs + NcaHashType_HierarchicalSha256.
    NcaFsSectionType_RomFs       = 1,   ///< NcaFsType_RomFs + NcaHashType_HierarchicalIntegrity.
    NcaFsSectionType_PatchRomFs  = 2,   ///< NcaFsType_RomFs + NcaHashType_HierarchicalIntegrity + NcaEncryptionType_AesCtrEx.
    NcaFsSectionType_Nca0RomFs   = 3,   ///< NcaFsType_RomFs + NcaHashType_HierarchicalSha256 + NcaVersion_Nca0.
    NcaFsSectionType_Invalid     = 4
} NcaFsSectionType;

typedef struct {
    bool enabled;
    void *nca_ctx;                      ///< NcaContext. Used to perform NCA reads.
    NcaFsHeader header;                 ///< NCA FS section header.
    u8 section_num;
    u64 section_offset;
    u64 section_size;
    u8 section_type;                    ///< NcaFsSectionType.
    u8 encryption_type;                 ///< NcaEncryptionType.
    u8 ctr[AES_BLOCK_SIZE];             ///< Used to update the AES CTR context IV based on the desired offset.
    Aes128CtrContext ctr_ctx;
    Aes128XtsContext xts_decrypt_ctx;
    Aes128XtsContext xts_encrypt_ctx;
} NcaFsSectionContext;

typedef enum {
    NcaVersion_Nca0 = 0,
    NcaVersion_Nca2 = 2,
    NcaVersion_Nca3 = 3
} NcaVersion;

typedef struct {
    u8 aes_xts_1[AES_128_KEY_SIZE];     ///< AES-128-XTS key 0 used for NCA FS sections with NcaEncryptionType_AesXts crypto.
    u8 aes_xts_2[AES_128_KEY_SIZE];     ///< AES-128-XTS key 1 used for NCA FS sections with NcaEncryptionType_AesXts crypto.
    u8 aes_ctr[AES_128_KEY_SIZE];       ///< AES-128-CTR key used for NCA FS sections with NcaEncryptionType_AesCtr crypto.
    u8 aes_ctr_ex[AES_128_KEY_SIZE];    ///< AES-128-CTR key used for NCA FS sections with NcaEncryptionType_AesCtrEx crypto.
} NcaDecryptedKeyArea;

typedef struct {
    u8 storage_id;                                          ///< NcmStorageId.
    NcmContentStorage *ncm_storage;                         ///< Pointer to a NcmContentStorage instance. Used to read NCA data from eMMC/SD.
    u64 gamecard_offset;                                    ///< Used to read NCA data from a gamecard using a FsStorage instance when storage_id == NcmStorageId_GameCard.
    NcmContentId content_id;                                ///< Also used to read NCA data.
    char content_id_str[0x21];
    u8 hash[SHA256_HASH_SIZE];                              ///< Manually calculated (if needed).
    char hash_str[0x41];
    u8 format_version;                                      ///< NcaVersion.
    u8 content_type;                                        ///< NcmContentType. Retrieved from NcmContentInfo.
    u64 content_size;                                       ///< Retrieved from NcmContentInfo.
    u8 key_generation;                                      ///< NcaKeyGenerationOld / NcaKeyGeneration. Retrieved from the decrypted header.
    u8 id_offset;                                           ///< Retrieved from NcmContentInfo.
    bool rights_id_available;
    bool titlekey_retrieved;
    u8 titlekey[AES_128_KEY_SIZE];                          ///< Decrypted titlekey from the ticket.
    bool dirty_header;
    NcaHeader header;                                       ///< NCA header.
    NcaFsSectionContext fs_contexts[NCA_FS_HEADER_COUNT];
    NcaDecryptedKeyArea decrypted_key_area;
} NcaContext;

typedef struct {
    u64 offset; ///< New data offset (relative to the start of the NCA content file).
    u64 size;   ///< New data size.
    u8 *data;   ///< New data.
} NcaHashDataPatch;

typedef struct {
    NcmContentId content_id;
    u32 hash_region_count;
    NcaHashDataPatch hash_region_patch[NCA_HIERARCHICAL_SHA256_MAX_REGION_COUNT];
} NcaHierarchicalSha256Patch;

typedef struct {
    NcmContentId content_id;
    NcaHashDataPatch hash_level_patch[NCA_IVFC_LEVEL_COUNT];
} NcaHierarchicalIntegrityPatch;

/// Functions to control the internal heap buffer used by NCA FS section crypto operations.
/// Must be called at startup.
bool ncaAllocateCryptoBuffer(void);
void ncaFreeCryptoBuffer(void);

/// Initializes a NCA context.
/// If 'storage_id' == NcmStorageId_GameCard, the 'hfs_partition_type' argument must be a valid GameCardHashFileSystemPartitionType value.
/// If the NCA holds a populated Rights ID field, and if the Ticket element pointed to by 'tik' hasn't been filled, ticket data will be retrieved.
/// If ticket data can't be retrieved, the context will still be initialized, but anything that involves working with encrypted NCA FS section blocks won't be possible (e.g. ncaReadFsSection()).
bool ncaInitializeContext(NcaContext *out, u8 storage_id, u8 hfs_partition_type, const NcmContentInfo *content_info, Ticket *tik);

/// Reads raw encrypted data from a NCA using an input context, previously initialized by ncaInitializeContext().
/// Input offset must be relative to the start of the NCA content file.
bool ncaReadContentFile(NcaContext *ctx, void *out, u64 read_size, u64 offset);

/// Reads decrypted data from a NCA FS section using an input context.
/// Input offset must be relative to the start of the NCA FS section.
/// If dealing with Patch RomFS sections, this function should only be used when *not* reading BKTR AesCtrEx storage data. Use ncaReadAesCtrExStorageFromBktrSection() for that.
bool ncaReadFsSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset);

/// Reads decrypted BKTR AesCtrEx storage data from a NCA Patch RomFS section using an input context and a AesCtrEx CTR value.
/// Input offset must be relative to the start of the NCA FS section.
bool ncaReadAesCtrExStorageFromBktrSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset, u32 ctr_val);

/// Returns a pointer to a heap-allocated buffer used to encrypt the input plaintext data, based on the encryption type used by the input NCA FS section, as well as its offset and size.
/// Input offset must be relative to the start of the NCA FS section.
/// Output size and offset are guaranteed to be aligned to the AES sector size used by the encryption type from the FS section.
/// Output offset is relative to the start of the NCA content file, making it easier to use the output encrypted block to seamlessly replace data while dumping a NCA.
/// This function isn't compatible with Patch RomFS sections.
void *ncaGenerateEncryptedFsSectionBlock(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, u64 *out_block_size, u64 *out_block_offset);

/// Generates HierarchicalSha256 FS section patch data, which can be used to seamlessly replace NCA data.
/// Input offset must be relative to the start of the last HierarchicalSha256 hash region (actual underlying FS).
/// Bear in mind that this function recalculates both the NcaHashData block master hash and the NCA FS header hash from the NCA header, and enables the 'dirty_header' flag from the NCA context.
/// As such, this function is not designed to generate more than one patch per HierarchicalSha256 FS section.
bool ncaGenerateHierarchicalSha256Patch(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, NcaHierarchicalSha256Patch *out);

/// Overwrites block(s) from a buffer holding raw NCA data using previously initialized NcaContext and NcaHierarchicalSha256Patch.
/// 'buf_offset' must hold the raw NCA offset where the data stored in 'buf' was read from.
void ncaWriteHierarchicalSha256PatchToMemoryBuffer(NcaContext *ctx, NcaHierarchicalSha256Patch *patch, void *buf, u64 buf_size, u64 buf_offset);

/// Generates HierarchicalIntegrity FS section patch data, which can be used to seamlessly replace NCA data.
/// Input offset must be relative to the start of the last HierarchicalIntegrity hash level (actual underlying FS).
/// Bear in mind that this function recalculates both the NcaHashData block master hash and the NCA FS header hash from the NCA header, and enables the 'dirty_header' flag from the NCA context.
/// As such, this function is not designed to generate more than one patch per HierarchicalIntegrity FS section.
bool ncaGenerateHierarchicalIntegrityPatch(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, NcaHierarchicalIntegrityPatch *out);

/// Overwrites block(s) from a buffer holding raw NCA data using a previously initialized NcaContext and NcaHierarchicalIntegrityPatch.
/// 'buf_offset' must hold the raw NCA offset where the data stored in 'buf' was read from.
void ncaWriteHierarchicalIntegrityPatchToMemoryBuffer(NcaContext *ctx, NcaHierarchicalIntegrityPatch *patch, void *buf, u64 buf_size, u64 buf_offset);

/// Returns a pointer to a string holding the name of the provided NCA FS section type.
const char *ncaGetFsSectionTypeName(u8 section_type);









/// Removes titlekey crypto dependency from a NCA context by wiping the Rights ID from the underlying NCA header and copying the decrypted titlekey to the NCA key area.
void ncaRemoveTitlekeyCrypto(NcaContext *ctx);

/// Encrypts NCA header and NCA FS headers from a NCA context.
bool ncaEncryptHeader(NcaContext *ctx);

















/// Miscellaneous functions.

NX_INLINE void ncaSetDownloadDistributionType(NcaContext *ctx)
{
    if (!ctx || ctx->header.distribution_type == NcaDistributionType_Download) return;
    ctx->header.distribution_type = NcaDistributionType_Download;
    ctx->dirty_header = true;
}

NX_INLINE bool ncaValidateHierarchicalSha256Offsets(NcaHierarchicalSha256Data *hierarchical_sha256_data, u64 section_size)
{
    if (!hierarchical_sha256_data || !section_size || !hierarchical_sha256_data->hash_block_size || !hierarchical_sha256_data->hash_region_count || \
        hierarchical_sha256_data->hash_region_count > NCA_HIERARCHICAL_SHA256_MAX_REGION_COUNT) return false;
    
    for(u32 i = 0; i < hierarchical_sha256_data->hash_region_count; i++)
    {
        if (hierarchical_sha256_data->hash_region[i].offset >= section_size || !hierarchical_sha256_data->hash_region[i].size || \
            (hierarchical_sha256_data->hash_region[i].offset + hierarchical_sha256_data->hash_region[i].size) > section_size) return false;
    }
    
    return true;
}

NX_INLINE bool ncaValidateHierarchicalIntegrityOffsets(NcaIntegrityMetaInfo *integrity_meta_info, u64 section_size)
{
    if (!integrity_meta_info || !section_size || __builtin_bswap32(integrity_meta_info->magic) != NCA_IVFC_MAGIC || integrity_meta_info->master_hash_size != SHA256_HASH_SIZE || \
        integrity_meta_info->info_level_hash.max_level_count != NCA_IVFC_MAX_LEVEL_COUNT) return false;
    
    for(u32 i = 0; i < NCA_IVFC_LEVEL_COUNT; i++)
    {
        if (integrity_meta_info->info_level_hash.level_information[i].offset >= section_size || !integrity_meta_info->info_level_hash.level_information[i].size || \
            !integrity_meta_info->info_level_hash.level_information[i].block_order || \
            (integrity_meta_info->info_level_hash.level_information[i].offset + integrity_meta_info->info_level_hash.level_information[i].size) > section_size) return false;
    }
    
    return true;
}

NX_INLINE void ncaFreeHierarchicalSha256Patch(NcaHierarchicalSha256Patch *patch)
{
    if (!patch) return;
    
    for(u32 i = 0; i < NCA_HIERARCHICAL_SHA256_MAX_REGION_COUNT; i++)
    {
        if (patch->hash_region_patch[i].data) free(patch->hash_region_patch[i].data);
    }
    
    memset(patch, 0, sizeof(NcaHierarchicalSha256Patch));
}

NX_INLINE void ncaFreeHierarchicalIntegrityPatch(NcaHierarchicalIntegrityPatch *patch)
{
    if (!patch) return;
    
    for(u32 i = 0; i < NCA_IVFC_LEVEL_COUNT; i++)
    {
        if (patch->hash_level_patch[i].data) free(patch->hash_level_patch[i].data);
    }
    
    memset(patch, 0, sizeof(NcaHierarchicalIntegrityPatch));
}

#endif /* __NCA_H__ */
