/**
 * @file    fsm.h
 * @brief   通用有限状态机框架 - 头文件（v2.1：新增 HSM 层次化状态机）
 * @version 2.1.0
 *
 * 版本历史：
 *   v1.0  基础 FSM（邻接矩阵、handler、condition）
 *   v2.0  工业/车规加固（volatile、MISRA enum、重入保护、超时、事件队列、统计、轨迹）
 *   v2.1  层次化状态机扩展（HSM）
 *           - parent_state 父链注册
 *           - 转换继承：子状态自动向父状态查找 transition
 *           - LCA 算法：精确确定 exit/entry 链的边界
 *           - on_exit 底向上，on_entry 顶向下，严格符合 UML 状态图语义
 *           - 循环父链检测、FSM_HSM_NO_PARENT 冲突静态断言
 *           - 完全向后兼容 v2.0 API（FSM_ENABLE_HSM=0 时零开销）
 *
 * 编译选项（在 Makefile 或 CMakeLists.txt 中以 -D 覆盖）：
 *   FSM_MAX_STATES        默认 8    邻接矩阵/函数表静态尺寸
 *   FSM_ENABLE_DEBUG      默认 1    状态名称查找表
 *   FSM_ENABLE_CALLBACKS  默认 1    全局 on_entry/on_exit 回调
 *   FSM_ENABLE_TIMEOUT    默认 1    状态超时自动跳转
 *   FSM_ENABLE_EVENT_QUEUE 默认 1   内置事件环形队列
 *   FSM_ENABLE_STATS      默认 1    运行时统计
 *   FSM_ENABLE_TRACE      默认 1    转换轨迹缓冲
 *   FSM_ENABLE_HSM        默认 0    层次化状态机（opt-in，行为变更较大）
 *   FSM_EVENT_QUEUE_SIZE  默认 8    必须为 2 的幂
 *   FSM_TRACE_BUFFER_SIZE 默认 16
 */

#ifndef FSM_H
#define FSM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*============================================================================
 * 版本信息
 *============================================================================*/
#define FSM_VERSION_MAJOR (2U)
#define FSM_VERSION_MINOR (1U)
#define FSM_VERSION_PATCH (0U)
#define FSM_VERSION_ENCODE(maj, min, pat) \
    (((uint32_t)(maj) << 16U) | ((uint32_t)(min) << 8U) | (uint32_t)(pat))
#define FSM_VERSION \
    FSM_VERSION_ENCODE(FSM_VERSION_MAJOR, FSM_VERSION_MINOR, FSM_VERSION_PATCH)

/*============================================================================
 * 编译期配置
 *============================================================================*/
#ifndef FSM_MAX_STATES
#define FSM_MAX_STATES (8U)
#endif
#ifndef FSM_ENABLE_DEBUG
#define FSM_ENABLE_DEBUG (1)
#endif
#ifndef FSM_ENABLE_CALLBACKS
#define FSM_ENABLE_CALLBACKS (1)
#endif
#ifndef FSM_ENABLE_TIMEOUT
#define FSM_ENABLE_TIMEOUT (1)
#endif
#ifndef FSM_ENABLE_EVENT_QUEUE
#define FSM_ENABLE_EVENT_QUEUE (1)
#endif
#ifndef FSM_ENABLE_STATS
#define FSM_ENABLE_STATS (1)
#endif
#ifndef FSM_ENABLE_TRACE
#define FSM_ENABLE_TRACE (1)
#endif
#ifndef FSM_ENABLE_HSM
#define FSM_ENABLE_HSM (1)
#endif
#ifndef FSM_EVENT_QUEUE_SIZE
#define FSM_EVENT_QUEUE_SIZE (8U)
#endif
#ifndef FSM_TRACE_BUFFER_SIZE
#define FSM_TRACE_BUFFER_SIZE (16U)
#endif

    /*============================================================================
     * 编译期断言
     *============================================================================*/
    _Static_assert(FSM_MAX_STATES >= 2U && FSM_MAX_STATES <= 254U,
                   "FSM_MAX_STATES must be in [2, 254] when HSM is used (0xFF reserved)");
#if FSM_ENABLE_EVENT_QUEUE
    _Static_assert((FSM_EVENT_QUEUE_SIZE & (FSM_EVENT_QUEUE_SIZE - 1U)) == 0U,
                   "FSM_EVENT_QUEUE_SIZE must be a power of 2");
    _Static_assert(FSM_EVENT_QUEUE_SIZE >= 2U && FSM_EVENT_QUEUE_SIZE <= 128U,
                   "FSM_EVENT_QUEUE_SIZE must be in [2, 128]");
#endif
    _Static_assert(FSM_TRACE_BUFFER_SIZE >= 2U && FSM_TRACE_BUFFER_SIZE <= 128U,
                   "FSM_TRACE_BUFFER_SIZE must be in [2, 128]");

/*============================================================================
 * 防御性断言宏
 *============================================================================*/
#ifndef NDEBUG
#if defined(__GNUC__) || defined(__clang__)
#define FSM_BREAK() __builtin_trap()
#elif defined(_MSC_VER)
#define FSM_BREAK() __debugbreak()
#else
#define FSM_BREAK()                      \
    do                                   \
    {                                    \
        (*(volatile uint32_t *)0U) = 0U; \
    } while (0)
#endif
#define FSM_ASSERT(expr, retval) \
    do                           \
    {                            \
        if (!(expr))             \
        {                        \
            FSM_BREAK();         \
            return (retval);     \
        }                        \
    } while (0)
#else
#define FSM_ASSERT(expr, retval) \
    do                           \
    {                            \
        if (!(expr))             \
        {                        \
            return (retval);     \
        }                        \
    } while (0)
#endif

    /*============================================================================
     * 基础类型
     *============================================================================*/
    typedef uint8_t fsm_state_t;
    typedef uint8_t fsm_ret_t;

/* 返回码（与 v2.0 数值完全一致） */
#define FSM_OK ((fsm_ret_t)0U)
#define FSM_ERROR ((fsm_ret_t)1U)
#define FSM_ERROR_NULL_PTR ((fsm_ret_t)2U)
#define FSM_ERROR_INVALID_STATE ((fsm_ret_t)3U)
#define FSM_ERROR_INVALID_TRANSITION ((fsm_ret_t)4U)
#define FSM_ERROR_NO_HANDLER ((fsm_ret_t)5U)
#define FSM_ERROR_FULL ((fsm_ret_t)6U)
#define FSM_ERROR_BUSY ((fsm_ret_t)7U)
#define FSM_ERROR_TIMEOUT ((fsm_ret_t)8U)

#if FSM_ENABLE_EVENT_QUEUE
    typedef uint8_t fsm_event_t;
#define FSM_EVENT_NONE ((fsm_event_t)0U)
#endif

    typedef struct fsm_context_s fsm_context_t;

    /*============================================================================
     * 函数指针类型
     *============================================================================*/
    typedef fsm_state_t (*fsm_handler_t)(fsm_context_t *ctx);
#if FSM_ENABLE_CALLBACKS
    typedef void (*fsm_on_entry_t)(fsm_context_t *ctx, fsm_state_t state);
    typedef void (*fsm_on_exit_t)(fsm_context_t *ctx, fsm_state_t state);
#endif
    typedef bool (*fsm_condition_t)(const fsm_context_t *ctx);
#if FSM_ENABLE_TIMEOUT
    typedef uint32_t (*fsm_get_tick_fn_t)(void);
#endif

    /*============================================================================
     * 无条件转换哨兵（全局唯一地址，修复 v1.x ODR 问题）
     *============================================================================*/
    extern const fsm_condition_t FSM_COND_ALWAYS;
/** @deprecated 改用 FSM_COND_ALWAYS，v3.x 移除 */
#define fsm_always_true FSM_COND_ALWAYS

/*============================================================================
 * HSM 专用定义
 *============================================================================*/
#if FSM_ENABLE_HSM
/**
 * @brief 无父状态哨兵（根状态标识）
 *
 * @details 值为 0xFF。由于 FSM_MAX_STATES <= 254，有效状态 ID 为 0..253，
 *          0xFF 永远不会与有效状态冲突。
 */
#define FSM_HSM_NO_PARENT ((fsm_state_t)0xFFU)

/**
 * @brief HSM 层级深度上限（防循环父链保护）
 *
 * @details 父链长度超过此值时判定为配置错误（循环或过深）。
 */
#define FSM_HSM_MAX_DEPTH ((uint8_t)(FSM_MAX_STATES))
#endif /* FSM_ENABLE_HSM */

/*============================================================================
 * 辅助数据结构
 *============================================================================*/
#if FSM_ENABLE_STATS
    typedef struct
    {
        uint32_t enter_count; /**< 累计进入次数                           */
        uint32_t total_ticks; /**< 累计驻留 tick                          */
        uint32_t max_ticks;   /**< 单次最长驻留 tick                      */
        uint32_t min_ticks;   /**< 单次最短驻留 tick（UINT32_MAX=未记录）  */
    } fsm_state_stats_t;
#endif

#if FSM_ENABLE_TRACE
    typedef struct
    {
        fsm_state_t from_state;
        fsm_state_t to_state;
        uint32_t tick;
#if FSM_ENABLE_EVENT_QUEUE
        fsm_event_t trigger_event;
#endif
    } fsm_trace_entry_t;
#endif

    /*============================================================================
     * 核心上下文结构体
     *============================================================================*/

    /**
     * @brief 状态机上下文（每个实例对应一份）
     *
     * 内存占用参考（全特性 + HSM，FSM_MAX_STATES=8，32 位平台）：
     *   基础字段          :   6B
     *   handlers[8]       :  32B
     *   transitions[8][8] : 256B
     *   callbacks         :   8B
     *   debug             :   6B
     *   timeout           :  50B
     *   event queue       :  12B
     *   stats[8]          : 128B
     *   trace[16]         :  64B
     *   HSM parent[8]     :   8B
     *   HSM cbs_fired     :   1B
     *   user_data         :   4B
     *   约计              : ~575B
     */
    struct fsm_context_s
    {
        /* ---- 核心运行时 ---- */
        volatile fsm_state_t current_state;
        fsm_state_t last_state;
        volatile bool state_changed;
        volatile bool in_step;

        /* ---- 静态配置表（初始化后只读，适合放 Flash）---- */
        fsm_handler_t handlers[FSM_MAX_STATES];
        fsm_condition_t transitions[FSM_MAX_STATES][FSM_MAX_STATES];

#if FSM_ENABLE_CALLBACKS
        fsm_on_entry_t on_entry;
        fsm_on_exit_t on_exit;
#endif

#if FSM_ENABLE_DEBUG
        const char *const *state_names;
        uint8_t state_count;
#endif

#if FSM_ENABLE_TIMEOUT
        fsm_get_tick_fn_t get_tick_fn;
        uint32_t state_enter_tick;
        uint32_t timeout_ticks[FSM_MAX_STATES];
        fsm_state_t timeout_target[FSM_MAX_STATES];
#endif

#if FSM_ENABLE_EVENT_QUEUE
        fsm_event_t eq_buf[FSM_EVENT_QUEUE_SIZE];
        uint8_t eq_head;
        uint8_t eq_tail;
        uint8_t eq_count;
        fsm_event_t current_event;
#endif

#if FSM_ENABLE_STATS
        fsm_state_stats_t stats[FSM_MAX_STATES];
#endif

#if FSM_ENABLE_TRACE
        fsm_trace_entry_t trace_buf[FSM_TRACE_BUFFER_SIZE];
        uint8_t trace_idx;
        uint8_t trace_count;
#endif

#if FSM_ENABLE_HSM
        /**
         * @brief 父状态查找表
         *
         * @details hsm_parent[s] = 状态 s 的父状态，FSM_HSM_NO_PARENT 表示根状态。
         *          初始化后由 fsm_hsm_set_parent() 填充。
         *          此字段是"静态配置"，fsm_reset() 保留，fsm_init() 清空。
         */
        fsm_state_t hsm_parent[FSM_MAX_STATES];

        /**
         * @brief HSM 转换回调已触发标志
         *
         * @details 在 fsm_hsm_perform_transition() 中置 true，表示本次转换的
         *          on_exit/on_entry 链已经按 HSM 语义触发完毕。
         *          fsm_handle_state_entry() 检测此标志后跳过重复触发，再清零。
         */
        bool hsm_cbs_fired;
#endif

        void *user_data;
    };

    /*============================================================================
     * 公共 API 
     *============================================================================*/

    fsm_ret_t fsm_init(fsm_context_t *ctx, fsm_state_t initial_state, void *user_data);
    fsm_ret_t fsm_register_handler(fsm_context_t *ctx, fsm_state_t state, fsm_handler_t handler);
    fsm_ret_t fsm_add_transition(fsm_context_t *ctx, fsm_state_t from_state,
                                 fsm_state_t to_state, fsm_condition_t condition);
#if FSM_ENABLE_CALLBACKS
    fsm_ret_t fsm_set_callbacks(fsm_context_t *ctx, fsm_on_entry_t on_entry, fsm_on_exit_t on_exit);
#endif
#if FSM_ENABLE_DEBUG
    fsm_ret_t fsm_set_state_names(fsm_context_t *ctx, const char *const *names, uint8_t count);
    const char *fsm_get_state_name(const fsm_context_t *ctx, fsm_state_t state);
#endif
    fsm_ret_t fsm_step(fsm_context_t *ctx);
    fsm_ret_t fsm_request_transition(fsm_context_t *ctx, fsm_state_t target_state);
    fsm_ret_t fsm_reset(fsm_context_t *ctx, fsm_state_t reset_state);

#if FSM_ENABLE_TIMEOUT
    fsm_ret_t fsm_set_tick_fn(fsm_context_t *ctx, fsm_get_tick_fn_t tick_fn);
    fsm_ret_t fsm_add_timeout(fsm_context_t *ctx, fsm_state_t state,
                              uint32_t timeout_ticks, fsm_state_t target_state);
    uint32_t fsm_get_tick_in_state(const fsm_context_t *ctx);
#endif
#if FSM_ENABLE_EVENT_QUEUE
    fsm_ret_t fsm_post_event(fsm_context_t *ctx, fsm_event_t event);
    static inline fsm_event_t fsm_get_current_event(const fsm_context_t *ctx)
    {
#if FSM_ENABLE_EVENT_QUEUE
        return (ctx != NULL) ? ctx->current_event : FSM_EVENT_NONE;
#else
        (void)ctx;
        return FSM_EVENT_NONE;
#endif
    }
    static inline uint8_t fsm_event_pending_count(const fsm_context_t *ctx)
    {
        return (ctx != NULL) ? ctx->eq_count : 0U;
    }
#endif
#if FSM_ENABLE_STATS
    fsm_ret_t fsm_get_stats(const fsm_context_t *ctx, fsm_state_t state, fsm_state_stats_t *out);
    fsm_ret_t fsm_clear_stats(fsm_context_t *ctx);
#endif
#if FSM_ENABLE_TRACE
    fsm_ret_t fsm_get_trace(const fsm_context_t *ctx, fsm_trace_entry_t *buf,
                            uint8_t buf_size, uint8_t *out_count);
    fsm_ret_t fsm_clear_trace(fsm_context_t *ctx);
#endif

/*============================================================================
 * 公共 API HSM 层次化状态机
 *============================================================================*/
#if FSM_ENABLE_HSM

    /**
     * @brief 为状态设置父状态（建立层级关系）
     *
     * @details 调用此函数后，当从 state 发起转换时，若其自身的 transitions 行
     *          找不到目标列，自动向父状态的行继承查找，直至根状态。
     *
     *          建立关系示例（IDLE 和 RUN 都在 OPERATIONAL 下）：
     *          @code
     *          fsm_hsm_set_parent(&ctx, STATE_IDLE, STATE_OPERATIONAL);
     *          fsm_hsm_set_parent(&ctx, STATE_RUN,  STATE_OPERATIONAL);
     *          fsm_hsm_set_parent(&ctx, STATE_OPERATIONAL, FSM_HSM_NO_PARENT);
     *          @endcode
     *
     *          注意：函数内部检测循环父链并拒绝（返回 FSM_ERROR）。
     *
     * @param[in,out] ctx    状态机上下文
     * @param[in]     state  要设置的子状态
     * @param[in]     parent 父状态 ID，或 FSM_HSM_NO_PARENT 表示根状态
     * @return FSM_OK / FSM_ERROR_NULL_PTR / FSM_ERROR_INVALID_STATE / FSM_ERROR（循环）
     */
    fsm_ret_t fsm_hsm_set_parent(fsm_context_t *ctx,
                                 fsm_state_t state,
                                 fsm_state_t parent);

    /**
     * @brief 获取状态在层级中的深度
     * @details 根状态（FSM_HSM_NO_PARENT）深度为 0，其直接子状态深度为 1，依此类推。
     * @return 深度值；父链超过 FSM_HSM_MAX_DEPTH 时返回 FSM_HSM_MAX_DEPTH（循环保护）
     */
    uint8_t fsm_hsm_get_depth(const fsm_context_t *ctx, fsm_state_t state);

    /**
     * @brief 计算两个状态的最低公共祖先（LCA）
     *
     * @details LCA 是用于确定转换时 exit/entry 链范围的关键值。
     *
     *          算法（O(depth)）：
     *          1. 计算两状态深度
     *          2. 将较深的状态上移至与较浅者同深
     *          3. 两者同步上移直至相遇
     *
     *          若两状态不在同一棵树中（无公共祖先），返回 FSM_HSM_NO_PARENT。
     *          此时转换仍会执行，退出链到顶，进入链从顶开始。
     *
     * @return LCA 状态 ID，或 FSM_HSM_NO_PARENT（无公共祖先）
     */
    fsm_state_t fsm_hsm_get_lca(const fsm_context_t *ctx,
                                fsm_state_t a,
                                fsm_state_t b);

    /**
     * @brief 检查 ancestor 是否是 state 的祖先（或与 state 相同）
     * @return true = ancestor 在 state 的父链上（含 state 自身）
     */
    bool fsm_hsm_is_ancestor(const fsm_context_t *ctx,
                             fsm_state_t ancestor,
                             fsm_state_t state);

#endif /* FSM_ENABLE_HSM */

    /*============================================================================
     * 状态机 API
     *============================================================================*/
    static inline uint32_t fsm_get_version(void) { return FSM_VERSION; }
    static inline fsm_state_t fsm_get_current_state(const fsm_context_t *ctx) { return ctx ? ctx->current_state : (fsm_state_t)0U; }
    static inline fsm_state_t fsm_get_last_state(const fsm_context_t *ctx) { return ctx ? ctx->last_state : (fsm_state_t)0U; }
    static inline bool fsm_is_state(const fsm_context_t *ctx, fsm_state_t s) { return ctx ? (ctx->current_state == s) : false; }
    static inline bool fsm_is_state_changed(const fsm_context_t *ctx) { return ctx ? ctx->state_changed : false; }
    static inline void *fsm_get_user_data(const fsm_context_t *ctx) { return ctx ? ctx->user_data : NULL; }

#ifdef __cplusplus
}
#endif

#endif /* FSM_H */