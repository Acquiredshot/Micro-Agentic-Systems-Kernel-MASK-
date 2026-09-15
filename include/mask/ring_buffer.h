#ifndef MASK_RING_BUFFER_H
#define MASK_RING_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#define MASK_MEMORY_ROLE_MAX 16
#define MASK_MEMORY_TEXT_MAX 512

struct mask_memory_entry {
    uint64_t timestamp_ms;
    char role[MASK_MEMORY_ROLE_MAX];
    char text[MASK_MEMORY_TEXT_MAX];
};

/* Fixed-arena ring buffer: backing storage is allocated once at init and
 * never grows or shrinks. Oldest entries are overwritten once full.
 * Safe for concurrent push/snapshot from multiple threads (reactor thread,
 * LLM worker threads). */
struct mask_ring_buffer {
    struct mask_memory_entry *entries;
    size_t capacity;
    size_t next_write;
    size_t count;
    pthread_mutex_t lock;
};

int mask_ring_buffer_init(struct mask_ring_buffer *rb, size_t capacity);
void mask_ring_buffer_destroy(struct mask_ring_buffer *rb);

/* Truncates text to MASK_MEMORY_TEXT_MAX - 1 if longer. */
void mask_ring_buffer_push(struct mask_ring_buffer *rb, const char *role, const char *text);

/* Copies up to max_out most recent entries, oldest-first, into out.
 * Returns the number of entries actually copied. */
size_t mask_ring_buffer_snapshot(struct mask_ring_buffer *rb,
                                  struct mask_memory_entry *out,
                                  size_t max_out);

#endif /* MASK_RING_BUFFER_H */
