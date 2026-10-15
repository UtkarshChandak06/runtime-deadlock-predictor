#ifndef RDP_INTERNAL_H
#define RDP_INTERNAL_H
#include <stddef.h>
/* Bounded diagnostic queue. No queue/graph mutex is held during writes.
 * Errors and overflow are counted; errno is preserved. Normal traces are off. */
void rdp_log(const char *fmt, ...);
void rdp_log_record(const char *record, size_t len);
size_t rdp_log_dropped(void);
size_t rdp_log_errors(void);
int rdp_monitor_enter(void);
void rdp_monitor_leave(int previous);
/* Nonzero only after both real pthread entry points are available. */
int rdp_monitor_available(void);
#endif
