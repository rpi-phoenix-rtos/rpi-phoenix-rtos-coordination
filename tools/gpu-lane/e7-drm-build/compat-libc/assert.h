/* E7: simulates the libphoenix fix -- C11 7.2 requires <assert.h> to define static_assert. */
#include_next <assert.h>
#if !defined(__cplusplus) && !defined(static_assert)
#define static_assert _Static_assert
#endif
