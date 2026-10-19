#include "mask/ring_buffer.h"
#include "mask/common.h"

#include <stdlib.h>
#include <string.h>

/* Strips ANSI escape sequences (as produced by terminal-oriented commands
 * like `clear`) and other non-printable control characters in place, so
 * that anything reading ring buffer text -- the GUI, a future TUI, a log
 * dump -- can always treat it as plain printable text. \n and \t are kept
 * since they're meaningful in multi-line tool output. */
static void sanitize_text(char *s) {
    char *read = s;
    char *write = s;

    while (*read) {
        unsigned char c = (unsigned char)*read;

        if (c == 0x1B) { /* ESC */
            read++;
            if (*read == '[') { /* CSI: ESC '[' params intermediates final */
                read++;
                while (*read && (unsigned char)*read >= 0x30 && (unsigned char)*read <= 0x3F) read++;
                while (*read && (unsigned char)*read >= 0x20 && (unsigned char)*read <= 0x2F) read++;
                if (*read) read++; /* final byte */
            } else if (*read == ']') { /* OSC: ESC ']' ... BEL or ESC \ */
                read++;
                while (*read && *read != '\a' && *read != 0x1B) read++;
                if (*read == '\a') read++;
            } else if (*read) {
                read++; /* short two-byte escape, e.g. ESC 'c' */
            }
            continue;
        }

        if (c == '\n' || c == '\t') {
            *write++ = (char)c;
            read++;
            continue;
        }

        if (c < 0x20 || c == 0x7F) {
            read++; /* drop other control characters */
            continue;
        }

        *write++ = (char)c;
        read++;
    }

    *write = '\0';
}

int mask_ring_buffer_init(struct mask_ring_buffer *rb, size_t capacity) {
    if (capacity == 0) {
        return MASK_ERR;
    }

    rb->entries = calloc(capacity, sizeof(struct mask_memory_entry));
    if (!rb->entries) {
        return MASK_ERR;
    }

    rb->capacity = capacity;
    rb->next_write = 0;
    rb->count = 0;

    if (pthread_mutex_init(&rb->lock, NULL) != 0) {
        free(rb->entries);
        rb->entries = NULL;
        return MASK_ERR;
    }

    return MASK_OK;
}

void mask_ring_buffer_destroy(struct mask_ring_buffer *rb) {
    if (!rb) {
        return;
    }
    pthread_mutex_destroy(&rb->lock);
    free(rb->entries);
    rb->entries = NULL;
    rb->capacity = 0;
    rb->count = 0;
}

void mask_ring_buffer_push(struct mask_ring_buffer *rb, const char *role, const char *text) {
    pthread_mutex_lock(&rb->lock);

    struct mask_memory_entry *slot = &rb->entries[rb->next_write];
    slot->timestamp_ms = mask_now_ms();
    snprintf(slot->role, sizeof(slot->role), "%s", role);
    snprintf(slot->text, sizeof(slot->text), "%s", text);
    sanitize_text(slot->text);

    rb->next_write = (rb->next_write + 1) % rb->capacity;
    if (rb->count < rb->capacity) {
        rb->count++;
    }

    pthread_mutex_unlock(&rb->lock);
}

size_t mask_ring_buffer_snapshot(struct mask_ring_buffer *rb,
                                  struct mask_memory_entry *out,
                                  size_t max_out) {
    pthread_mutex_lock(&rb->lock);

    size_t n = rb->count < max_out ? rb->count : max_out;
    /* oldest entry currently stored is at next_write when the buffer has
     * wrapped at least once; when count < capacity, oldest is index 0. */
    size_t start = (rb->count < rb->capacity) ? 0 : rb->next_write;
    /* we want the n most recent, so skip forward within the logical range */
    size_t skip = rb->count - n;
    size_t first = (start + skip) % rb->capacity;

    for (size_t i = 0; i < n; i++) {
        out[i] = rb->entries[(first + i) % rb->capacity];
    }

    pthread_mutex_unlock(&rb->lock);
    return n;
}
