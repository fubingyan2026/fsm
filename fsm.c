/**
 * @file    fsm.c
 * @brief   通用有限状态机框架 - 实现（v2.1：HSM 层次化状态机）
 * @version 2.1.0
 *
 * HSM 核心机制说明：
 *
 *  1. 转换继承（Transition Inheritance）
 *     fsm_is_valid_transition() 在 HSM 模式下从当前状态开始，
 *     沿父链向上逐层查找 transitions[s][target]，直到找到或到达根。
 *     这实现了"子状态未定义的转换由父状态兜底"的语义。
 *
 *  2. LCA 算法（Lowest Common Ancestor）
 *     fsm_hsm_get_lca(a, b)：通过深度均衡 + 同步上移，O(depth) 时间复杂度。
 *
 *  3. 退出链（Exit Chain）：bottom-up，从当前状态到 LCA（不含 LCA）
 *     循环：s = current; while s != LCA: on_exit(s); s = parent[s];
 *
 *  4. 进入链（Entry Chain）：top-down，从 LCA 下一层到目标状态
 *     先逆序收集路径：s = target → LCA（不含），存入栈；然后反序调用 on_entry。
 *
 *  5. 时序（Timing）
 *     HSM 的 entry/exit 链在 fsm_step() → handler 返回后立即触发（同步），
 *     而非延迟到下一次 fsm_step()。这与 flat FSM 的回调时序不同。
 *     标志 hsm_cbs_fired 通知 fsm_handle_state_entry() 跳过重复触发。
 */

#include "fsm.h"
#include <string.h>

/*============================================================================
 * FSM_COND_ALWAYS 唯一定义（修复 ODR）
 *============================================================================*/

static bool fsm_always_true_impl(const fsm_context_t *ctx)
{
    (void)ctx;
    return true;
}

const fsm_condition_t FSM_COND_ALWAYS = fsm_always_true_impl;

/*============================================================================
 * 私有工具函数
 *============================================================================*/

/**
 * @brief 验证单步转换条件（查询邻接矩阵中的 [from][target] 单格）
 *
 * @details 不走父链，只检查 from 这一行。
 *          HSM 的父链遍历由 fsm_is_valid_transition() 调用本函数多次完成。
 */
static bool fsm_check_one_transition(const fsm_context_t *ctx,
                                     fsm_state_t from,
                                     fsm_state_t target)
{
    fsm_condition_t cond = ctx->transitions[from][target];

    if (cond == NULL)
    {
        return false; /* 此行无该目标的转换 */
    }

    return cond(ctx); /* 有条件则执行；FSM_COND_ALWAYS 直接返回 true */
}

/**
 * @brief 验证从 current_state 到 target 的转换是否合法
 *
 * @details HSM 模式：沿父链逐层查找，实现转换继承。
 *          Flat 模式：只查当前状态行（与 v2.0 行为完全一致）。
 *
 *          保持当前状态（target == current）始终允许（不触发回调）。
 */
static bool fsm_is_valid_transition(const fsm_context_t *ctx,
                                    fsm_state_t target)
{
    if ((ctx == NULL) ||
        (ctx->current_state >= (fsm_state_t)FSM_MAX_STATES) ||
        (target >= (fsm_state_t)FSM_MAX_STATES))
    {
        return false;
    }

    if (ctx->current_state == target)
    {
        return true; /* self-stay 始终允许 */
    }

#if FSM_ENABLE_HSM
    /*
     * HSM 转换继承：从当前状态开始，沿父链向上，在每一层的 transitions 行
     * 中查找 target 列。找到第一个非 NULL 条目则执行条件函数。
     * 未找到则转换不存在。
     */
    {
        fsm_state_t walk = ctx->current_state;

        while (walk != FSM_HSM_NO_PARENT)
        {
            if (fsm_check_one_transition(ctx, walk, target))
            {
                return true;
            }

            /* 条目存在但条件返回 false：停止向父查找，避免绕过守卫 */
            if (ctx->transitions[walk][target] != NULL)
            {
                return false;
            }

            walk = ctx->hsm_parent[walk];
        }

        return false;
    } /* end HSM block */

#else
    /* Flat FSM：只检查当前状态行（v2.0 原始行为） */
    return fsm_check_one_transition(ctx, ctx->current_state, target);
#endif
}

/**
 * @brief 记录轨迹 + 更新 current_state + 置 state_changed
 *
 * @details 此函数不触发任何回调，仅负责状态切换和轨迹记录。
 *          回调由调用方（flat: fsm_handle_state_entry; HSM: fsm_hsm_perform_transition）负责。
 */
static void fsm_do_transition(fsm_context_t *ctx,
                              fsm_state_t next_state,
                              uint8_t trigger_ev)
{
    (void)trigger_ev;

#if FSM_ENABLE_TRACE
    {
        fsm_trace_entry_t *entry = &ctx->trace_buf[ctx->trace_idx];

        entry->from_state = ctx->current_state;
        entry->to_state = next_state;

#if FSM_ENABLE_TIMEOUT
        entry->tick = (ctx->get_tick_fn != NULL) ? ctx->get_tick_fn() : 0U;
#else
        entry->tick = 0U;
#endif

#if FSM_ENABLE_EVENT_QUEUE
        entry->trigger_event = (fsm_event_t)trigger_ev;
#endif

        ctx->trace_idx = (uint8_t)((ctx->trace_idx + 1U) % FSM_TRACE_BUFFER_SIZE);

        if (ctx->trace_count < (uint8_t)FSM_TRACE_BUFFER_SIZE)
        {
            ctx->trace_count++;
        }
    }
#endif /* FSM_ENABLE_TRACE */

    ctx->current_state = next_state;
    ctx->state_changed = true;
}

/**
 * @brief 处理状态进入的簿记工作（在 fsm_step() 开头，state_changed==true 时调用）
 *
 * @details 负责：统计更新、tick 快照更新、回调触发（Flat 模式）、清除 state_changed。
 *          HSM 模式下回调已在 fsm_hsm_perform_transition() 中触发，此处跳过。
 */
static void fsm_handle_state_entry(fsm_context_t *ctx, fsm_state_t new_state)
{
    fsm_state_t old_state = ctx->last_state;
    bool fire_cbs = true;

#if FSM_ENABLE_HSM
    if (ctx->hsm_cbs_fired)
    {
        /* HSM 模式：exit/entry 链已在转换时触发，本次跳过 */
        fire_cbs = false;
        ctx->hsm_cbs_fired = false;
    }
#endif

    /* ---- 统计：记录上一状态的驻留时长 ---- */
#if FSM_ENABLE_STATS
    if (old_state != new_state)
    {
        fsm_state_stats_t *s = &ctx->stats[old_state];

#if FSM_ENABLE_TIMEOUT
        if (ctx->get_tick_fn != NULL)
        {
            uint32_t now = ctx->get_tick_fn();
            uint32_t duration = now - ctx->state_enter_tick;

            s->total_ticks += duration;
            if (duration > s->max_ticks)
            {
                s->max_ticks = duration;
            }
            if (duration < s->min_ticks)
            {
                s->min_ticks = duration;
            }
        }
#endif

        ctx->stats[new_state].enter_count++;
    }
#endif /* FSM_ENABLE_STATS */

    /* ---- 回调（仅 Flat 模式或未触发过的情况）---- */
#if FSM_ENABLE_CALLBACKS
    if (fire_cbs && (old_state != new_state))
    {
        if (ctx->on_exit != NULL)
        {
            ctx->on_exit(ctx, old_state);
        }
        if (ctx->on_entry != NULL)
        {
            ctx->on_entry(ctx, new_state);
        }
    }
#else
    (void)fire_cbs;
#endif

    /* ---- 更新 tick 快照 ---- */
#if FSM_ENABLE_TIMEOUT
    ctx->state_enter_tick = (ctx->get_tick_fn != NULL) ? ctx->get_tick_fn() : 0U;
#endif

    /* ---- 稳定化 ---- */
    ctx->last_state = new_state;
    ctx->state_changed = false;
}

/*============================================================================
 * HSM 私有函数
 *============================================================================*/

#if FSM_ENABLE_HSM

/**
 * @brief 执行 HSM 语义的完整转换（含退出链 + 进入链）
 *
 * @details 执行顺序：
 *  1. 计算 LCA = fsm_hsm_get_lca(current, next)
 *  2. 退出链（bottom-up）：current → LCA（不含 LCA）
 *     每个状态调用 on_exit()
 *  3. 进入链（top-down）：LCA 的子节点（朝向 next 方向）→ next
 *     先逆序压栈，再顺序调用 on_entry()
 *  4. 调用 fsm_do_transition() 更新状态并记录轨迹
 *  5. 置 hsm_cbs_fired = true，通知 fsm_handle_state_entry() 跳过回调
 *
 * @note 此函数假定调用方已通过 fsm_is_valid_transition() 验证。
 */
static void fsm_hsm_perform_transition(fsm_context_t *ctx,
                                       fsm_state_t next_state,
                                       uint8_t trigger_ev)
{
    fsm_state_t lca;
    fsm_state_t s;
    fsm_state_t entry_path[FSM_MAX_STATES]; /* 进入链（逆序收集，顺序触发）*/
    uint8_t entry_count = 0U;
    uint8_t i;

    lca = fsm_hsm_get_lca(ctx, ctx->current_state, next_state);

    /* ----------------------------------------------------------------
     * 退出链：当前状态 → LCA（bottom-up，不含 LCA）
     * ---------------------------------------------------------------- */
#if FSM_ENABLE_CALLBACKS
    if (ctx->on_exit != NULL)
    {
        s = ctx->current_state;

        while ((s != lca) && (s != FSM_HSM_NO_PARENT))
        {
            ctx->on_exit(ctx, s);
            s = ctx->hsm_parent[s];
        }
    }
#endif

    /* ----------------------------------------------------------------
     * 收集进入链：next_state → LCA（bottom-up 收集，存入栈）
     *   entry_path[0] = next_state（最深，最后进入）
     *   entry_path[entry_count-1] = LCA 的直接子节点（最先进入）
     * ---------------------------------------------------------------- */
    s = next_state;

    while ((s != lca) && (s != FSM_HSM_NO_PARENT))
    {
        if (entry_count < (uint8_t)FSM_MAX_STATES)
        {
            entry_path[entry_count] = s;
            entry_count++;
        }

        s = ctx->hsm_parent[s];
    }

    /* ----------------------------------------------------------------
     * 触发进入链：top-down（entry_path 反向遍历）
     *   i = entry_count-1 → 0
     *   即先进入离 LCA 最近的状态，最后进入 next_state
     * ---------------------------------------------------------------- */
#if FSM_ENABLE_CALLBACKS
    if ((ctx->on_entry != NULL) && (entry_count > 0U))
    {
        for (i = entry_count; i > 0U; i--)
        {
            ctx->on_entry(ctx, entry_path[i - 1U]);
        }
    }
#else
    (void)entry_path;
    (void)entry_count;
    (void)i;
#endif

    /* ----------------------------------------------------------------
     * 记录轨迹 + 切换状态
     * ---------------------------------------------------------------- */
    ctx->hsm_cbs_fired = true; /* 通知 fsm_handle_state_entry() 跳过回调 */
    fsm_do_transition(ctx, next_state, trigger_ev);
}

/**
 * @brief 检查超时并执行 HSM 强制跳转（FSM_ENABLE_HSM + FSM_ENABLE_TIMEOUT）
 */
#if FSM_ENABLE_TIMEOUT
static fsm_ret_t fsm_hsm_check_timeout(fsm_context_t *ctx)
{
    fsm_state_t s = ctx->current_state;
    uint32_t threshold = ctx->timeout_ticks[s];
    uint32_t elapsed;
    fsm_state_t target;

    if ((ctx->get_tick_fn == NULL) || (threshold == 0U))
    {
        return FSM_OK;
    }

    elapsed = ctx->get_tick_fn() - ctx->state_enter_tick;

    if (elapsed < threshold)
    {
        return FSM_OK;
    }

    target = ctx->timeout_target[s];

    if (!fsm_is_valid_transition(ctx, target))
    {
        return FSM_ERROR_INVALID_TRANSITION; /* 超时目标不合法 */
    }

    fsm_hsm_perform_transition(ctx, target, (uint8_t)FSM_EVENT_NONE);
    return FSM_ERROR_TIMEOUT;
}
#endif /* FSM_ENABLE_TIMEOUT */

#endif /* FSM_ENABLE_HSM */

/*============================================================================
 * 超时检查（Flat 模式，仅在 !FSM_ENABLE_HSM 时编译）
 *============================================================================*/

#if FSM_ENABLE_TIMEOUT && !FSM_ENABLE_HSM
static fsm_ret_t fsm_check_timeout(fsm_context_t *ctx)
{
    fsm_state_t s = ctx->current_state;
    uint32_t threshold = ctx->timeout_ticks[s];
    uint32_t elapsed;
    fsm_state_t target;

    if ((ctx->get_tick_fn == NULL) || (threshold == 0U))
    {
        return FSM_OK;
    }

    elapsed = ctx->get_tick_fn() - ctx->state_enter_tick;

    if (elapsed < threshold)
    {
        return FSM_OK;
    }

    target = ctx->timeout_target[s];

    if (!fsm_is_valid_transition(ctx, target))
    {
        return FSM_ERROR_INVALID_TRANSITION; /* 超时目标不合法 */
    }

    fsm_do_transition(ctx, target, (uint8_t)FSM_EVENT_NONE);
    return FSM_ERROR_TIMEOUT;
}
#endif

/*============================================================================
 * 公共 API — v2.0 兼容实现
 *============================================================================*/

fsm_ret_t fsm_init(fsm_context_t *ctx,
                   fsm_state_t initial_state,
                   void *user_data)
{
    FSM_ASSERT(ctx != NULL, FSM_ERROR_NULL_PTR);
    FSM_ASSERT(initial_state < FSM_MAX_STATES, FSM_ERROR_INVALID_STATE);

    (void)memset(ctx, 0, sizeof(fsm_context_t));

#if FSM_ENABLE_STATS
    {
        uint8_t i;
        for (i = 0U; i < (uint8_t)FSM_MAX_STATES; i++)
        {
            ctx->stats[i].min_ticks = UINT32_MAX;
        }
    }
#endif

#if FSM_ENABLE_HSM
    {
        uint8_t i;
        for (i = 0U; i < (uint8_t)FSM_MAX_STATES; i++)
        {
            ctx->hsm_parent[i] = FSM_HSM_NO_PARENT;
        }
    }
#endif

    ctx->current_state = initial_state;
    ctx->last_state = initial_state;
    ctx->state_changed = true;
    ctx->user_data = user_data;

    return FSM_OK;
}

fsm_ret_t fsm_register_handler(fsm_context_t *ctx,
                               fsm_state_t state,
                               fsm_handler_t handler)
{
    FSM_ASSERT(ctx != NULL, FSM_ERROR_NULL_PTR);
    FSM_ASSERT(handler != NULL, FSM_ERROR_NULL_PTR);
    FSM_ASSERT(state < FSM_MAX_STATES, FSM_ERROR_INVALID_STATE);

    ctx->handlers[state] = handler;
    return FSM_OK;
}

fsm_ret_t fsm_add_transition(fsm_context_t *ctx,
                             fsm_state_t from_state,
                             fsm_state_t to_state,
                             fsm_condition_t condition)
{
    FSM_ASSERT(ctx != NULL, FSM_ERROR_NULL_PTR);
    FSM_ASSERT(from_state < FSM_MAX_STATES, FSM_ERROR_INVALID_STATE);
    FSM_ASSERT(to_state < FSM_MAX_STATES, FSM_ERROR_INVALID_STATE);

    ctx->transitions[from_state][to_state] = (condition != NULL) ? condition : FSM_COND_ALWAYS;
    return FSM_OK;
}

#if FSM_ENABLE_CALLBACKS
fsm_ret_t fsm_set_callbacks(fsm_context_t *ctx,
                            fsm_on_entry_t on_entry,
                            fsm_on_exit_t on_exit)
{
    FSM_ASSERT(ctx != NULL, FSM_ERROR_NULL_PTR);
    ctx->on_entry = on_entry;
    ctx->on_exit = on_exit;
    return FSM_OK;
}
#endif

#if FSM_ENABLE_DEBUG
fsm_ret_t fsm_set_state_names(fsm_context_t *ctx,
                              const char *const *state_names,
                              uint8_t state_count)
{
    FSM_ASSERT(ctx != NULL, FSM_ERROR_NULL_PTR);
    FSM_ASSERT(state_names != NULL, FSM_ERROR_NULL_PTR);
    ctx->state_names = state_names;
    ctx->state_count = state_count;
    return FSM_OK;
}

const char *fsm_get_state_name(const fsm_context_t *ctx, fsm_state_t state)
{
    if ((ctx != NULL) && (ctx->state_names != NULL) && (state < ctx->state_count))
    {
        return ctx->state_names[state];
    }
    return "UNKNOWN";
}
#endif

/**
 * @brief 主驱动函数
 *
 * 执行顺序：
 *  1. 重入保护
 *  2. 状态进入处理（state_changed == true 时）
 *  3. 超时检查
 *  4. 消费事件队列
 *  5. 调用 handler
 *  6. 处理转换（HSM: fsm_hsm_perform_transition; Flat: fsm_do_transition）
 */
fsm_ret_t fsm_step(fsm_context_t *ctx)
{
    fsm_state_t current_state;
    fsm_state_t next_state;
    fsm_ret_t ret;
    uint8_t trigger_ev;

    FSM_ASSERT(ctx != NULL, FSM_ERROR_NULL_PTR);

    /* ---- 1. 重入保护 ---- */
    if (ctx->in_step)
    {
        return FSM_ERROR_BUSY;
    }
    ctx->in_step = true;

    current_state = ctx->current_state;

    if (current_state >= (fsm_state_t)FSM_MAX_STATES)
    {
        ctx->in_step = false;
        return FSM_ERROR_INVALID_STATE;
    }

    /* ---- 2. 状态进入处理 ---- */
    if (ctx->state_changed)
    {
        fsm_handle_state_entry(ctx, current_state);
        current_state = ctx->current_state; /* 刷新（回调内不应改变状态，但防御性读取）*/
    }

    /* ---- 3. 超时检查 ---- */
#if FSM_ENABLE_TIMEOUT
    {
        fsm_ret_t timeout_ret;

#if FSM_ENABLE_HSM
        timeout_ret = fsm_hsm_check_timeout(ctx);
#else
        timeout_ret = fsm_check_timeout(ctx);
#endif

        if (timeout_ret != FSM_OK)
        {
            ctx->in_step = false;
            return timeout_ret;
        }
    }
#endif

    /* ---- 4. 消费事件队列 ---- */
    trigger_ev = (uint8_t)FSM_EVENT_NONE;

#if FSM_ENABLE_EVENT_QUEUE
    if (ctx->eq_count > 0U)
    {
        trigger_ev = (uint8_t)ctx->eq_buf[ctx->eq_head];
        ctx->current_event = ctx->eq_buf[ctx->eq_head];
        ctx->eq_head = (uint8_t)((ctx->eq_head + 1U) & (FSM_EVENT_QUEUE_SIZE - 1U));
        ctx->eq_count--;
    }
    else
    {
        ctx->current_event = FSM_EVENT_NONE;
    }
#endif

    /* ---- 5. 调用 handler ---- */
    if (ctx->handlers[current_state] == NULL)
    {
        ctx->in_step = false;
        return FSM_ERROR_NO_HANDLER;
    }

    next_state = ctx->handlers[current_state](ctx);

    /* ---- 6. 处理转换 ---- */
    ret = FSM_OK;

    if (next_state != current_state)
    {
        if (fsm_is_valid_transition(ctx, next_state))
        {
#if FSM_ENABLE_HSM
            fsm_hsm_perform_transition(ctx, next_state, trigger_ev);
#else
            fsm_do_transition(ctx, next_state, trigger_ev);
#endif
        }
        else
        {
            ret = FSM_ERROR_INVALID_TRANSITION;
        }
    }

    ctx->in_step = false;
    return ret;
}

fsm_ret_t fsm_request_transition(fsm_context_t *ctx, fsm_state_t target_state)
{
    FSM_ASSERT(ctx != NULL, FSM_ERROR_NULL_PTR);
    FSM_ASSERT(target_state < FSM_MAX_STATES, FSM_ERROR_INVALID_STATE);

    if (ctx->in_step)
    {
        return FSM_ERROR_BUSY;
    }

    if (!fsm_is_valid_transition(ctx, target_state))
    {
        return FSM_ERROR_INVALID_TRANSITION;
    }

#if FSM_ENABLE_HSM
    fsm_hsm_perform_transition(ctx, target_state, (uint8_t)FSM_EVENT_NONE);
#else
    fsm_do_transition(ctx, target_state, (uint8_t)FSM_EVENT_NONE);
#endif

    return FSM_OK;
}

fsm_ret_t fsm_reset(fsm_context_t *ctx, fsm_state_t reset_state)
{
    FSM_ASSERT(ctx != NULL, FSM_ERROR_NULL_PTR);
    FSM_ASSERT(reset_state < FSM_MAX_STATES, FSM_ERROR_INVALID_STATE);

    /* 保留 handler/transition/parent/callbacks/names/timeout_config/user_data */
    ctx->current_state = reset_state;
    ctx->last_state = reset_state;
    ctx->state_changed = true;
    ctx->in_step = false;

#if FSM_ENABLE_HSM
    ctx->hsm_cbs_fired = false;
#endif

#if FSM_ENABLE_TIMEOUT
    ctx->state_enter_tick = (ctx->get_tick_fn != NULL) ? ctx->get_tick_fn() : 0U;
#endif

#if FSM_ENABLE_EVENT_QUEUE
    ctx->eq_head = 0U;
    ctx->eq_tail = 0U;
    ctx->eq_count = 0U;
    ctx->current_event = FSM_EVENT_NONE;
    (void)memset(ctx->eq_buf, 0, sizeof(ctx->eq_buf));
#endif

    return FSM_OK;
}

/*============================================================================
 * v2.0 新增 API 实现（Timeout / Event / Stats / Trace）
 *============================================================================*/

#if FSM_ENABLE_TIMEOUT
fsm_ret_t fsm_set_tick_fn(fsm_context_t *ctx, fsm_get_tick_fn_t tick_fn)
{
    FSM_ASSERT(ctx != NULL, FSM_ERROR_NULL_PTR);
    FSM_ASSERT(tick_fn != NULL, FSM_ERROR_NULL_PTR);
    ctx->get_tick_fn = tick_fn;
    ctx->state_enter_tick = tick_fn();
    return FSM_OK;
}

fsm_ret_t fsm_add_timeout(fsm_context_t *ctx,
                          fsm_state_t state,
                          uint32_t timeout_ticks,
                          fsm_state_t target_state)
{
    FSM_ASSERT(ctx != NULL, FSM_ERROR_NULL_PTR);
    FSM_ASSERT(state < FSM_MAX_STATES, FSM_ERROR_INVALID_STATE);
    FSM_ASSERT(target_state < FSM_MAX_STATES, FSM_ERROR_INVALID_STATE);
    ctx->timeout_ticks[state] = timeout_ticks;
    ctx->timeout_target[state] = target_state;
    return FSM_OK;
}

uint32_t fsm_get_tick_in_state(const fsm_context_t *ctx)
{
    if ((ctx == NULL) || (ctx->get_tick_fn == NULL))
    {
        return 0U;
    }
    return ctx->get_tick_fn() - ctx->state_enter_tick;
}
#endif

#if FSM_ENABLE_EVENT_QUEUE
fsm_ret_t fsm_post_event(fsm_context_t *ctx, fsm_event_t event)
{
    FSM_ASSERT(ctx != NULL, FSM_ERROR_NULL_PTR);

    if (ctx->eq_count >= (uint8_t)FSM_EVENT_QUEUE_SIZE)
    {
        return FSM_ERROR_FULL;
    }

    ctx->eq_buf[ctx->eq_tail] = event;
    ctx->eq_tail = (uint8_t)((ctx->eq_tail + 1U) & (FSM_EVENT_QUEUE_SIZE - 1U));
    ctx->eq_count++;
    return FSM_OK;
}
#endif

#if FSM_ENABLE_STATS
fsm_ret_t fsm_get_stats(const fsm_context_t *ctx,
                        fsm_state_t state,
                        fsm_state_stats_t *out)
{
    FSM_ASSERT(ctx != NULL, FSM_ERROR_NULL_PTR);
    FSM_ASSERT(out != NULL, FSM_ERROR_NULL_PTR);
    FSM_ASSERT(state < FSM_MAX_STATES, FSM_ERROR_INVALID_STATE);
    (void)memcpy(out, &ctx->stats[state], sizeof(fsm_state_stats_t));
    return FSM_OK;
}

fsm_ret_t fsm_clear_stats(fsm_context_t *ctx)
{
    uint8_t i;
    FSM_ASSERT(ctx != NULL, FSM_ERROR_NULL_PTR);
    for (i = 0U; i < (uint8_t)FSM_MAX_STATES; i++)
    {
        ctx->stats[i].enter_count = 0U;
        ctx->stats[i].total_ticks = 0U;
        ctx->stats[i].max_ticks = 0U;
        ctx->stats[i].min_ticks = UINT32_MAX;
    }
    return FSM_OK;
}
#endif

#if FSM_ENABLE_TRACE
fsm_ret_t fsm_get_trace(const fsm_context_t *ctx,
                        fsm_trace_entry_t *buf,
                        uint8_t buf_size,
                        uint8_t *out_count)
{
    uint8_t total;
    uint8_t start_idx;
    uint8_t i;

    FSM_ASSERT(ctx != NULL, FSM_ERROR_NULL_PTR);
    FSM_ASSERT(buf != NULL, FSM_ERROR_NULL_PTR);
    FSM_ASSERT(out_count != NULL, FSM_ERROR_NULL_PTR);

    total = ctx->trace_count;

    if ((total == 0U) || (buf_size == 0U))
    {
        *out_count = 0U;
        return FSM_OK;
    }

    if (total > buf_size)
    {
        total = buf_size;
    }

    start_idx = (ctx->trace_count < (uint8_t)FSM_TRACE_BUFFER_SIZE) ? 0U : ctx->trace_idx;

    for (i = 0U; i < total; i++)
    {
        uint8_t src = (uint8_t)((start_idx + i) % FSM_TRACE_BUFFER_SIZE);
        (void)memcpy(&buf[i], &ctx->trace_buf[src], sizeof(fsm_trace_entry_t));
    }

    *out_count = total;
    return FSM_OK;
}

fsm_ret_t fsm_clear_trace(fsm_context_t *ctx)
{
    FSM_ASSERT(ctx != NULL, FSM_ERROR_NULL_PTR);
    (void)memset(ctx->trace_buf, 0, sizeof(ctx->trace_buf));
    ctx->trace_idx = 0U;
    ctx->trace_count = 0U;
    return FSM_OK;
}
#endif

/*============================================================================
 * v2.1 新增 API 实现：HSM
 *============================================================================*/

#if FSM_ENABLE_HSM

fsm_ret_t fsm_hsm_set_parent(fsm_context_t *ctx,
                             fsm_state_t state,
                             fsm_state_t parent)
{
    uint8_t depth;
    fsm_state_t s;

    FSM_ASSERT(ctx != NULL, FSM_ERROR_NULL_PTR);
    FSM_ASSERT(state < FSM_MAX_STATES, FSM_ERROR_INVALID_STATE);

    /* parent 可以是 FSM_HSM_NO_PARENT（将 state 设为根）或有效状态 ID */
    if (parent != FSM_HSM_NO_PARENT)
    {
        FSM_ASSERT(parent < FSM_MAX_STATES, FSM_ERROR_INVALID_STATE);
    }

    /* 自我父链检测：state 不能是自己的祖先 */
    if (parent == state)
    {
        return FSM_ERROR; /* 自指父链，静默拒绝 */
    }

    /*
     * 循环检测：假设设置后，沿新父链向上走，若遇到 state 自身则成环。
     * 走到 FSM_HSM_NO_PARENT 或超过 FSM_HSM_MAX_DEPTH 则安全。
     */
    if (parent != FSM_HSM_NO_PARENT)
    {
        s = parent;
        depth = 0U;

        while ((s != FSM_HSM_NO_PARENT) && (depth < (uint8_t)FSM_HSM_MAX_DEPTH))
        {
            if (s == state)
            {
                return FSM_ERROR; /* 检测到父链成环，静默拒绝 */
            }
            s = ctx->hsm_parent[s];
            depth++;
        }
    }

    ctx->hsm_parent[state] = parent;
    return FSM_OK;
}

uint8_t fsm_hsm_get_depth(const fsm_context_t *ctx, fsm_state_t state)
{
    uint8_t depth = 0U;
    fsm_state_t s;

    if ((ctx == NULL) || (state >= (fsm_state_t)FSM_MAX_STATES))
    {
        return 0U;
    }

    s = ctx->hsm_parent[state];

    while ((s != FSM_HSM_NO_PARENT) && (depth < (uint8_t)FSM_HSM_MAX_DEPTH))
    {
        s = ctx->hsm_parent[s];
        depth++;
    }

    return depth;
}

fsm_state_t fsm_hsm_get_lca(const fsm_context_t *ctx,
                            fsm_state_t a,
                            fsm_state_t b)
{
    uint8_t depth_a;
    uint8_t depth_b;
    uint8_t guard;

    if (ctx == NULL)
    {
        return FSM_HSM_NO_PARENT;
    }
    if (a == b)
    {
        return a;
    }

    depth_a = fsm_hsm_get_depth(ctx, a);
    depth_b = fsm_hsm_get_depth(ctx, b);

    /* ---- 深度均衡：将较深的状态上移至与较浅者同深 ---- */
    guard = (uint8_t)FSM_HSM_MAX_DEPTH;

    while ((depth_a > depth_b) && (guard > 0U))
    {
        a = ctx->hsm_parent[a];
        depth_a--;
        guard--;
    }

    guard = (uint8_t)FSM_HSM_MAX_DEPTH;

    while ((depth_b > depth_a) && (guard > 0U))
    {
        b = ctx->hsm_parent[b];
        depth_b--;
        guard--;
    }

    /* ---- 同步上移直至相遇 ---- */
    guard = (uint8_t)FSM_HSM_MAX_DEPTH;

    while ((a != b) && (guard > 0U))
    {
        if ((a == FSM_HSM_NO_PARENT) || (b == FSM_HSM_NO_PARENT))
        {
            /* 两个状态不在同一棵树中，无公共祖先 */
            return FSM_HSM_NO_PARENT;
        }

        a = ctx->hsm_parent[a];
        b = ctx->hsm_parent[b];
        guard--;
    }

    return (guard > 0U) ? a : FSM_HSM_NO_PARENT;
}

bool fsm_hsm_is_ancestor(const fsm_context_t *ctx,
                         fsm_state_t ancestor,
                         fsm_state_t state)
{
    fsm_state_t s;
    uint8_t guard;

    if (ctx == NULL)
    {
        return false;
    }
    if (ancestor == state)
    {
        return true;
    }

    s = state;
    guard = (uint8_t)FSM_HSM_MAX_DEPTH;

    while ((s != FSM_HSM_NO_PARENT) && (guard > 0U))
    {
        s = ctx->hsm_parent[s];
        guard--;

        if (s == ancestor)
        {
            return true;
        }
    }

    return false;
}

#endif /* FSM_ENABLE_HSM */