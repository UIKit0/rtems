/*
 * Copyright (c) 2013-2014 embedded brains GmbH.  All rights reserved.
 *
 *  embedded brains GmbH
 *  Dornierstr. 4
 *  82178 Puchheim
 *  Germany
 *  <rtems@embedded-brains.de>
 *
 * Copyright (c) 2013 Deng Hengyi.
 *
 * The license and distribution terms for this file may be
 * found in the file LICENSE in this distribution or at
 * http://www.rtems.org/license/LICENSE.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <rtems/score/atomic.h>
#include <rtems/score/smpbarrier.h>
#include <rtems.h>
#include <limits.h>
#include <string.h>

#include "tmacros.h"

const char rtems_test_name[] = "SMPATOMIC 1";

#define MASTER_PRIORITY 1

#define WORKER_PRIORITY 2

#define CPU_COUNT 32

typedef struct {
  Atomic_Ulong stop;
  SMP_barrier_Control barrier;
  size_t worker_count;
  rtems_id stop_worker_timer_id;
  Atomic_Uint atomic_int_value;
  Atomic_Ulong atomic_value;
  unsigned long per_worker_value[CPU_COUNT];
  unsigned long normal_value;
  char unused_space_for_cache_line_separation[128];
  unsigned long second_value;
  Atomic_Flag global_flag;
} test_context;

typedef struct {
  void (*init)(test_context *ctx);
  void (*body)(test_context *ctx, size_t worker_index);
  void (*fini)(test_context *ctx);
} test_case;

static test_context test_instance = {
  .stop = ATOMIC_INITIALIZER_ULONG(0),
  .barrier = SMP_BARRIER_CONTROL_INITIALIZER
};

static bool stop(test_context *ctx)
{
  return _Atomic_Load_ulong(&ctx->stop, ATOMIC_ORDER_RELAXED) != 0;
}

static bool is_master_worker(size_t worker_index)
{
  return worker_index == 0;
}

static void test_fini(
  test_context *ctx,
  const char *test,
  bool atomic
)
{
  unsigned long expected_value = 0;
  unsigned long actual_value;
  size_t worker_index;

  printf("=== atomic %s test case ===\n", test);

  for (worker_index = 0; worker_index < ctx->worker_count; ++worker_index) {
    unsigned long worker_value = ctx->per_worker_value[worker_index];

    expected_value += worker_value;

    printf(
      "worker %zu value: %lu\n",
      worker_index,
      worker_value
    );
  }

  if (atomic) {
    actual_value = _Atomic_Load_ulong(&ctx->atomic_value, ATOMIC_ORDER_RELAXED);
  } else {
    actual_value = ctx->normal_value;
  }

  printf(
    "atomic value: expected = %lu, actual = %lu\n",
    expected_value,
    actual_value
  );

  rtems_test_assert(expected_value == actual_value);
}

static void test_atomic_add_init(test_context *ctx)
{
  _Atomic_Init_ulong(&ctx->atomic_value, 0);
}

static void test_atomic_add_body(test_context *ctx, size_t worker_index)
{
  unsigned long counter = 0;

  while (!stop(ctx)) {
    ++counter;
    _Atomic_Fetch_add_ulong(&ctx->atomic_value, 1, ATOMIC_ORDER_RELAXED);
  }

  ctx->per_worker_value[worker_index] = counter;
}

static void test_atomic_add_fini(test_context *ctx)
{
  test_fini(ctx, "add", true);
}

static void test_atomic_flag_init(test_context *ctx)
{
  _Atomic_Flag_clear(&ctx->global_flag, ATOMIC_ORDER_RELEASE);
  ctx->normal_value = 0;
}

static void test_atomic_flag_body(test_context *ctx, size_t worker_index)
{
  unsigned long counter = 0;

  while (!stop(ctx)) {
    while (_Atomic_Flag_test_and_set(&ctx->global_flag, ATOMIC_ORDER_ACQUIRE)) {
      /* Wait */
    }

    ++counter;
    ++ctx->normal_value;

    _Atomic_Flag_clear(&ctx->global_flag, ATOMIC_ORDER_RELEASE);
  }

  ctx->per_worker_value[worker_index] = counter;
}

static void test_atomic_flag_fini(test_context *ctx)
{
  test_fini(ctx, "flag", false);
}

static void test_atomic_sub_init(test_context *ctx)
{
  _Atomic_Init_ulong(&ctx->atomic_value, 0);
}

static void test_atomic_sub_body(test_context *ctx, size_t worker_index)
{
  unsigned long counter = 0;

  while (!stop(ctx)) {
    --counter;
    _Atomic_Fetch_sub_ulong(&ctx->atomic_value, 1, ATOMIC_ORDER_RELAXED);
  }

  ctx->per_worker_value[worker_index] = counter;
}

static void test_atomic_sub_fini(test_context *ctx)
{
  test_fini(ctx, "sub", true);
}

static void test_atomic_compare_exchange_init(test_context *ctx)
{
  _Atomic_Init_ulong(&ctx->atomic_value, 0);
  ctx->normal_value = 0;
}

static void test_atomic_compare_exchange_body(test_context *ctx, size_t worker_index)
{
  unsigned long counter = 0;

  while (!stop(ctx)) {
    bool success;

    do {
      unsigned long zero = 0;

      success = _Atomic_Compare_exchange_ulong(
        &ctx->atomic_value,
        &zero,
        1,
        ATOMIC_ORDER_ACQUIRE,
        ATOMIC_ORDER_RELAXED
      );
    } while (!success);

    ++counter;
    ++ctx->normal_value;

    _Atomic_Store_ulong(&ctx->atomic_value, 0, ATOMIC_ORDER_RELEASE);
  }

  ctx->per_worker_value[worker_index] = counter;
}

static void test_atomic_compare_exchange_fini(test_context *ctx)
{
  test_fini(ctx, "compare exchange", false);
}

static void test_atomic_or_and_init(test_context *ctx)
{
  _Atomic_Init_ulong(&ctx->atomic_value, 0);
}

static void test_atomic_or_and_body(test_context *ctx, size_t worker_index)
{
  unsigned long the_bit = 1UL << worker_index;
  unsigned long current_bit = 0;

  while (!stop(ctx)) {
    unsigned long previous;

    if (current_bit != 0) {
      previous = _Atomic_Fetch_and_ulong(
        &ctx->atomic_value,
        ~the_bit,
        ATOMIC_ORDER_RELAXED
      );
      current_bit = 0;
    } else {
      previous = _Atomic_Fetch_or_ulong(
        &ctx->atomic_value,
        the_bit,
        ATOMIC_ORDER_RELAXED
      );
      current_bit = the_bit;
    }

    rtems_test_assert((previous & the_bit) != current_bit);
  }

  ctx->per_worker_value[worker_index] = current_bit;
}

static void test_atomic_or_and_fini(test_context *ctx)
{
  test_fini(ctx, "or/and", true);
}

static void test_atomic_fence_init(test_context *ctx)
{
  ctx->normal_value = 0;
  ctx->second_value = 0;
  _Atomic_Fence(ATOMIC_ORDER_RELEASE);
}

static void test_atomic_fence_body(test_context *ctx, size_t worker_index)
{
  if (is_master_worker(worker_index)) {
    unsigned long counter = 0;

    while (!stop(ctx)) {
      ++counter;
      ctx->normal_value = counter;
      _Atomic_Fence(ATOMIC_ORDER_RELEASE);
      ctx->second_value = counter;
    }
  } else {
    while (!stop(ctx)) {
      unsigned long n;
      unsigned long s;

      s = ctx->second_value;
      _Atomic_Fence(ATOMIC_ORDER_ACQUIRE);
      n = ctx->normal_value;

      rtems_test_assert(n - s < LONG_MAX);
    }
  }
}

static void test_atomic_fence_fini(test_context *ctx)
{
  printf(
    "=== atomic fence test case ===\n"
    "normal value = %lu, second value = %lu\n",
    ctx->normal_value,
    ctx->second_value
  );
}

static const test_case test_cases[] = {
  {
    test_atomic_add_init,
    test_atomic_add_body,
    test_atomic_add_fini
  }, {
    test_atomic_flag_init,
    test_atomic_flag_body,
    test_atomic_flag_fini
  }, {
    test_atomic_sub_init,
    test_atomic_sub_body,
    test_atomic_sub_fini
  }, {
    test_atomic_compare_exchange_init,
    test_atomic_compare_exchange_body,
    test_atomic_compare_exchange_fini
  }, {
    test_atomic_or_and_init,
    test_atomic_or_and_body,
    test_atomic_or_and_fini
  }, {
    test_atomic_fence_init,
    test_atomic_fence_body,
    test_atomic_fence_fini
  },
};

#define TEST_COUNT RTEMS_ARRAY_SIZE(test_cases)

static void stop_worker_timer(rtems_id timer_id, void *arg)
{
  test_context *ctx = arg;

  _Atomic_Store_ulong(&ctx->stop, 1, ATOMIC_ORDER_RELAXED);
}

static void start_worker_stop_timer(test_context *ctx)
{
  rtems_status_code sc;

  _Atomic_Store_ulong(&ctx->stop, 0, ATOMIC_ORDER_RELEASE);

  sc = rtems_timer_fire_after(
    ctx->stop_worker_timer_id,
    rtems_clock_get_ticks_per_second(),
    stop_worker_timer,
    ctx
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
}

static void run_tests(test_context *ctx, size_t worker_index)
{
  SMP_barrier_State bs = SMP_BARRIER_STATE_INITIALIZER;
  size_t test;

  for (test = 0; test < TEST_COUNT; ++test) {
    const test_case *tc = &test_cases[test];

    if (is_master_worker(worker_index)) {
      start_worker_stop_timer(ctx);
      (*tc->init)(ctx);
    }

    _SMP_barrier_Wait(&ctx->barrier, &bs, ctx->worker_count);

    (*tc->body)(ctx, worker_index);

    _SMP_barrier_Wait(&ctx->barrier, &bs, ctx->worker_count);

    if (is_master_worker(worker_index)) {
      (*tc->fini)(ctx);
    }
  }
}

static void worker_task(size_t worker_index)
{
  test_context *ctx = &test_instance;

  run_tests(ctx, worker_index);

  (void) rtems_task_suspend(RTEMS_SELF);
  rtems_test_assert(0);
}

static void test(void)
{
  test_context *ctx = &test_instance;
  rtems_status_code sc;
  size_t worker_index;

  ctx->worker_count = rtems_get_processor_count();

  sc = rtems_timer_create(
    rtems_build_name('S', 'T', 'O', 'P'),
    &ctx->stop_worker_timer_id
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  for (worker_index = 1; worker_index < ctx->worker_count; ++worker_index) {
    rtems_id worker_id;

    sc = rtems_task_create(
      rtems_build_name('W', 'O', 'R', 'K'),
      WORKER_PRIORITY,
      RTEMS_MINIMUM_STACK_SIZE,
      RTEMS_DEFAULT_MODES,
      RTEMS_DEFAULT_ATTRIBUTES,
      &worker_id
    );
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);

    sc = rtems_task_start(worker_id, worker_task, worker_index);
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  }

  run_tests(ctx, 0);
}

static void Init(rtems_task_argument arg)
{
  TEST_BEGIN();

  test();

  TEST_END();
  rtems_test_exit(0);
}

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER

#define CONFIGURE_SMP_APPLICATION

#define CONFIGURE_SMP_MAXIMUM_PROCESSORS CPU_COUNT

#define CONFIGURE_MAXIMUM_TASKS CPU_COUNT

#define CONFIGURE_MAXIMUM_TIMERS 1

#define CONFIGURE_INIT_TASK_PRIORITY MASTER_PRIORITY
#define CONFIGURE_INIT_TASK_INITIAL_MODES RTEMS_DEFAULT_MODES
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_DEFAULT_ATTRIBUTES

#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE

#define CONFIGURE_INIT

#include <rtems/confdefs.h>
