#include "utils/log.h"

void debug(const char* format, ...) {
#ifdef DEBUG
	va_list _ArgList;
	_crt_va_start(_ArgList, _Format);
	vprintf_s(_Format, _ArgList);
	_crt_va_end(_ArgList);
#endif
}