#include "rdp/report.h"
#include "rdp/internal.h"
#include <errno.h>
#include <stdio.h>
void rdp_report_cycle(long tid, void *held, void *requested,
                      void *const path[], size_t len)
{
    int saved = errno;
    char record[RDP_LOG_RECORD_CAP];
    size_t off = 0;
    int n;
    if (!path || !len || len > RDP_GRAPH_MAX_CYC) {
        rdp_log("[deadlock_monitor] warning unavailable: invalid cycle; diagnostics INCOMPLETE\n");
        errno = saved;
        return;
    }
    n = snprintf(record, sizeof record,
        "[WARNING] Potential Deadlock Detected (not a confirmed deadlock)\n"
        "Thread: tid=%ld\nHeld Lock: %p\nRequested Lock: %p\nCycle: ",
        tid, held, requested);
    if (n < 0 || (size_t)n >= sizeof record) goto failure;
    off = (size_t)n;
    for (size_t i = 0; i <= len; ++i) {
        n = snprintf(record + off, sizeof record - off, "%s%p",
                     i ? " -> " : "", i < len ? path[i] : requested);
        if (n < 0 || (size_t)n >= sizeof record - off) goto failure;
        off += (size_t)n;
    }
    record[off++] = '\n';
    rdp_log_record(record, off);
    errno = saved;
    return;
failure:
    rdp_log("[deadlock_monitor] warning formatting failed; diagnostics INCOMPLETE\n");
    errno = saved;
}
