#ifndef MASK_RING_BUFFER_H
#define MASK_RING_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

/**
 * @file ring_buffer.h
 * @brief Bounded in-memory event log used by MASK for short-term reasoning state.
 */

#define MASK_MEMORY_ROLE_MAX 16
#define MASK_MEMORY_TEXT_MAX 512

/**
 * @brief Single observation stored in the agent memory ring.
 */
struct mask_memory_entry {
    uint64_t timestamp_ms;
    char role[MASK_MEMORY_ROLE_MAX];
    char text[MASK_MEMORY_TEXT_MAX];
};

/**
 * @brief Fixed-capacity, mutex-protected ring buffer for bounded memory.
 *
 * The backing array is allocated at initialization and reused until shutdown.
 * Once the buffer is full, the oldest entries are overwritten.
 */
struct mask_ring_buffer {
    struct mask_memory_entry *entries;
    size_t capacity;
    size_t next_write;
    size_t count;
    pthread_mutex_t lock;
};

/**
 * @brief Initializes a ring buffer with a fixed capacity.
 *
 * @param rb Ring buffer to initialize.
 * @param capacity Number of memory entries to store.
 * @return MASK_OK on success, MASK_ERR on allocation or mutex failure.
 */
int mask_ring_buffer_init(struct mask_ring_buffer *rb, size_t capacity);

/**
 * @brief Releases resources owned by the ring buffer.
 * @param rb Ring buffer to destroy.
 */
void mask_ring_buffer_destroy(struct mask_ring_buffer *rb);

/**
 * @brief Appends a new memory entry to the ring, sanitizing control characters.
 *
 * @param rb Target ring buffer.
 * @param role Source or category label for the observation.
 * @param text Message text to store. It is truncated to fit the fixed slot size.
 */
void mask_ring_buffer_push(struct mask_ring_buffer *rb, const char *role, const char *text);

/**
 * @brief Copies the most recent entries into a caller-supplied array.
 *
 * @param rb Ring buffer to read from.
 * @param out Destination buffer for copied entries.
 * @param max_out Maximum number of entries to copy.
 * @return Number of entries actually copied.
 */
size_t mask_ring_buffer_snapshot(struct mask_ring_buffer *rb,
                                  struct mask_memory_entry *out,
                                  size_t max_out);

#endif /* MASK_RING_BUFFER_H */
