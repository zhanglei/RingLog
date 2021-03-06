#ifndef __RING_LOG_H__
#define __RING_LOG_H__

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>//getpid, gettid

enum LOG_LEVEL
{
    FATAL = 1,
    ERROR,
    WARN,
    INFO,
    DEBUG,
    TRACE,
};

extern pid_t gettid();

struct utc_timer
{
    utc_timer()
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        //set _sys_acc_sec, _sys_acc_min
        _sys_acc_sec = tv.tv_sec;
        _sys_acc_min = _sys_acc_sec / 60;
        //use _sys_acc_sec calc year, mon, day, hour, min, sec
        struct tm cur_tm;
        localtime_r((time_t*)&_sys_acc_sec, &cur_tm);
        year = cur_tm.tm_year + 1900;
        mon  = cur_tm.tm_mon + 1;
        day  = cur_tm.tm_mday;
        hour  = cur_tm.tm_hour;
        min  = cur_tm.tm_min;
        sec  = cur_tm.tm_sec;
    }

    uint64_t get_curr_time(int* p_msec = NULL)
    {
        struct timeval tv;
        //get current ts
        gettimeofday(&tv, NULL);
        if (p_msec)
            *p_msec = tv.tv_usec / 1000;
        //if not in same seconds
        if ((uint32_t)tv.tv_sec != _sys_acc_sec)
        {
            sec = tv.tv_sec % 60;
            _sys_acc_sec = tv.tv_sec;
            //or if not in same minutes
            if (_sys_acc_sec / 60 != _sys_acc_min)
            {
                //use _sys_acc_sec update year, mon, day, hour, min, sec
                _sys_acc_min = _sys_acc_sec / 60;
                struct tm cur_tm;
                localtime_r((time_t*)&_sys_acc_sec, &cur_tm);
                year = cur_tm.tm_year + 1900;
                mon  = cur_tm.tm_mon + 1;
                day  = cur_tm.tm_mday;
                hour = cur_tm.tm_hour;
                min  = cur_tm.tm_min;
                //reformat utc format
                reset_utc_fmt();
            }
            else
            {
		//reformat utc format only sec
		reset_utc_fmt_sec();
	    }
        }
        return tv.tv_sec;
    }

    int year, mon, day, hour, min, sec;
    char utc_fmt[20];

private:
    void reset_utc_fmt()
    {
        snprintf(utc_fmt, 20, "%d-%02d-%02d %02d:%02d:%02d", year, mon, day, hour, min, sec);
    }
    
    void reset_utc_fmt_sec()
    {
        snprintf(utc_fmt + 17, 3, "%02d", sec);
    }

    uint64_t _sys_acc_min;
    uint64_t _sys_acc_sec;
};

class cell_buffer
{
public:
    enum buffer_status
    {
        FREE,
        FULL
    };

    cell_buffer(uint32_t len): 
    status(FREE), 
    prev(NULL), 
    next(NULL), 
    _total_len(len), 
    _used_len(0)
    {
        _data = new char[len];
        if (!_data)
        {
            fprintf(stderr, "no space to allocate _data\n");
            exit(1);
        }
    }

    uint32_t avail_len() const { return _total_len - _used_len; }

    bool empty() const { return _used_len == 0; }

    void append(const char* log_line, uint32_t len)
    {
        if (avail_len() < len)
            return ;
        memcpy(_data + _used_len, log_line, len);
        _used_len += len;
    }

    void clear()
    {
        _used_len = 0;
        status = FREE;
    }

    void persist(FILE* fp)
    {
        uint32_t wt_len = fwrite(_data, 1, _used_len, fp);
        if (wt_len != _used_len)
        {
            fprintf(stderr, "write log to disk error, wt_len %u\n", wt_len);
        }
    }

    buffer_status status;

    cell_buffer* prev;
    cell_buffer* next;

private:
    cell_buffer(const cell_buffer&);
    cell_buffer& operator=(const cell_buffer&);

    uint32_t _total_len;
    uint32_t _used_len;
    char* _data;
};

class ring_log
{
public:
    ring_log(int buff_cnt = 5, uint32_t one_buff_len = 100*1024*1024);

    ~ring_log() {} //because ring_log is global instance, so don't need destructor

    void init(const char* log_dir, const char* prog_name, int level);

    int get_level() const { return _level; }

    void persist();

    void try_append(const char* lvl, const char* format, ...);

private:
    bool decis_file(int year, int mon, int day);

    ring_log(const ring_log&);
    ring_log& operator=(const ring_log&);

    uint32_t _one_buff_len;
    int _buff_cnt;

    cell_buffer* _curr_buf;
    cell_buffer* _prst_buf;

    cell_buffer* last_buf;

    static pthread_mutex_t _mutex;
    static pthread_cond_t _cond;

    FILE* _fp;
    pid_t _pid;
    int _year, _mon, _day, _log_cnt;
    char _prog_name[128];
    char _log_dir[512];

    bool _env_ok;//if log dir ok
    int _level;
    uint64_t _lst_lts;//last can't log error time(s) if value != 0, log error happened last time
    
    utc_timer _tm;
};

extern ring_log g_log;

void* be_thdo(void* args);

#define LOG_INIT(log_dir, prog_name, level) \
    do \
    { \
        g_log.init(log_dir, prog_name, level); \
        pthread_t tid; \
        pthread_create(&tid, NULL, be_thdo, NULL); \
        pthread_detach(tid); \
    } while (0)

//format: [LEVEL][yy-mm-dd h:m:s.ms][tid]file_name:line_no(func_name):content
#define LOG_TRACE(fmt, args...) \
    do \
    { \
        if (g_log.get_level() >= TRACE) \
        { \
            g_log.try_append("[TRACE]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_DEBUG(fmt, args...) \
    do \
    { \
        if (g_log.get_level() >= DEBUG) \
        { \
            g_log.try_append("[DEBUG]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_INFO(fmt, args...) \
    do \
    { \
        if (g_log.get_level() >= INFO) \
        { \
            g_log.try_append("[INFO]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_WARN(fmt, args...) \
    do \
    { \
        if (g_log.get_level() >= WARN) \
        { \
            g_log.try_append("[WARN]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_ERROR(fmt, args...) \
    do \
    { \
        if (g_log.get_level() >= ERROR) \
        { \
            g_log.try_append("[ERROR]", "[%u]%s:%d(%s): " fmt "\n", \
                gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_FATAL(fmt, args...) \
    do \
    { \
        g_log.try_append("[FATAL]", "[%u]%s:%d(%s): " fmt "\n", \
            gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
    } while (0)

#endif
