#ifndef CLOVER_LOG_H
#define CLOVER_LOG_H

#include <syslog.h>
#include <clover_utils.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void clv_log(s32 priority, const char *fmt, ...);

#define CLV_DEBUG LOG_DEBUG
#define CLV_INFO LOG_INFO
#define CLV_NOTICE LOG_NOTICE
#define CLV_WARNING LOG_WARNING
#define CLV_ERROR LOG_ERR

#define clv_debug(fmt, ...) do { \
	syslog(CLV_DEBUG, "[DEBUG ][%05d:%-24s] " fmt "\n", \
		__LINE__, __func__, ##__VA_ARGS__); \
	clv_log(CLV_DEBUG, "[DEBUG ][%05d:%-24s] " fmt "\n", \
		__LINE__, __func__, ##__VA_ARGS__); \
} while (0);

#define clv_info(fmt, ...) do { \
	syslog(CLV_INFO, "[INFO  ][%05d:%-24s] " fmt "\n", \
		__LINE__, __func__, ##__VA_ARGS__); \
	clv_log(CLV_INFO, "[INFO  ][%05d:%-24s] " fmt "\n", \
		__LINE__, __func__, ##__VA_ARGS__); \
} while (0);

#define clv_notice(fmt, ...) do { \
	syslog(CLV_NOTICE, "[NOTICE][%05d:%-24s] " fmt "\n", \
		__LINE__, __func__, ##__VA_ARGS__); \
	clv_log(CLV_NOTICE, "[NOTICE][%05d:%-24s] " fmt "\n", \
		__LINE__, __func__, ##__VA_ARGS__); \
} while (0);

#define clv_warn(fmt, ...) do { \
	syslog(CLV_WARNING, "[WARN  ][%05d:%-24s] " fmt "\n", \
		__LINE__, __func__, ##__VA_ARGS__); \
	clv_log(CLV_WARNING, "[WARN  ][%05d:%-24s] " fmt "\n", \
		__LINE__, __func__, ##__VA_ARGS__); \
} while (0);

#define clv_err(fmt, ...) do { \
	syslog(CLV_ERROR, "[ERROR ][%05d:%-24s] " fmt "\n", \
		__LINE__, __func__, ##__VA_ARGS__); \
	clv_log(CLV_ERROR, "[ERROR ][%05d:%-24s] " fmt "\n", \
		__LINE__, __func__, ##__VA_ARGS__); \
} while (0);

#ifdef __cplusplus
}
#endif

#endif

