#define MICROPY_TASK_STACK_SIZE             (24 * 1024)
#define MICROPY_PY_SYS_EXC_INFO             (1)

#include <mpconfigport.h>

#undef MICROPY_FLOAT_IMPL
#define MICROPY_FLOAT_IMPL                  (MICROPY_FLOAT_IMPL_DOUBLE)

#undef MICROPY_PY_MACHINE_ADC
#undef MICROPY_PY_MACHINE_ADC_BLOCK
