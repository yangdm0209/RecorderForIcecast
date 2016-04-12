#ifndef LOG_H
#define LOG_H

#include <android/log.h>
#define LOG_TAG "Dashu.Record.Core"
#define LOGI(format, args...) do{__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "[%s:%d] -> "format, __FUNCTION__, __LINE__, ##args);}while(0);
#define LOGD(format, args...) do{__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "[%s:%d] -> "format, __FUNCTION__, __LINE__, ##args);}while(0);
#define LOGW(format, args...) do{__android_log_print(ANDROID_LOG_WARN, LOG_TAG, "[%s:%d] -> "format, __FUNCTION__, __LINE__, ##args);}while(0);
#define LOGE(format, args...) do{__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "[%s:%d] -> "format, __FUNCTION__, __LINE__, ##args);}while(0);

#endif // LOG_H
