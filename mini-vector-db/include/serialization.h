#ifndef SERIALIZATION_H
#define SERIALIZATION_H

#include "vector_db.h"
#include <stdio.h>

/* L4: Binary Serialization Format
 *
 * Endian-aware binary format for persisting vector indices.
 * Uses network byte order (big-endian) for portability.
 *
 * File layout:
 *   [Header: magic u32, version u32, num_collections u32]
 *   [Collection 0: name str64, config..., vectors..., index...]
 *   ...
 *   [Trailer: checksum u32]
 *
 * FAISS uses mmap for I/O; we use sequential read/write for simplicity.
 * This follows the "Write-Ahead Log" principle at storage level.
 */

#define SER_MAGIC    0x56444354
#define SER_VERSION  1
#define SER_MAX_PATH 512

typedef int (*ser_read_fn)(FILE *fp, void *buf, size_t sz);
typedef int (*ser_write_fn)(FILE *fp, const void *buf, size_t sz);

/* Write u32 in big-endian */
int ser_write_u32(FILE *fp, unsigned int val);

/* Read u32 in big-endian */
int ser_read_u32(FILE *fp, unsigned int *val);

/* Write u64 in big-endian */
int ser_write_u64(FILE *fp, unsigned long long val);

/* Read u64 in big-endian */
int ser_read_u64(FILE *fp, unsigned long long *val);

/* Write f32 in IEEE 754 big-endian */
int ser_write_f32(FILE *fp, float val);

/* Read f32 in IEEE 754 big-endian */
int ser_read_f32(FILE *fp, float *val);

/* Write int32 in big-endian */
int ser_write_i32(FILE *fp, int val);

/* Read int32 in big-endian */
int ser_read_i32(FILE *fp, int *val);

/* Write string (length-prefixed: u32 + data) */
int ser_write_string(FILE *fp, const char *str);
int ser_read_string(FILE *fp, char *str, int max_len);

/* Write a Vector to file */
int ser_write_vector(FILE *fp, const Vector *v);
int ser_read_vector(FILE *fp, Vector *v);

/* Write HNSWGraph to file */
int ser_write_hnsw(FILE *fp, const HNSWGraph *graph);
int ser_read_hnsw(FILE *fp, HNSWGraph *graph);

/* Write IVFIndex to file */
int ser_write_ivf(FILE *fp, const IVFIndex *index);
int ser_read_ivf(FILE *fp, IVFIndex *index);

/* Write LSHTable to file */
int ser_write_lsh(FILE *fp, const LSHTable *table);
int ser_read_lsh(FILE *fp, LSHTable *table);

/* Compute CRC32 checksum over a buffer */
unsigned int ser_crc32(const unsigned char *data, int len);

/* File-level checksum validation */
int ser_validate_checksum(FILE *fp);

#endif