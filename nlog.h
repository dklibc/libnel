#ifndef _NLOG_H
#define _NLOG_H

#include <syslog.h>
#include <string.h>
#include <errno.h>

#define ERROR(frmt, ...) nlog(LOG_ERR, "%s: "frmt, __func__, ##__VA_ARGS__)
#define DEBUG(frmt, ...) nlog(LOG_DEBUG, "%s: "frmt, __func__, ##__VA_ARGS__)
#define ERRNO(frmt, ...) nlog(LOG_ERR, "%s: "frmt": %s", __func__, ##__VA_ARGS__, strerror(errno))

void nlog_init(void);
void nlog(int lvl, const char *frmt, ...);

#endif

