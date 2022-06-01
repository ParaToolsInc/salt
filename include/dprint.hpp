#ifdef DEBUG_NO_WAY
#define DPRINT(__fmt, ...) printf(__fmt, ##__VA_ARGS__)
#define DPRINT0(__fmt) printf(__fmt)
#else
#define DPRINT(__fmt, ...)
#define DPRINT0(__fmt)
#endif

