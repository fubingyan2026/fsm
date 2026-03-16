/**
 * @file    fsm_hsm_test.c
 * @brief   FSM v2.1 层次化状态机（HSM）全功能验证测试
 *
 * 测试场景：电机控制器状态机（三层层级）
 *
 * 层级结构：
 *
 *   TOP（虚根，depth=0，不分配 handler，作为 transition 兜底层）
 *   ├── OPERATIONAL（depth=1，复合状态，管理正常运行逻辑）
 *   │   ├── IDLE（depth=2，叶子，等待启动指令）
 *   │   └── RUN（depth=2，叶子，运行中）
 *   └── FAULT（depth=1，叶子，故障处理）
 *
 * Transition 配置：
 *   TOP       → FAULT       : NULL（无条件，所有子状态继承此故障转换）
 *   IDLE      → RUN         : cond_run_ok（条件：无故障且收到启动事件）
 *   RUN       → IDLE        : NULL（无条件，收到停止指令）
 *   OPERATIONAL → FAULT     : 覆盖 TOP 的故障转换（条件相同，但在 OPERATIONAL 层定义）
 *   FAULT     → OPERATIONAL : NULL（恢复后返回 OPERATIONAL，进入其默认子状态）
 *
 * 验证项目：
 *   [T01] 基础层级注册（set_parent / get_depth / is_ancestor）
 *   [T02] LCA 计算正确性
 *   [T03] 转换继承：IDLE 通过父链 OPERATIONAL 继承 → FAULT 转换
 *   [T04] on_exit 退出链顺序（bottom-up）：IDLE → OPERATIONAL → (LCA=TOP 不触发)
 *   [T05] on_entry 进入链顺序（top-down）：FAULT 直接子 TOP，只触发 FAULT
 *   [T06] LCA = OPERATIONAL 时的 IDLE → RUN：只触发 exit(IDLE)、entry(RUN)
 *   [T07] 转换继承优先级：子状态条件 false 时不向父继承
 *   [T08] fsm_request_transition() 在 HSM 模式下触发正确的退出/进入链
 *   [T09] 超时 + HSM：FAULT 超时后 HSM-aware 跳转回 OPERATIONAL
 *   [T10] fsm_reset() 保留 parent 配置
 *   [T11] 循环父链检测
 *   [T12] v2.0 兼容性：FSM_ENABLE_HSM=1 下 stats、trace、event queue 仍正常工作
 */

#include "fsm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* NDEBUG を無効化する独自 assert（標準 TEST_ASSERT() は NDEBUG で無効化されるため）*/
#define TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); \
            __builtin_trap(); \
        } \
    } while (0)

/*============================================================================
 * 状态定义
 *============================================================================*/
typedef enum
{
    ST_TOP         = 0,
    ST_OPERATIONAL = 1,
    ST_IDLE        = 2,
    ST_RUN         = 3,
    ST_FAULT       = 4,
    ST_COUNT
} motor_state_t;

static const char * const STATE_NAMES[] =
{
    "TOP", "OPERATIONAL", "IDLE", "RUN", "FAULT"
};

/*============================================================================
 * 事件定义
 *============================================================================*/
#define EV_START  ((fsm_event_t)0x01U)
#define EV_STOP   ((fsm_event_t)0x02U)
#define EV_FAULT  ((fsm_event_t)0x03U)
#define EV_CLEAR  ((fsm_event_t)0x04U)

/*============================================================================
 * 用户数据
 *============================================================================*/
typedef struct
{
    bool     fault_active;
    bool     run_requested;
    uint32_t run_cycles;
} motor_ctx_t;

static motor_ctx_t g_motor;

/*============================================================================
 * Tick 模拟
 *============================================================================*/
static uint32_t g_tick = 0U;
static uint32_t mock_tick(void) { return g_tick; }

/*============================================================================
 * 退出/进入回调日志（用于验证顺序）
 *============================================================================*/
#define LOG_MAX 32

static char  g_log[LOG_MAX][32];
static int   g_log_count = 0;

static void log_reset(void)
{
    g_log_count = 0;
    memset(g_log, 0, sizeof(g_log));
}

static void log_push(const char *prefix, fsm_state_t state, const fsm_context_t *ctx)
{
    if (g_log_count < LOG_MAX)
    {
        snprintf(g_log[g_log_count], 32, "%s(%s)",
                 prefix, fsm_get_state_name(ctx, state));
        g_log_count++;
    }
}

static void on_exit_cb(fsm_context_t *ctx, fsm_state_t state)
{
    log_push("EXIT", state, ctx);
}

static void on_entry_cb(fsm_context_t *ctx, fsm_state_t state)
{
    log_push("ENTRY", state, ctx);
}

/*============================================================================
 * 转换条件
 *============================================================================*/
static bool cond_run_ok(const fsm_context_t *ctx)
{
    const motor_ctx_t *m = (const motor_ctx_t *)fsm_get_user_data(ctx);
    return m->run_requested && !m->fault_active;
}

/*============================================================================
 * Handler（TOP 是复合虚节点，可以没有 handler；此处给一个空实现）
 *============================================================================*/
static fsm_state_t handler_top(fsm_context_t *ctx)
{
    return fsm_get_current_state(ctx); /* 虚根不主动跳转 */
}

static fsm_state_t handler_operational(fsm_context_t *ctx)
{
    /* 复合状态本身不应被直接驻留；正常情况下应处于子状态 */
    return fsm_get_current_state(ctx);
}

static fsm_state_t handler_idle(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);
    motor_ctx_t *m = (motor_ctx_t *)fsm_get_user_data(ctx);

    if (ev == EV_FAULT || m->fault_active)
    {
        return (fsm_state_t)ST_FAULT;
    }

    if ((ev == EV_START) && m->run_requested && !m->fault_active)
    {
        return (fsm_state_t)ST_RUN;
    }

    return (fsm_state_t)ST_IDLE;
}

static fsm_state_t handler_run(fsm_context_t *ctx)
{
    fsm_event_t  ev = fsm_get_current_event(ctx);
    motor_ctx_t *m  = (motor_ctx_t *)fsm_get_user_data(ctx);

    m->run_cycles++;

    if (ev == EV_FAULT || m->fault_active)
    {
        return (fsm_state_t)ST_FAULT;
    }

    if (ev == EV_STOP)
    {
        m->run_requested = false;
        return (fsm_state_t)ST_IDLE;
    }

    return (fsm_state_t)ST_RUN;
}

static fsm_state_t handler_fault(fsm_context_t *ctx)
{
    fsm_event_t  ev = fsm_get_current_event(ctx);
    motor_ctx_t *m  = (motor_ctx_t *)fsm_get_user_data(ctx);

    if (ev == EV_CLEAR)
    {
        m->fault_active = false;
        return (fsm_state_t)ST_OPERATIONAL;
    }

    return (fsm_state_t)ST_FAULT;
}

/*============================================================================
 * 测试辅助：构建标准电机状态机
 *============================================================================*/
static fsm_context_t g_fsm;

static void setup_motor_fsm(void)
{
    fsm_ret_t r;

    memset(&g_motor, 0, sizeof(g_motor));
    g_tick = 0U;
    log_reset();

    r = fsm_init(&g_fsm, (fsm_state_t)ST_IDLE, &g_motor);
    TEST_ASSERT(r == FSM_OK);

    /* 状态名称 */
    r = fsm_set_state_names(&g_fsm, STATE_NAMES, (uint8_t)ST_COUNT);
    TEST_ASSERT(r == FSM_OK);

    /* Tick 源 */
    r = fsm_set_tick_fn(&g_fsm, mock_tick);
    TEST_ASSERT(r == FSM_OK);

    /* 回调 */
    r = fsm_set_callbacks(&g_fsm, on_entry_cb, on_exit_cb);
    TEST_ASSERT(r == FSM_OK);

    /* Handler 注册 */
    TEST_ASSERT(fsm_register_handler(&g_fsm, ST_TOP,         handler_top)         == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_fsm, ST_OPERATIONAL, handler_operational) == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_fsm, ST_IDLE,        handler_idle)        == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_fsm, ST_RUN,         handler_run)         == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_fsm, ST_FAULT,       handler_fault)       == FSM_OK);

    /* Transition 注册
     *
     * TOP 层（被 IDLE/RUN 通过父链继承）：
     *   TOP → FAULT：无条件，任何子状态遇到 fault 都能跳过来
     *
     * IDLE 层：
     *   IDLE → RUN：有条件（cond_run_ok）
     *
     * RUN 层：
     *   RUN → IDLE：无条件
     *
     * FAULT 层：
     *   FAULT → OPERATIONAL：无条件（恢复）
     */
    TEST_ASSERT(fsm_add_transition(&g_fsm, ST_TOP,   ST_FAULT,       NULL)       == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_fsm, ST_IDLE,  ST_RUN,         cond_run_ok) == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_fsm, ST_RUN,   ST_IDLE,        NULL)       == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_fsm, ST_FAULT, ST_OPERATIONAL, NULL)       == FSM_OK);

    /* HSM 层级：parent_state 配置
     *
     *   TOP         → FSM_HSM_NO_PARENT（根）
     *   OPERATIONAL → TOP
     *   IDLE        → OPERATIONAL
     *   RUN         → OPERATIONAL
     *   FAULT       → TOP
     */
    TEST_ASSERT(fsm_hsm_set_parent(&g_fsm, ST_TOP,         FSM_HSM_NO_PARENT)       == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_fsm, ST_OPERATIONAL, (fsm_state_t)ST_TOP)     == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_fsm, ST_IDLE,        (fsm_state_t)ST_OPERATIONAL) == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_fsm, ST_RUN,         (fsm_state_t)ST_OPERATIONAL) == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_fsm, ST_FAULT,       (fsm_state_t)ST_TOP)     == FSM_OK);

    /* 超时：FAULT 超过 500 tick 自动恢复 */
    TEST_ASSERT(fsm_add_timeout(&g_fsm, ST_FAULT, 500U, (fsm_state_t)ST_OPERATIONAL) == FSM_OK);
}

/*============================================================================
 * T01：层级深度与祖先关系
 *============================================================================*/
static void test_t01_hierarchy_meta(void)
{
    setup_motor_fsm();

    /* 深度 */
    TEST_ASSERT(fsm_hsm_get_depth(&g_fsm, ST_TOP)         == 0U);
    TEST_ASSERT(fsm_hsm_get_depth(&g_fsm, ST_OPERATIONAL) == 1U);
    TEST_ASSERT(fsm_hsm_get_depth(&g_fsm, ST_IDLE)        == 2U);
    TEST_ASSERT(fsm_hsm_get_depth(&g_fsm, ST_RUN)         == 2U);
    TEST_ASSERT(fsm_hsm_get_depth(&g_fsm, ST_FAULT)       == 1U);

    /* 祖先关系 */
    TEST_ASSERT(fsm_hsm_is_ancestor(&g_fsm, ST_TOP,         ST_IDLE)        == true);
    TEST_ASSERT(fsm_hsm_is_ancestor(&g_fsm, ST_OPERATIONAL, ST_IDLE)        == true);
    TEST_ASSERT(fsm_hsm_is_ancestor(&g_fsm, ST_OPERATIONAL, ST_RUN)         == true);
    TEST_ASSERT(fsm_hsm_is_ancestor(&g_fsm, ST_TOP,         ST_FAULT)       == true);
    TEST_ASSERT(fsm_hsm_is_ancestor(&g_fsm, ST_OPERATIONAL, ST_FAULT)       == false); /* FAULT 在 TOP 下 */
    TEST_ASSERT(fsm_hsm_is_ancestor(&g_fsm, ST_IDLE,        ST_RUN)         == false); /* 兄弟 */
    TEST_ASSERT(fsm_hsm_is_ancestor(&g_fsm, ST_IDLE,        ST_IDLE)        == true);  /* 自身 */

    printf("[T01] PASS: 层级深度与祖先关系\n");
}

/*============================================================================
 * T02：LCA 计算
 *============================================================================*/
static void test_t02_lca(void)
{
    setup_motor_fsm();

    /* IDLE ↔ RUN：兄弟，LCA = OPERATIONAL */
    TEST_ASSERT(fsm_hsm_get_lca(&g_fsm, ST_IDLE, ST_RUN)         == (fsm_state_t)ST_OPERATIONAL);

    /* IDLE ↔ FAULT：IDLE 在 OPERATIONAL 下，FAULT 在 TOP 下，LCA = TOP */
    TEST_ASSERT(fsm_hsm_get_lca(&g_fsm, ST_IDLE, ST_FAULT)       == (fsm_state_t)ST_TOP);

    /* RUN ↔ FAULT：同上 */
    TEST_ASSERT(fsm_hsm_get_lca(&g_fsm, ST_RUN, ST_FAULT)        == (fsm_state_t)ST_TOP);

    /* OPERATIONAL ↔ FAULT：均在 TOP 下，LCA = TOP */
    TEST_ASSERT(fsm_hsm_get_lca(&g_fsm, ST_OPERATIONAL, ST_FAULT) == (fsm_state_t)ST_TOP);

    /* 自身 ↔ 自身 = 自身 */
    TEST_ASSERT(fsm_hsm_get_lca(&g_fsm, ST_IDLE, ST_IDLE)        == (fsm_state_t)ST_IDLE);

    /* 祖先 ↔ 子孙：LCA = 祖先 */
    TEST_ASSERT(fsm_hsm_get_lca(&g_fsm, ST_TOP, ST_IDLE)         == (fsm_state_t)ST_TOP);
    TEST_ASSERT(fsm_hsm_get_lca(&g_fsm, ST_OPERATIONAL, ST_RUN)  == (fsm_state_t)ST_OPERATIONAL);

    printf("[T02] PASS: LCA 计算\n");
}

/*============================================================================
 * T03：转换继承（IDLE 继承 TOP 的 → FAULT）
 *
 *   IDLE 自身 transitions 行没有 → FAULT 的条目。
 *   通过父链：IDLE → OPERATIONAL（没有）→ TOP（有，无条件）→ 允许。
 *============================================================================*/
static void test_t03_transition_inheritance(void)
{
    setup_motor_fsm();

    /* 在 IDLE 状态时，handler 返回 ST_FAULT（由故障触发） */
    /* fsm_step 的第一次调用先处理 state_changed（消费初始进入），不做转换 */
    fsm_step(&g_fsm); /* 消费初始 state_changed */

    g_motor.fault_active = true;
    fsm_post_event(&g_fsm, EV_FAULT);
    fsm_ret_t r = fsm_step(&g_fsm);

    TEST_ASSERT(r == FSM_OK);
    TEST_ASSERT(fsm_get_current_state(&g_fsm) == (fsm_state_t)ST_FAULT);

    printf("[T03] PASS: 转换继承（IDLE 通过父链继承 TOP → FAULT）\n");
}

/*============================================================================
 * T04 & T05：退出链（bottom-up）和进入链（top-down）顺序验证
 *
 *   IDLE → FAULT，LCA = TOP
 *
 *   期望退出顺序：EXIT(IDLE)、EXIT(OPERATIONAL)       （不含 TOP = LCA）
 *   期望进入顺序：ENTRY(FAULT)                         （不含 TOP = LCA）
 *============================================================================*/
static void test_t04_t05_exit_entry_order(void)
{
    setup_motor_fsm();
    log_reset();

    fsm_step(&g_fsm); /* 消费初始 state_changed（ENTRY(IDLE) 在此触发）*/
    log_reset();      /* 清掉初始进入的日志，只关心后续转换 */

    /* 触发 IDLE → FAULT */
    g_motor.fault_active = true;
    fsm_post_event(&g_fsm, EV_FAULT);
    fsm_step(&g_fsm);

    TEST_ASSERT(fsm_get_current_state(&g_fsm) == (fsm_state_t)ST_FAULT);

    /* 验证日志顺序 */
    TEST_ASSERT(g_log_count == 3);
    TEST_ASSERT(strcmp(g_log[0], "EXIT(IDLE)")        == 0);
    TEST_ASSERT(strcmp(g_log[1], "EXIT(OPERATIONAL)") == 0);
    TEST_ASSERT(strcmp(g_log[2], "ENTRY(FAULT)")      == 0);

    printf("[T04] PASS: 退出链 bottom-up（IDLE → OPERATIONAL，不含 TOP）\n");
    printf("[T05] PASS: 进入链 top-down（FAULT，不含 TOP）\n");
}

/*============================================================================
 * T06：LCA = OPERATIONAL 时（IDLE ↔ RUN）只触发直接状态的 exit/entry
 *
 *   期望退出：EXIT(IDLE)                 （OPERATIONAL = LCA，不触发）
 *   期望进入：ENTRY(RUN)
 *============================================================================*/
static void test_t06_lca_operational(void)
{
    setup_motor_fsm();
    fsm_step(&g_fsm); /* 消费初始 state_changed */
    log_reset();

    /* 触发 IDLE → RUN */
    g_motor.run_requested = true;
    fsm_post_event(&g_fsm, EV_START);
    fsm_step(&g_fsm);

    TEST_ASSERT(fsm_get_current_state(&g_fsm) == (fsm_state_t)ST_RUN);
    TEST_ASSERT(g_log_count == 2);
    TEST_ASSERT(strcmp(g_log[0], "EXIT(IDLE)")  == 0);
    TEST_ASSERT(strcmp(g_log[1], "ENTRY(RUN)")  == 0);

    printf("[T06] PASS: LCA=OPERATIONAL，仅触发直接 exit(IDLE)/entry(RUN)\n");
}

/*============================================================================
 * T07：转换继承优先级——子状态条件 false 时阻止向父继承
 *
 *   IDLE 有 → RUN 的条目（cond_run_ok），但条件返回 false（fault_active=true）。
 *   期望：转换被拒绝（不会向父继承并错误地允许）。
 *============================================================================*/
static void test_t07_inheritance_guard(void)
{
    setup_motor_fsm();
    fsm_step(&g_fsm);

    /* 有故障时，cond_run_ok 返回 false，转换应被拒绝 */
    g_motor.fault_active  = true;
    g_motor.run_requested = true;
    /* 不投递 EV_FAULT，让 handler 直接返回 ST_RUN（手动触发）*/
    /* 用 fsm_request_transition 测试 validator */
    fsm_ret_t r = fsm_request_transition(&g_fsm, (fsm_state_t)ST_RUN);
    TEST_ASSERT(r == FSM_ERROR_INVALID_TRANSITION);
    TEST_ASSERT(fsm_get_current_state(&g_fsm) == (fsm_state_t)ST_IDLE);

    printf("[T07] PASS: 子状态条件 false 时阻止向父继承\n");
}

/*============================================================================
 * T08：fsm_request_transition() 在 HSM 模式下的退出/进入链
 *
 *   从 IDLE 外部请求跳转到 FAULT，验证 exit/entry 顺序。
 *============================================================================*/
static void test_t08_request_transition_hsm(void)
{
    setup_motor_fsm();
    fsm_step(&g_fsm);
    log_reset();

    /* 外部直接请求（IDLE → FAULT，TOP 层 transition 已注册无条件） */
    fsm_ret_t r = fsm_request_transition(&g_fsm, (fsm_state_t)ST_FAULT);
    TEST_ASSERT(r == FSM_OK);
    TEST_ASSERT(fsm_get_current_state(&g_fsm) == (fsm_state_t)ST_FAULT);

    TEST_ASSERT(g_log_count == 3);
    TEST_ASSERT(strcmp(g_log[0], "EXIT(IDLE)")        == 0);
    TEST_ASSERT(strcmp(g_log[1], "EXIT(OPERATIONAL)") == 0);
    TEST_ASSERT(strcmp(g_log[2], "ENTRY(FAULT)")      == 0);

    printf("[T08] PASS: fsm_request_transition() 触发正确的 HSM exit/entry 链\n");
}

/*============================================================================
 * T09：超时 + HSM（FAULT → OPERATIONAL）
 *
 *   FAULT 超时后跳回 OPERATIONAL（通过 fsm_hsm_check_timeout 执行 HSM 转换）。
 *   期望进入链：ENTRY(OPERATIONAL)（LCA=TOP 不触发，FAULT → TOP → OPERATIONAL）
 *============================================================================*/
static void test_t09_timeout_hsm(void)
{
    setup_motor_fsm();
    fsm_step(&g_fsm);

    /* 先进入 FAULT */
    g_motor.fault_active = true;
    fsm_request_transition(&g_fsm, (fsm_state_t)ST_FAULT);
    fsm_step(&g_fsm); /* 消费 state_changed，记录 enter_tick */
    log_reset();

    /* 时间未到，不超时 */
    g_tick = 499U;
    fsm_ret_t r = fsm_step(&g_fsm);
    TEST_ASSERT(r == FSM_OK);
    TEST_ASSERT(fsm_get_current_state(&g_fsm) == (fsm_state_t)ST_FAULT);

    /* 超时触发 */
    g_tick = 501U;
    r = fsm_step(&g_fsm);
    TEST_ASSERT(r == FSM_ERROR_TIMEOUT);
    TEST_ASSERT(fsm_get_current_state(&g_fsm) == (fsm_state_t)ST_OPERATIONAL);

    /* 验证 exit/entry 链：FAULT → OPERATIONAL，LCA=TOP */
    TEST_ASSERT(g_log_count == 2);
    TEST_ASSERT(strcmp(g_log[0], "EXIT(FAULT)")          == 0);
    TEST_ASSERT(strcmp(g_log[1], "ENTRY(OPERATIONAL)")   == 0);

    printf("[T09] PASS: 超时 + HSM exit/entry 链（FAULT → OPERATIONAL）\n");
}

/*============================================================================
 * T10：fsm_reset() 保留 parent 配置
 *============================================================================*/
static void test_t10_reset_keeps_parent(void)
{
    setup_motor_fsm();

    /* 验证初始 parent 配置 */
    TEST_ASSERT(g_fsm.hsm_parent[ST_IDLE]        == (fsm_state_t)ST_OPERATIONAL);
    TEST_ASSERT(g_fsm.hsm_parent[ST_OPERATIONAL] == (fsm_state_t)ST_TOP);

    /* 执行一些转换后 reset */
    fsm_step(&g_fsm);
    g_motor.run_requested = true;
    fsm_post_event(&g_fsm, EV_START);
    fsm_step(&g_fsm); /* IDLE → RUN */

    fsm_reset(&g_fsm, (fsm_state_t)ST_IDLE);

    /* parent 配置应保留 */
    TEST_ASSERT(g_fsm.hsm_parent[ST_IDLE]        == (fsm_state_t)ST_OPERATIONAL);
    TEST_ASSERT(g_fsm.hsm_parent[ST_OPERATIONAL] == (fsm_state_t)ST_TOP);

    /* 状态应回到 IDLE */
    TEST_ASSERT(fsm_get_current_state(&g_fsm) == (fsm_state_t)ST_IDLE);

    /* 转换继承仍然有效 */
    g_motor.fault_active = true;
    fsm_step(&g_fsm); /* 消费初始 */
    fsm_post_event(&g_fsm, EV_FAULT);
    fsm_step(&g_fsm);
    TEST_ASSERT(fsm_get_current_state(&g_fsm) == (fsm_state_t)ST_FAULT);

    printf("[T10] PASS: fsm_reset() 保留 parent 配置，转换继承仍有效\n");
}

/*============================================================================
 * T11：循环父链检测
 *============================================================================*/
static void test_t11_cycle_detection(void)
{
    fsm_context_t ctx;
    fsm_init(&ctx, 0U, NULL);

    /* 正常设置：0 → 1 → 2 */
    TEST_ASSERT(fsm_hsm_set_parent(&ctx, 1U, 0U) == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&ctx, 2U, 1U) == FSM_OK);

    /* 自指：state == parent */
    TEST_ASSERT(fsm_hsm_set_parent(&ctx, 0U, 0U) == FSM_ERROR);

    /* 逆向成环：0 → 2（会导致 0 → 1 → 2 → 0 的环）*/
    TEST_ASSERT(fsm_hsm_set_parent(&ctx, 0U, 2U) == FSM_ERROR);

    printf("[T11] PASS: 循环父链检测（自指 + 逆向成环）\n");
}

/*============================================================================
 * T12：v2.0 特性在 HSM 模式下仍正常工作（stats、trace、event queue）
 *============================================================================*/
static void test_t12_v20_features_with_hsm(void)
{
    fsm_state_stats_t stats;
    fsm_trace_entry_t trace[FSM_TRACE_BUFFER_SIZE];
    uint8_t           trace_count = 0U;

    setup_motor_fsm();

    /* 执行若干转换：IDLE → RUN → IDLE → FAULT */
    fsm_step(&g_fsm);

    g_motor.run_requested = true;
    fsm_post_event(&g_fsm, EV_START);
    fsm_step(&g_fsm); /* IDLE → RUN */
    fsm_step(&g_fsm); /* 消费 state_changed */

    fsm_post_event(&g_fsm, EV_STOP);
    fsm_step(&g_fsm); /* RUN → IDLE */
    fsm_step(&g_fsm);

    g_motor.fault_active = true;
    fsm_post_event(&g_fsm, EV_FAULT);
    fsm_step(&g_fsm); /* IDLE → FAULT */

    /* ---- stats ---- */
    TEST_ASSERT(fsm_get_stats(&g_fsm, ST_IDLE, &stats) == FSM_OK);
    TEST_ASSERT(stats.enter_count >= 1U);

    TEST_ASSERT(fsm_get_stats(&g_fsm, ST_RUN, &stats) == FSM_OK);
    TEST_ASSERT(stats.enter_count == 1U);

    /* ---- trace ---- */
    TEST_ASSERT(fsm_get_trace(&g_fsm, trace, FSM_TRACE_BUFFER_SIZE, &trace_count) == FSM_OK);
    TEST_ASSERT(trace_count >= 3U); /* 至少 3 条转换：→RUN, →IDLE, →FAULT */

    /* 最后一条转换应该是 → FAULT */
    TEST_ASSERT(trace[trace_count - 1U].to_state == (fsm_state_t)ST_FAULT);

    printf("[T12] PASS: v2.0 stats/trace 在 HSM 模式下正常（stats.enter_count=%u，trace=%u 条）\n",
           (unsigned)stats.enter_count, (unsigned)trace_count);
}

/*============================================================================
 * T13：完整场景演练（带日志输出，辅助人工核查）
 *
 *   依次执行：
 *   IDLE -[start]→ RUN -[stop]→ IDLE -[fault]→ FAULT -[clear]→ OPERATIONAL
 *============================================================================*/
static void test_t13_full_scenario(void)
{
    setup_motor_fsm();
    fsm_step(&g_fsm); /* 消费初始进入 */

    printf("\n--- T13 场景日志 ---\n");
    printf("初始状态: %s\n\n", fsm_get_state_name(&g_fsm, fsm_get_current_state(&g_fsm)));

    /* Step 1: IDLE → RUN */
    log_reset();
    g_motor.run_requested = true;
    fsm_post_event(&g_fsm, EV_START);
    fsm_step(&g_fsm);
    printf("事件 EV_START → 状态: %s\n", fsm_get_state_name(&g_fsm, fsm_get_current_state(&g_fsm)));
    for (int k = 0; k < g_log_count; k++) { printf("  %s\n", g_log[k]); }
    TEST_ASSERT(fsm_get_current_state(&g_fsm) == (fsm_state_t)ST_RUN);
    fsm_step(&g_fsm);

    /* Step 2: RUN → IDLE */
    log_reset();
    fsm_post_event(&g_fsm, EV_STOP);
    fsm_step(&g_fsm);
    printf("\n事件 EV_STOP → 状态: %s\n", fsm_get_state_name(&g_fsm, fsm_get_current_state(&g_fsm)));
    for (int k = 0; k < g_log_count; k++) { printf("  %s\n", g_log[k]); }
    TEST_ASSERT(fsm_get_current_state(&g_fsm) == (fsm_state_t)ST_IDLE);
    fsm_step(&g_fsm);

    /* Step 3: IDLE → FAULT（通过继承 TOP 的 transition）*/
    log_reset();
    g_motor.fault_active = true;
    fsm_post_event(&g_fsm, EV_FAULT);
    fsm_step(&g_fsm);
    printf("\n事件 EV_FAULT → 状态: %s\n", fsm_get_state_name(&g_fsm, fsm_get_current_state(&g_fsm)));
    for (int k = 0; k < g_log_count; k++) { printf("  %s\n", g_log[k]); }
    TEST_ASSERT(fsm_get_current_state(&g_fsm) == (fsm_state_t)ST_FAULT);
    fsm_step(&g_fsm);

    /* Step 4: FAULT → OPERATIONAL（clear）*/
    log_reset();
    fsm_post_event(&g_fsm, EV_CLEAR);
    fsm_step(&g_fsm);
    printf("\n事件 EV_CLEAR → 状态: %s\n", fsm_get_state_name(&g_fsm, fsm_get_current_state(&g_fsm)));
    for (int k = 0; k < g_log_count; k++) { printf("  %s\n", g_log[k]); }
    TEST_ASSERT(fsm_get_current_state(&g_fsm) == (fsm_state_t)ST_OPERATIONAL);

    printf("--- 场景演练完成 ---\n\n");
    printf("[T13] PASS: 完整场景演练\n");
}

/*============================================================================
 * main
 *============================================================================*/
int main(void)
{
    printf("=== FSM v2.1 HSM 层次化状态机验证 ===\n\n");
    printf("编译配置：FSM_ENABLE_HSM=%d, FSM_MAX_STATES=%u\n",
           FSM_ENABLE_HSM, (unsigned)FSM_MAX_STATES);
    printf("sizeof(fsm_context_t) = %u bytes\n\n",
           (unsigned)sizeof(fsm_context_t));

    test_t01_hierarchy_meta();
    test_t02_lca();
    test_t03_transition_inheritance();
    test_t04_t05_exit_entry_order();
    test_t06_lca_operational();
    test_t07_inheritance_guard();
    test_t08_request_transition_hsm();
    test_t09_timeout_hsm();
    test_t10_reset_keeps_parent();
    test_t11_cycle_detection();
    test_t12_v20_features_with_hsm();
    test_t13_full_scenario();

    printf("\n所有测试通过 (13/13)\n");
    return 0;
}