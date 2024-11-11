#ifndef UCC_EC_MACA_H_
#define UCC_EC_MACA_H_

#include "components/ec/base/ucc_ec_base.h"
#include "components/ec/ucc_ec_log.h"
#include "ucc/api/ucc_status.h"
#include "utils/arch/maca_def.h"
#include "utils/ucc_mpool.h"
#include "utils/ucc_spinlock.h"
#include <mc_runtime.h>

#define MAX_SUBTASKS 12
#define WARP_SIZE    32

// DEVICE SPECIFIC BEGIN
// CUDA only
typedef enum ucc_ec_maca_strm_task_mode
{
    UCC_EC_MACA_TASK_KERNEL,
    UCC_EC_MACA_TASK_MEM_OPS,
    UCC_EC_MACA_TASK_AUTO,
    UCC_EC_MACA_TASK_LAST,
} ucc_ec_maca_strm_task_mode_t;
// DEVICE SPECIFIC END

typedef enum ucc_ec_task_status
{
    UCC_EC_MACA_TASK_COMPLETED,
    UCC_EC_MACA_TASK_POSTED,
    UCC_EC_MACA_TASK_STARTED,
    UCC_EC_MACA_TASK_COMPLETED_ACK,
} ucc_ec_task_status_t;

typedef enum ucc_ec_maca_executor_state
{
    UCC_EC_MACA_EXECUTOR_INITIALIZED,
    UCC_EC_MACA_EXECUTOR_POSTED,
    UCC_EC_MACA_EXECUTOR_STARTED,
    UCC_EC_MACA_EXECUTOR_SHUTDOWN,
    UCC_EC_MACA_EXECUTOR_SHUTDOWN_ACK,
} ucc_ec_maca_executor_state_t;

typedef enum ucc_ec_maca_executor_mode
{
    UCC_EC_MACA_EXECUTOR_MODE_PERSISTENT,
    UCC_EC_MACA_EXECUTOR_MODE_INTERRUPTIBLE,
} ucc_ec_maca_executor_mode_t;

typedef ucc_status_t (*ucc_ec_maca_task_post_fn)(uint32_t * dev_status,
                                                 int        blocking_wait,
                                                 mcStream_t stream);

// Config should align with members in ucc_ec_maca
typedef struct ucc_ec_maca_config {
    ucc_ec_config_t              super;
    ucc_ec_maca_strm_task_mode_t strm_task_mode; // DEVICE SPECIFIC, CUDA only
    unsigned long                exec_num_workers;
    unsigned long                exec_num_threads;
    unsigned long                exec_max_tasks;
    unsigned long                exec_num_streams;
    unsigned long                reduce_num_blocks;
    int                          reduce_num_threads;
    int           use_cooperative_launch; // DEVICE SPECIFIC, CUDA only
    unsigned long exec_copy_thresh;       // DEVICE SPECIFIC, CUDA only
    // int           reduce_host_limit;      // DEVICE SPECIFIC, ROCm only
    // int           copy_host_limit;        // DEVICE SPECIFIC, ROCm only
} ucc_ec_maca_config_t;

typedef struct ucc_ec_maca {
    ucc_ec_base_t                super;
    int                          stream_initialized;
    mcStream_t                   stream;
    int                          exec_streams_initialized;
    mcStream_t *                 exec_streams;
    ucc_mpool_t                  events;
    ucc_mpool_t                  executors;
    ucc_mpool_t                  executor_interruptible_tasks;
    ucc_mpool_t                  executor_persistent_tasks;
    ucc_thread_mode_t            thread_mode;
    ucc_ec_maca_strm_task_mode_t strm_task_mode; // DEVICE SPECIFIC, CUDA only
    ucc_spinlock_t               init_spinlock;
    // ucc_mpool_t                  strm_reqs; // DEVICE SPECIFIC, ROCm only
} ucc_ec_maca_t;

typedef struct ucc_ec_maca_event {
    mcEvent_t event;
} ucc_ec_maca_event_t;

typedef struct ucc_ec_maca_stream_request {
    uint32_t   status;
    uint32_t * dev_status;
    mcStream_t stream;
} ucc_ec_maca_stream_request_t;

typedef struct ucc_ec_maca_executor_interruptible_task {
    ucc_ee_executor_task_t super;
    void *                 event;
    mcGraph_t              graph;      // DEVICE SPECIFIC, CUDA only
    mcGraphExec_t          graph_exec; // DEVICE SPECIFIC, CUDA only
} ucc_ec_maca_executor_interruptible_task_t;

// DEVICE SPECIFIC BEGIN
// CUDA only
typedef struct ucc_ec_maca_executor_persistent_task {
    ucc_ee_executor_task_t       super;
    int                          num_subtasks;
    ucc_ee_executor_task_args_t *subtasks[MAX_SUBTASKS];
} ucc_ec_maca_executor_persistent_task_t;
// DEVICE SPECIFIC END

typedef struct ucc_ec_maca_executor_task_ops {
    ucc_status_t (*task_post)(ucc_ee_executor_t *                executor,
                              const ucc_ee_executor_task_args_t *task_args,
                              ucc_ee_executor_task_t **          task);
    ucc_status_t (*task_test)(const ucc_ee_executor_task_t *task);
    ucc_status_t (*task_finalize)(ucc_ee_executor_task_t *task);
} ucc_ec_maca_executor_task_ops_t;

typedef struct ucc_ec_maca_executor {
    ucc_ee_executor_t               super;
    ucc_ec_maca_executor_mode_t     mode;
    uint64_t                        requested_ops; // DEVICE SPECIFIC, CUDA only
    ucc_ec_maca_executor_task_ops_t ops;
    ucc_spinlock_t                  tasks_lock;
    ucc_ec_maca_executor_state_t    state;
    int                             pidx;
    ucc_ee_executor_task_args_t *   tasks;
    ucc_ec_maca_executor_state_t *  dev_state;
    ucc_ee_executor_task_args_t *   dev_tasks;
    int *                           dev_pidx;
    int *                           dev_cidx;
} ucc_ec_maca_executor_t;

ucc_status_t ucc_ec_maca_event_create(void **event);

ucc_status_t ucc_ec_maca_event_destroy(void *event);

ucc_status_t ucc_ec_maca_event_post(void *ee_context, void *event);

ucc_status_t ucc_ec_maca_event_test(void *event);

extern ucc_ec_maca_t ucc_ec_maca;

#define EC_MACA_CONFIG                                                         \
    (ucc_derived_of(ucc_ec_maca.super.config, ucc_ec_maca_config_t))

#endif // UCC_EC_MACA_H_