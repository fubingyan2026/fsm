/**
 * @file    fsm_example.c
 * @brief   游戏角色状态机示例 - 使用层次化状态机 (HSM)
 * @version 2.1.0
 *
 * 本示例展示如何使用 FSM 框架的 HSM (Hierarchical State Machine) 功能。
 * 场景：游戏角色状态管理
 *
 * 层次结构：
 * [ALIVE] (存活) - 父状态
 *   ├── [IDLE] (站立)
 *   ├── [MOVING] (移动) - 父状态
 *   │   ├── [WALKING] (行走)
 *   │   └── [RUNNING] (奔跑)
 *   ├── [ATTACKING] (攻击) - 父状态
 *   │   ├── [MELEE] (近战攻击)
 *   │   └── [RANGED] (远程攻击)
 *   └── [DEFENDING] (防御) - 父状态
 *       ├── [BLOCKING] (格挡)
 *       └── [DODGING] (闪避)
 *
 * [DEAD] (死亡) - 父状态
 *   └── [DYING] (濒死/等待复活)
 */

#include "fsm.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

/*============================================================================
 * 游戏角色状态机实例 - 层次化状态机 (HSM)
 *============================================================================*/

/**
 * @brief 角色状态定义 (使用 HSM 层次结构)
 */
typedef enum
{
    /* --- 存活分支 --- */
    CHAR_ALIVE = 0,        /**< 存活状态 (父状态) */
    CHAR_IDLE,             /**< 站立/休息 */
    CHAR_MOVING,           /**< 移动中 (父状态) */
    CHAR_WALKING,          /**< 行走 */
    CHAR_RUNNING,          /**< 奔跑 */
    CHAR_ATTACKING,        /**< 攻击中 (父状态) */
    CHAR_MELEE,            /**< 近战攻击 */
    CHAR_RANGED,           /**< 远程攻击 */
    CHAR_DEFENDING,        /**< 防御中 (父状态) */
    CHAR_BLOCKING,         /**< 格挡 */
    CHAR_DODGING,          /**< 闪避 */

    /* --- 死亡分支 --- */
    CHAR_DEAD,             /**< 死亡状态 (父状态) */
    CHAR_DYING,            /**< 濒死状态 */

    /* --- 元数据 --- */
    CHAR_STATE_COUNT       /**< 状态总数 */
} character_state_t;

/**
 * @brief 角色事件定义
 */
typedef enum
{
    CHAR_EVENT_NONE = 0,       /**< 无事件 */
    CHAR_EVENT_IDLE,            /**< 停止移动 */
    CHAR_EVENT_WALK,            /**< 开始行走 */
    CHAR_EVENT_RUN,             /**< 开始奔跑 */
    CHAR_EVENT_STOP_MOVING,     /**< 停止移动 */
    CHAR_EVENT_ATTACK_MELEE,    /**< 近战攻击 */
    CHAR_EVENT_ATTACK_RANGED,   /**< 远程攻击 */
    CHAR_EVENT_ATTACK_DONE,     /**< 攻击完成 */
    CHAR_EVENT_BLOCK,           /**< 开始格挡 */
    CHAR_EVENT_DODGE,           /**< 开始闪避 */
    CHAR_EVENT_DEFEND_DONE,     /**< 防御结束 */
    CHAR_EVENT_TAKE_DAMAGE,     /**< 受到伤害 */
    CHAR_EVENT_DIE,             /**< 死亡 */
    CHAR_EVENT_RESPAWN,         /**< 复活 */
    CHAR_EVENT_HURT             /**< 受伤 (攻击时) */
} character_event_t;

/**
 * @brief 角色用户数据结构
 */
typedef struct
{
    uint32_t health;           /**< 生命值 */
    uint32_t max_health;       /**< 最大生命值 */
    uint32_t stamina;           /**< 体力 */
    uint32_t max_stamina;       /**< 最大体力 */
    uint32_t action_timer;      /**< 动作计时器 */
    uint32_t damage;            /**< 伤害值 */
    bool is_alive;              /**< 是否存活 */
    bool in_combat;             /**< 是否在战斗中 */
} character_data_t;

/*============================================================================
 * 状态处理器函数
 *============================================================================*/

/**
 * @brief 站立状态处理器
 */
static fsm_state_t character_idle_handler(fsm_context_t *ctx)
{
    character_data_t *data = (character_data_t *)fsm_get_user_data(ctx);

    if (data == NULL)
    {
        return CHAR_IDLE;
    }

    /* 检查是否死亡 */
    if (!data->is_alive)
    {
        return CHAR_DYING;
    }

    printf("🎮 角色站立等待\n");
    data->action_timer++;

    /* 检查事件 */
    fsm_event_t event = fsm_get_current_event(ctx);

    /* 死亡事件 */
    if (event == CHAR_EVENT_DIE)
    {
        data->action_timer = 0;
        data->health = 0;
        data->is_alive = false;
        return CHAR_DYING;
    }

    /* 受伤事件 */
    if (event == CHAR_EVENT_TAKE_DAMAGE)
    {
        data->action_timer = 0;
        if (data->health > 20)
        {
            data->health -= 20;
            printf("💥 受到伤害！剩余生命: %u\n", data->health);
            return CHAR_BLOCKING; /* 切换到格挡姿态 */
        }
        else
        {
            printf("💀 生命值过低！\n");
            return CHAR_DYING; /* 进入濒死状态 */
        }
    }

    return CHAR_IDLE; /* 保持站立状态 */
}

/**
 * @brief 行走状态处理器
 */
static fsm_state_t character_walking_handler(fsm_context_t *ctx)
{
    character_data_t *data = (character_data_t *)fsm_get_user_data(ctx);

    if (data == NULL)
    {
        return CHAR_WALKING;
    }

    printf("🚶 角色行走中\n");
    data->action_timer++;
    data->stamina = (data->stamina > 0) ? data->stamina - 1 : 0;

    fsm_event_t event = fsm_get_current_event(ctx);

    /* 停止移动 */
    if (event == CHAR_EVENT_STOP_MOVING || data->stamina == 0)
    {
        data->action_timer = 0;
        return CHAR_IDLE;
    }

    /* 奔跑 */
    if (event == CHAR_EVENT_RUN && data->stamina > 20)
    {
        data->action_timer = 0;
        return CHAR_RUNNING;
    }

    /* 受伤 */
    if (event == CHAR_EVENT_TAKE_DAMAGE)
    {
        data->action_timer = 0;
        if (data->health > 20)
        {
            data->health -= 20;
            return CHAR_DEFENDING;
        }
        else
        {
            return CHAR_DYING;
        }
    }

    return CHAR_WALKING;
}

/**
 * @brief 奔跑状态处理器
 */
static fsm_state_t character_running_handler(fsm_context_t *ctx)
{
    character_data_t *data = (character_data_t *)fsm_get_user_data(ctx);

    if (data == NULL)
    {
        return CHAR_RUNNING;
    }

    printf("🏃 角色奔跑中\n");
    data->action_timer++;
    data->stamina = (data->stamina > 0) ? data->stamina - 3 : 0;

    fsm_event_t event = fsm_get_current_event(ctx);

    /* 体力不足或停止 */
    if (event == CHAR_EVENT_STOP_MOVING || data->stamina == 0)
    {
        data->action_timer = 0;
        return CHAR_WALKING;
    }

    /* 受伤 */
    if (event == CHAR_EVENT_TAKE_DAMAGE)
    {
        data->action_timer = 0;
        if (data->health > 20)
        {
            data->health -= 20;
            return CHAR_BLOCKING;
        }
        else
        {
            return CHAR_DYING;
        }
    }

    return CHAR_RUNNING;
}

/**
 * @brief 近战攻击状态处理器
 */
static fsm_state_t character_melee_handler(fsm_context_t *ctx)
{
    character_data_t *data = (character_data_t *)fsm_get_user_data(ctx);

    if (data == NULL)
    {
        return CHAR_MELEE;
    }

    printf("⚔️ 角色进行近战攻击！\n");
    data->action_timer++;
    data->stamina = (data->stamina > 0) ? data->stamina - 15 : 0;
    data->in_combat = true;

    /* 攻击完成 */
    if (data->action_timer >= 3)
    {
        data->action_timer = 0;
        printf("💥 近战攻击造成 %u 点伤害！\n", data->damage);
        return CHAR_IDLE;
    }

    return CHAR_MELEE;
}

/**
 * @brief 远程攻击状态处理器
 */
static fsm_state_t character_ranged_handler(fsm_context_t *ctx)
{
    character_data_t *data = (character_data_t *)fsm_get_user_data(ctx);

    if (data == NULL)
    {
        return CHAR_RANGED;
    }

    printf("🏹 角色进行远程攻击！\n");
    data->action_timer++;
    data->stamina = (data->stamina > 0) ? data->stamina - 10 : 0;
    data->in_combat = true;

    /* 攻击完成 */
    if (data->action_timer >= 5)
    {
        data->action_timer = 0;
        printf("💥 远程攻击造成 %u 点伤害！\n", data->damage);
        return CHAR_IDLE;
    }

    return CHAR_RANGED;
}

/**
 * @brief 格挡状态处理器
 */
static fsm_state_t character_blocking_handler(fsm_context_t *ctx)
{
    character_data_t *data = (character_data_t *)fsm_get_user_data(ctx);

    if (data == NULL)
    {
        return CHAR_BLOCKING;
    }

    /* 检查是否死亡 */
    if (!data->is_alive)
    {
        return CHAR_DYING;
    }

    printf("🛡️ 角色正在格挡！\n");
    data->action_timer++;
    data->stamina = (data->stamina > 0) ? data->stamina - 5 : 0;

    fsm_event_t event = fsm_get_current_event(ctx);

    /* 死亡事件 */
    if (event == CHAR_EVENT_DIE)
    {
        data->action_timer = 0;
        data->health = 0;
        data->is_alive = false;
        return CHAR_DYING;
    }

    /* 防御结束 */
    if (event == CHAR_EVENT_DEFEND_DONE || data->stamina == 0)
    {
        data->action_timer = 0;
        return CHAR_IDLE;
    }

    /* 受伤时减少格挡时间 */
    if (event == CHAR_EVENT_TAKE_DAMAGE)
    {
        data->action_timer = 0;
        data->health = (data->health > 5) ? data->health - 5 : 0; /* 格挡减少伤害 */
        printf("🛡️ 格挡成功！剩余生命: %u\n", data->health);
    }

    return CHAR_BLOCKING;
}

/**
 * @brief 闪避状态处理器
 */
static fsm_state_t character_dodging_handler(fsm_context_t *ctx)
{
    character_data_t *data = (character_data_t *)fsm_get_user_data(ctx);

    if (data == NULL)
    {
        return CHAR_DODGING;
    }

    printf("💨 角色正在闪避！\n");
    data->action_timer++;
    data->stamina = (data->stamina > 0) ? data->stamina - 20 : 0;
    data->in_combat = true;

    /* 闪避结束 */
    if (data->action_timer >= 2 || data->stamina == 0)
    {
        data->action_timer = 0;
        return CHAR_IDLE;
    }

    return CHAR_DODGING;
}

/**
 * @brief 濒死状态处理器
 */
static fsm_state_t character_dying_handler(fsm_context_t *ctx)
{
    character_data_t *data = (character_data_t *)fsm_get_user_data(ctx);

    if (data == NULL)
    {
        return CHAR_DYING;
    }

    printf("💀 角色濒死！等待复活...\n");
    data->action_timer++;
    data->is_alive = false;

    /* 3秒后自动复活 */
    if (data->action_timer >= 3)
    {
        data->action_timer = 0;
        data->health = data->max_health;
        data->stamina = data->max_stamina;
        data->is_alive = true;
        printf("✨ 角色复活！生命值: %u\n", data->health);
        return CHAR_IDLE;
    }

    return CHAR_DYING;
}

/*============================================================================
 * 条件函数
 *============================================================================*/

/**
 * @brief 检查是否存活
 */
static bool is_alive(const fsm_context_t *ctx)
{
    const character_data_t *data = (const character_data_t *)fsm_get_user_data(ctx);
    return (data != NULL && data->is_alive);
}

/**
 * @brief 检查是否有足够体力
 */
static bool has_stamina(const fsm_context_t *ctx)
{
    const character_data_t *data = (const character_data_t *)fsm_get_user_data(ctx);
    return (data != NULL && data->stamina > 10);
}

/**
 * @brief 检查是否在战斗中
 */
static bool is_in_combat(const fsm_context_t *ctx)
{
    const character_data_t *data = (const character_data_t *)fsm_get_user_data(ctx);
    return (data != NULL && data->in_combat);
}

/**
 * @brief 检查是否死亡
 */
static bool is_dead(const fsm_context_t *ctx)
{
    const character_data_t *data = (const character_data_t *)fsm_get_user_data(ctx);
    return (data != NULL && !data->is_alive);
}

/*============================================================================
 * 回调函数
 *============================================================================*/

#if FSM_ENABLE_CALLBACKS
/**
 * @brief 状态进入回调
 */
static void character_on_entry(fsm_context_t *ctx, fsm_state_t state)
{
    character_data_t *data = (character_data_t *)fsm_get_user_data(ctx);

    if (data == NULL)
    {
        return;
    }

    /* 重置计时器 */
    data->action_timer = 0;

    /* 进入状态时清理战斗状态 */
    if (state == CHAR_IDLE || state == CHAR_WALKING || state == CHAR_RUNNING)
    {
        data->in_combat = false;
    }

    /* 根据进入的状态执行特定操作 */
    switch (state)
    {
    case CHAR_ALIVE:
        printf("=== 进入【存活】状态 ===\n");
        break;
    case CHAR_IDLE:
        printf("=== 进入【站立】状态 ===\n");
        break;
    case CHAR_MOVING:
        printf("=== 进入【移动】父状态 ===\n");
        break;
    case CHAR_WALKING:
        printf("=== 进入【行走】状态 ===\n");
        break;
    case CHAR_RUNNING:
        printf("=== 进入【奔跑】状态 ===\n");
        break;
    case CHAR_ATTACKING:
        printf("=== 进入【攻击】父状态 ===\n");
        break;
    case CHAR_MELEE:
        printf("=== 进入【近战攻击】状态 ===\n");
        break;
    case CHAR_RANGED:
        printf("=== 进入【远程攻击】状态 ===\n");
        break;
    case CHAR_DEFENDING:
        printf("=== 进入【防御】父状态 ===\n");
        break;
    case CHAR_BLOCKING:
        printf("=== 进入【格挡】状态 ===\n");
        break;
    case CHAR_DODGING:
        printf("=== 进入【闪避】状态 ===\n");
        break;
    case CHAR_DEAD:
        printf("=== 进入【死亡】父状态 ===\n");
        break;
    case CHAR_DYING:
        printf("=== 进入【濒死】状态 ===\n");
        break;
    default:
        break;
    }
}

/**
 * @brief 状态退出回调
 */
static void character_on_exit(fsm_context_t *ctx, fsm_state_t state)
{
    (void)ctx;

    /* 根据退出的状态执行清理操作 */
    switch (state)
    {
    case CHAR_IDLE:
        printf("=== 退出【站立】状态 ===\n");
        break;
    case CHAR_WALKING:
        printf("=== 退出【行走】状态 ===\n");
        break;
    case CHAR_RUNNING:
        printf("=== 退出【奔跑】状态 ===\n");
        break;
    case CHAR_MELEE:
        printf("=== 退出【近战攻击】状态 ===\n");
        break;
    case CHAR_RANGED:
        printf("=== 退出【远程攻击】状态 ===\n");
        break;
    case CHAR_BLOCKING:
        printf("=== 退出【格挡】状态 ===\n");
        break;
    case CHAR_DODGING:
        printf("=== 退出【闪避】状态 ===\n");
        break;
    case CHAR_DYING:
        printf("=== 退出【濒死】状态 ===\n");
        break;
    default:
        break;
    }
}
#endif

/*============================================================================
 * Tick 源函数
 *============================================================================*/

#if FSM_ENABLE_TIMEOUT
/**
 * @brief 获取当前 tick 值
 */
static uint32_t character_get_tick(void)
{
    static uint32_t tick = 0;
    return tick++;
}
#endif

/*============================================================================
 * 状态名称表 (用于调试)
 *============================================================================*/

#if FSM_ENABLE_DEBUG
static const char *character_state_names[] = {
    "存活",      /* CHAR_ALIVE */
    "站立",      /* CHAR_IDLE */
    "移动(父)",  /* CHAR_MOVING */
    "行走",      /* CHAR_WALKING */
    "奔跑",      /* CHAR_RUNNING */
    "攻击(父)",  /* CHAR_ATTACKING */
    "近战",      /* CHAR_MELEE */
    "远程",      /* CHAR_RANGED */
    "防御(父)",  /* CHAR_DEFENDING */
    "格挡",      /* CHAR_BLOCKING */
    "闪避",      /* CHAR_DODGING */
    "死亡(父)",  /* CHAR_DEAD */
    "濒死",      /* CHAR_DYING */
    NULL         /* 结束标记 */
};
#endif

/*============================================================================
 * 游戏角色状态机初始化函数 (HSM 版本)
 *============================================================================*/

/**
 * @brief 初始化游戏角色状态机
 *
 * HSM 层次结构设置：
 * - CHAR_ALIVE 是 CHAR_IDLE, CHAR_MOVING, CHAR_ATTACKING, CHAR_DEFENDING 的父状态
 * - CHAR_MOVING 是 CHAR_WALKING, CHAR_RUNNING 的父状态
 * - CHAR_ATTACKING 是 CHAR_MELEE, CHAR_RANGED 的父状态
 * - CHAR_DEFENDING 是 CHAR_BLOCKING, CHAR_DODGING 的父状态
 * - CHAR_DEAD 是 CHAR_DYING 的父状态
 */
static fsm_ret_t character_init_hsm(fsm_context_t *ctx, character_data_t *data)
{
    fsm_ret_t ret;

    if (ctx == NULL || data == NULL)
    {
        return FSM_ERROR_NULL_PTR;
    }

    /* 初始化状态机上下文 */
    ret = fsm_init(ctx, CHAR_IDLE, data);
    if (ret != FSM_OK)
    {
        return ret;
    }

    /* 注册状态处理器 */
    fsm_register_handler(ctx, CHAR_IDLE, character_idle_handler);
    fsm_register_handler(ctx, CHAR_WALKING, character_walking_handler);
    fsm_register_handler(ctx, CHAR_RUNNING, character_running_handler);
    fsm_register_handler(ctx, CHAR_MELEE, character_melee_handler);
    fsm_register_handler(ctx, CHAR_RANGED, character_ranged_handler);
    fsm_register_handler(ctx, CHAR_BLOCKING, character_blocking_handler);
    fsm_register_handler(ctx, CHAR_DODGING, character_dodging_handler);
    fsm_register_handler(ctx, CHAR_DYING, character_dying_handler);

    /* ========== 设置 HSM 层次结构 ========== */
    /* 注意：必须先设置父关系，再添加转换 */

    /* 存活分支的父状态设置 */
    ret = fsm_hsm_set_parent(ctx, CHAR_IDLE, CHAR_ALIVE);
    if (ret != FSM_OK) return ret;

    ret = fsm_hsm_set_parent(ctx, CHAR_MOVING, CHAR_ALIVE);
    if (ret != FSM_OK) return ret;

    ret = fsm_hsm_set_parent(ctx, CHAR_ATTACKING, CHAR_ALIVE);
    if (ret != FSM_OK) return ret;

    ret = fsm_hsm_set_parent(ctx, CHAR_DEFENDING, CHAR_ALIVE);
    if (ret != FSM_OK) return ret;

    /* 移动子状态的父状态 */
    ret = fsm_hsm_set_parent(ctx, CHAR_WALKING, CHAR_MOVING);
    if (ret != FSM_OK) return ret;

    ret = fsm_hsm_set_parent(ctx, CHAR_RUNNING, CHAR_MOVING);
    if (ret != FSM_OK) return ret;

    /* 攻击子状态的父状态 */
    ret = fsm_hsm_set_parent(ctx, CHAR_MELEE, CHAR_ATTACKING);
    if (ret != FSM_OK) return ret;

    ret = fsm_hsm_set_parent(ctx, CHAR_RANGED, CHAR_ATTACKING);
    if (ret != FSM_OK) return ret;

    /* 防御子状态的父状态 */
    ret = fsm_hsm_set_parent(ctx, CHAR_BLOCKING, CHAR_DEFENDING);
    if (ret != FSM_OK) return ret;

    ret = fsm_hsm_set_parent(ctx, CHAR_DODGING, CHAR_DEFENDING);
    if (ret != FSM_OK) return ret;

    /* 死亡分支的父状态设置 */
    ret = fsm_hsm_set_parent(ctx, CHAR_DYING, CHAR_DEAD);
    if (ret != FSM_OK) return ret;

    /* 设置根状态 (ALIVE 和 DEAD 是顶级父状态) */
    ret = fsm_hsm_set_parent(ctx, CHAR_ALIVE, FSM_HSM_NO_PARENT);
    if (ret != FSM_OK) return ret;

    ret = fsm_hsm_set_parent(ctx, CHAR_DEAD, FSM_HSM_NO_PARENT);
    if (ret != FSM_OK) return ret;

    /* ========== 添加状态转换 (利用 HSM 转换继承) ========== */

    /* --- 存活分支的转换 --- */

    /* 站立状态的转换 (直接到子状态) */
    fsm_add_transition(ctx, CHAR_IDLE, CHAR_WALKING, has_stamina);  /* 有体力时开始行走 */
    fsm_add_transition(ctx, CHAR_IDLE, CHAR_MELEE, FSM_COND_ALWAYS); /* 站立时可以近战 */
    fsm_add_transition(ctx, CHAR_IDLE, CHAR_RANGED, FSM_COND_ALWAYS); /* 站立时可以远程 */
    fsm_add_transition(ctx, CHAR_IDLE, CHAR_BLOCKING, FSM_COND_ALWAYS); /* 站立时可以格挡 */
    fsm_add_transition(ctx, CHAR_IDLE, CHAR_DODGING, FSM_COND_ALWAYS); /* 站立时可以闪避 */
    fsm_add_transition(ctx, CHAR_IDLE, CHAR_DYING, is_dead); /* 死亡时进入濒死状态 */

    /* 移动父状态的转换 (子状态会继承) */
    fsm_add_transition(ctx, CHAR_MOVING, CHAR_IDLE, FSM_COND_ALWAYS); /* 停止移动 */
    fsm_add_transition(ctx, CHAR_MOVING, CHAR_MELEE, FSM_COND_ALWAYS); /* 移动中也可以攻击 */
    fsm_add_transition(ctx, CHAR_MOVING, CHAR_RANGED, FSM_COND_ALWAYS); /* 移动中也可以远程 */
    fsm_add_transition(ctx, CHAR_MOVING, CHAR_BLOCKING, FSM_COND_ALWAYS); /* 移动中可以格挡 */
    fsm_add_transition(ctx, CHAR_MOVING, CHAR_DYING, is_dead); /* 死亡时进入濒死状态 */

    /* 攻击父状态的转换 (子状态会继承) */
    fsm_add_transition(ctx, CHAR_ATTACKING, CHAR_IDLE, FSM_COND_ALWAYS); /* 攻击完成后返回站立 */
    fsm_add_transition(ctx, CHAR_ATTACKING, CHAR_DYING, is_dead); /* 死亡时进入濒死状态 */

    /* 防御父状态的转换 (子状态会继承) */
    fsm_add_transition(ctx, CHAR_DEFENDING, CHAR_IDLE, FSM_COND_ALWAYS); /* 防御结束 */
    fsm_add_transition(ctx, CHAR_DEFENDING, CHAR_DYING, is_dead); /* 死亡时进入濒死状态 */

    /* --- 死亡分支的转换 --- */
    fsm_add_transition(ctx, CHAR_DYING, CHAR_IDLE, is_alive); /* 复活时返回站立状态 */

#if FSM_ENABLE_CALLBACKS
    /* 注册回调函数 */
    fsm_set_callbacks(ctx, character_on_entry, character_on_exit);
#endif

#if FSM_ENABLE_DEBUG
    /* 设置状态名称表 */
    fsm_set_state_names(ctx, character_state_names, CHAR_STATE_COUNT);
#endif

#if FSM_ENABLE_TIMEOUT
    /* 设置 tick 源 */
    fsm_set_tick_fn(ctx, character_get_tick);
#endif

    return FSM_OK;
}

/*============================================================================
 * 主函数 - 演示游戏角色 HSM 状态机运行
 *============================================================================*/

int main(void)
{
    fsm_context_t character_fsm;
    character_data_t character_data = {
        .health = 100,
        .max_health = 100,
        .stamina = 100,
        .max_stamina = 100,
        .action_timer = 0,
        .damage = 25,
        .is_alive = true,
        .in_combat = false
    };

    fsm_ret_t ret;
    int tick_count = 0;

    printf("🎮 游戏角色 HSM 状态机演示开始\n\n");

    /* 初始化 HSM 状态机 */
    ret = character_init_hsm(&character_fsm, &character_data);
    if (ret != FSM_OK)
    {
        printf("错误: 状态机初始化失败 (错误码: %d)\n", ret);
        return -1;
    }

    printf("角色初始化完成，进入HSM层次结构...\n\n");

    /* 运行角色状态机 */
    while (tick_count < 35)
    {
        /* 执行状态机步进 */
        ret = fsm_step(&character_fsm);
        if (ret != FSM_OK && ret != FSM_ERROR_TIMEOUT)
        {
            printf("错误: 状态机步进失败 (错误码: %d)\n", ret);
            break;
        }

        /* 模拟外部事件 */
        switch (tick_count)
        {
        case 3:
            printf("\n🏃 触发行走事件\n");
            fsm_post_event(&character_fsm, CHAR_EVENT_WALK);
            break;

        case 6:
            printf("\n🏃 触发奔跑事件\n");
            fsm_post_event(&character_fsm, CHAR_EVENT_RUN);
            break;

        case 9:
            printf("\n🛡️ 触发受伤事件!\n");
            fsm_post_event(&character_fsm, CHAR_EVENT_TAKE_DAMAGE);
            break;

        case 12:
            printf("\n⚔️ 触发近战攻击事件\n");
            fsm_post_event(&character_fsm, CHAR_EVENT_ATTACK_MELEE);
            break;

        case 16:
            printf("\n🏹 触发远程攻击事件\n");
            fsm_post_event(&character_fsm, CHAR_EVENT_ATTACK_RANGED);
            break;

        case 20:
            printf("\n🛡️ 触发格挡事件\n");
            fsm_post_event(&character_fsm, CHAR_EVENT_BLOCK);
            break;

        case 23:
            printf("\n💨 触发闪避事件\n");
            fsm_post_event(&character_fsm, CHAR_EVENT_DODGE);
            break;

        case 26:
            printf("\n💀 模拟死亡 (设置 is_alive = false)\n");
            character_data.is_alive = false;
            character_data.health = 0;
            fsm_post_event(&character_fsm, CHAR_EVENT_DIE);
            break;
        }

        /* 显示当前状态信息 */
#if FSM_ENABLE_DEBUG
        const char *state_name = fsm_get_state_name(&character_fsm,
                                                    fsm_get_current_state(&character_fsm));
        printf("当前状态: %s\n", state_name ? state_name : "未知");
#endif

        /* 显示角色信息 */
        printf("生命: %u/%u | 体力: %u/%u | 战斗: %s\n",
               character_data.health, character_data.max_health,
               character_data.stamina, character_data.max_stamina,
               character_data.in_combat ? "是" : "否");
        printf("---\n");

        /* 等待 1 秒 */
        sleep(1);
        tick_count++;
    }

    printf("\n🎮 游戏角色 HSM 状态机演示结束\n");

    /* 显示统计信息 */
#if FSM_ENABLE_STATS
    printf("\n=== 状态统计信息 ===\n");
    for (fsm_state_t state = 0; state < CHAR_STATE_COUNT; state++)
    {
        const char *state_name = fsm_get_state_name(&character_fsm, state);
        if (state_name != NULL)
        {
            printf("%s: 状态ID = %d\n", state_name, state);
        }
    }
#endif

    return 0;
}
