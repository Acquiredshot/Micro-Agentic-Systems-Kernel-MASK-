#ifndef MASK_EVENT_EXPORT_H
#define MASK_EVENT_EXPORT_H

#include "mask/ipc.h"

/* Starts a background thread that tails the event log file and POSTs new
 * lines to the configured event_export_url. Safe to call even if no URL is
 * configured (the thread exits immediately in that case). */
void mask_event_export_start(struct mask_ipc_context *ctx);

/* Triggers an immediate threat intel feed fetch on a new thread. The fetch
 * curl()s the configured threat_feed_url, parses the response (JSON array of
 * strings or one-IOC-per-line plain text), and updates cfg->ioc_data. */
void mask_threat_feed_trigger(struct mask_ipc_context *ctx);

/* Returns ring buffer entries matching a query filter, as JSON. Used by
 * Network Guardian and PAKSHIELD to query MASK's host state. */
char *mask_query_events(struct mask_ipc_context *ctx, const char *filter_json);

#endif /* MASK_EVENT_EXPORT_H */
