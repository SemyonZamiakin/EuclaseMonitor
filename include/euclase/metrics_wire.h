/*
 * Euclase metrics wire format, version 1 ("EMS1").
 *
 * Euclase Monitor publishes one snapshot of all sampled metrics per update on
 * the Corundum ring EMS_RING_NAME (Windows: "Local\corundum.euclase-monitor").
 * Each snapshot is one Corundum record of type EMS_RECORD_SNAPSHOT:
 *
 *   header, 24 bytes:  "EMS1" | u32 entry count | u64 sequence | u64 unix time, ms
 *   entry:             u8 kind (0 number, 1 string) | u8 name length | u16 value length
 *                      | name bytes (UTF-8, no NUL) | value bytes
 *     number: value length 8, IEEE-754 double, little-endian; NaN = unavailable
 *     string: UTF-8 bytes, no NUL
 *
 * All integers are little-endian; entries are packed (no alignment), so read
 * multi-byte values with memcpy. Metric names are listed in docs/METRICS.md.
 * Header-only, C99 / C++11, no dependencies. MIT, Copyright (c) 2026 Euclase.
 */
#ifndef EUCLASE_METRICS_WIRE_H
#define EUCLASE_METRICS_WIRE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define EMS_RING_NAME "/euclase-monitor"
#define EMS_RECORD_SNAPSHOT 1u
#define EMS_HEADER_SIZE 24u
#define EMS_KIND_NUMBER 0u
#define EMS_KIND_STRING 1u

typedef struct ems_header {
    uint32_t count;
    uint64_t seq;
    uint64_t unix_ms;
} ems_header;

typedef struct ems_entry {
    unsigned kind;
    const char *name; /* not NUL-terminated */
    size_t name_len;
    double number;    /* kind == EMS_KIND_NUMBER */
    const char *str;  /* kind == EMS_KIND_STRING, not NUL-terminated */
    size_t str_len;
} ems_entry;

typedef struct ems_iter {
    const unsigned char *p, *end;
    uint32_t left;
} ems_iter;

/* Checks the header. Returns 1 and fills *h and *it on success, 0 on a malformed snapshot. */
static inline int ems_begin(const void *data, size_t size, ems_header *h, ems_iter *it) {
    const unsigned char *b = (const unsigned char *)data;
    if (size < EMS_HEADER_SIZE || memcmp(b, "EMS1", 4) != 0) return 0;
    memcpy(&h->count, b + 4, 4);
    memcpy(&h->seq, b + 8, 8);
    memcpy(&h->unix_ms, b + 16, 8);
    it->p = b + EMS_HEADER_SIZE;
    it->end = b + size;
    it->left = h->count;
    return 1;
}

/* Next entry. Returns 1 with *e filled, 0 at the end or on malformed data (bounds-checked). */
static inline int ems_next(ems_iter *it, ems_entry *e) {
    if (it->left == 0 || (size_t)(it->end - it->p) < 4) return 0;
    const unsigned kind = it->p[0], nlen = it->p[1];
    uint16_t vlen;
    memcpy(&vlen, it->p + 2, 2);
    if ((size_t)(it->end - it->p) < 4u + nlen + vlen) return 0;
    e->kind = kind;
    e->name = (const char *)it->p + 4;
    e->name_len = nlen;
    const unsigned char *v = it->p + 4 + nlen;
    if (kind == EMS_KIND_NUMBER) {
        if (vlen != 8) return 0;
        memcpy(&e->number, v, 8);
        e->str = NULL;
        e->str_len = 0;
    } else if (kind == EMS_KIND_STRING) {
        e->str = (const char *)v;
        e->str_len = vlen;
        e->number = 0;
    } else {
        return 0;
    }
    it->p += 4u + nlen + vlen;
    it->left--;
    return 1;
}

/* Name comparison helper: 1 if the entry is called `name`. */
static inline int ems_is(const ems_entry *e, const char *name) {
    size_t n = strlen(name);
    return e->name_len == n && memcmp(e->name, name, n) == 0;
}

#endif
