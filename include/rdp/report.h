#ifndef RDP_REPORT_H
#define RDP_REPORT_H
#include <stddef.h>
#include "rdp/graph.h"
/* Enough for 1025 hexadecimal pointers, separators, and fixed metadata. */
#define RDP_LOG_RECORD_CAP (512 + (RDP_GRAPH_MAX_CYC + 1) * (2 * sizeof(void *) + 8))
void rdp_report_cycle(long tid, void *held, void *requested,
                      void *const path[], size_t len);
#endif
