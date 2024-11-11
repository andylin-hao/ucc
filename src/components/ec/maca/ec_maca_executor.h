#include "ec_maca.h"
#include "ucc/api/ucc_status.h"

ucc_status_t ucc_maca_executor_init(const ucc_ee_executor_params_t *params,
                                    ucc_ee_executor_t **executor);

ucc_status_t ucc_maca_executor_status(const ucc_ee_executor_t *executor);

ucc_status_t ucc_maca_executor_finalize(ucc_ee_executor_t *executor);

ucc_status_t ucc_maca_executor_start(ucc_ee_executor_t *executor,
                                     void *ee_context);

ucc_status_t ucc_maca_executor_stop(ucc_ee_executor_t *executor);

ucc_status_t ucc_maca_executor_task_post(ucc_ee_executor_t *executor,
                                         const ucc_ee_executor_task_args_t *task_args,
                                         ucc_ee_executor_task_t **task);

ucc_status_t ucc_maca_executor_task_test(const ucc_ee_executor_task_t *task);

ucc_status_t ucc_maca_executor_task_finalize(ucc_ee_executor_task_t *task);

/* implemented in ec_maca_executor.cu */
ucc_status_t ucc_ec_maca_persistent_kernel_start(ucc_ec_maca_executor_t *eee);

ucc_status_t ucc_ec_maca_reduce(ucc_ee_executor_task_args_t *task,
                                mcStream_t                  stream);