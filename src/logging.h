/* Used for loggingMode */
#define JERS_LOG_DEBUG    0
#define JERS_LOG_INFO     1
#define JERS_LOG_WARNING  2
#define JERS_LOG_CRITICAL 3

void error_die(char * format, ...) __attribute__((format(printf,1,2)));
void print_msg(int level, const char * format, ...) __attribute__((format(printf,2,3)));
void _logMessage(const char * whom, int level, const char * message);
void openDaemonLog(char * logfile);
void setLogfileName(char * name);

#define print_msg_debug(...)    print_msg(JERS_LOG_DEBUG, __VA_ARGS__)
#define print_msg_info(...)     print_msg(JERS_LOG_INFO, __VA_ARGS__)
#define print_msg_warning(...)  print_msg(JERS_LOG_WARNING, __VA_ARGS__)
#define print_msg_critical(...) print_msg(JERS_LOG_CRITICAL, __VA_ARGS__)
