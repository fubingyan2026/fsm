/**
 * @file    pubg_fsm.c
 * @brief   "和平精英" 场景示例：演示较复杂的状态机流转
 *
 * 本示例构建一个 FSM，模拟从大厅 -> 匹配 -> 载入 -> 游戏内 -> 胜利/失败的流程，
 * 并展示了超时、事件驱动、以及动作/日志等组合的使用方式。
 */

#include "pubg_fsm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* NDEBUG を無効化する独自 assert */
#define TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); \
            __builtin_trap(); \
        } \
    } while (0)

/* 状态名 */
static const char * const PUBGM_STATE_NAMES[ST_COUNT] =
{
    "菜单",
    "匹配中",
    "加载中",
    "跳伞",
    "选择落点",
    "下落",
    "着陆",
    "游戏中",
    "安全区",
    "战斗",
    "敌方小队A",
    "敌方小队B",
    "敌方小队C",
    "搜刮物资",
    "空投",
    "队友",
    "随机事件",
    "被击倒",
    "复活中",
    "胜利",
    "失败",
    "总结",
};

/* 日志 */
#define PUBG_LOG_MAX 64
static char  g_pubg_log[PUBG_LOG_MAX][48];
static int   g_pubg_log_count;

static void pubg_log_reset(void)
{
    g_pubg_log_count = 0;
    memset(g_pubg_log, 0, sizeof(g_pubg_log));
}

static void pubg_log_push(const char *prefix, fsm_state_t state, const fsm_context_t *ctx)
{
    if (g_pubg_log_count < PUBG_LOG_MAX)
    {
        snprintf(g_pubg_log[g_pubg_log_count], sizeof(g_pubg_log[0]), "%s(%s)", prefix,
                 fsm_get_state_name(ctx, state));
        g_pubg_log_count++;
    }
}

static void pubg_on_entry(fsm_context_t *ctx, fsm_state_t state)
{
    pubg_log_push("ENTRY", state, ctx);
}

static void pubg_on_exit(fsm_context_t *ctx, fsm_state_t state)
{
    pubg_log_push("EXIT", state, ctx);
}

/* Tick 模拟 */
static uint32_t g_pubg_tick;
static uint32_t pubg_tick_fn(void) { return g_pubg_tick; }

/* handler 实现 */
static fsm_state_t handler_menu(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);

    if (ev == EV_START_MATCH)
    {
        return (fsm_state_t)ST_MATCHMAKING;
    }

    return (fsm_state_t)ST_MENU;
}

static fsm_state_t handler_matchmaking(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);

    /* 超时：匹配超时则返回大厅 */
    if (ev == EV_GAME_OVER)
    {
        return (fsm_state_t)ST_MENU;
    }

    if (ev == EV_MATCH_FOUND)
    {
        return (fsm_state_t)ST_LOADING;
    }

    return (fsm_state_t)ST_MATCHMAKING;
}

static fsm_state_t handler_parachute(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);
    pubg_user_data_t *ud = (pubg_user_data_t *)fsm_get_user_data(ctx);

    if (ev == EV_DROP_POINT_SELECTED)
    {
        ud->drop_point_selected = true;
        return (fsm_state_t)ST_DROP_SELECTION;
    }

    /* 保持在跳伞阶段，等待落点选择 */
    return (fsm_state_t)ST_PARACHUTE;
}

static fsm_state_t handler_drop_selection(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);
    pubg_user_data_t *ud = (pubg_user_data_t *)fsm_get_user_data(ctx);

    /* 由 parachute 处理 drop point 选择，当前只需根据标记推进 */
    if (ud->drop_point_selected)
    {
        ud->drop_point_selected = false;
        return (fsm_state_t)ST_FALLING;
    }

    /* 额外处理：如果直接收到开伞事件，也提前进入落地阶段 */
    if (ev == EV_PARACHUTE_OPEN)
    {
        ud->parachute_opened = true;
        return (fsm_state_t)ST_FALLING;
    }

    return (fsm_state_t)ST_DROP_SELECTION;
}

static fsm_state_t handler_falling(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);
    pubg_user_data_t *ud = (pubg_user_data_t *)fsm_get_user_data(ctx);

    /* 如果已经开伞或收到开伞事件，进入着陆阶段 */
    if (ud->parachute_opened || ev == EV_PARACHUTE_OPEN)
    {
        ud->parachute_opened = false;
        return (fsm_state_t)ST_LANDING;
    }

    /* 收到着陆事件直接进入着陆 */
    if (ev == EV_LANDED)
    {
        ud->landed = true;
        return (fsm_state_t)ST_LANDING;
    }

    return (fsm_state_t)ST_FALLING;
}

static fsm_state_t handler_landing(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);
    pubg_user_data_t *ud = (pubg_user_data_t *)fsm_get_user_data(ctx);

    if (ev == EV_LANDED)
    {
        ud->landed = true;
        return (fsm_state_t)ST_IN_GAME;
    }

    /* 如果已经标记为着陆，也可以直接进入游戏 */
    if (ud->landed)
    {
        return (fsm_state_t)ST_IN_GAME;
    }

    return (fsm_state_t)ST_LANDING;
}

static fsm_state_t handler_loading(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);
    if (ev == EV_LOAD_DONE)
    {
        return (fsm_state_t)ST_PARACHUTE;
    }
    return (fsm_state_t)ST_LOADING;
}

static fsm_state_t handler_in_game(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);
    pubg_user_data_t *ud = (pubg_user_data_t *)fsm_get_user_data(ctx);

    if (ev == EV_VICTORY)
    {
        return (fsm_state_t)ST_WIN;
    }
    if (ev == EV_GAME_OVER)
    {
        return (fsm_state_t)ST_LOSE;
    }

    /* 安全区收缩 */
    if (ev == EV_ZONE_CLOSE)
    {
        ud->safe_zone_active = true;
        return (fsm_state_t)ST_SAFE_ZONE;
    }

    /* 进入战斗（选择敌人小队 A/B/C） */
    if (ev == EV_ENEMY_SQUAD_A)
    {
        ud->in_combat = true;
        ud->enemy_squads++;
        ud->target_squad = 1;
        return (fsm_state_t)ST_COMBAT;
    }
    if (ev == EV_ENEMY_SQUAD_B)
    {
        ud->in_combat = true;
        ud->enemy_squads++;
        ud->target_squad = 2;
        return (fsm_state_t)ST_COMBAT;
    }
    if (ev == EV_ENEMY_SQUAD_C)
    {
        ud->in_combat = true;
        ud->enemy_squads++;
        ud->target_squad = 3;
        return (fsm_state_t)ST_COMBAT;
    }

    /* 物资 / 空投 / 队友 */
    if (ev == EV_LOOT_FOUND)
    {
        return (fsm_state_t)ST_LOOTING;
    }
    if (ev == EV_AIRDROP_INCOMING)
    {
        ud->airdrop_active = true;
        return (fsm_state_t)ST_AIRDROP;
    }
    if (ev == EV_TEAMMATE_DOWN)
    {
        ud->teammates_alive = (ud->teammates_alive > 0) ? (ud->teammates_alive - 1) : 0;
        return (fsm_state_t)ST_TEAMMATE;
    }

    /* 随机事件 */
    if (ev == EV_RANDOM_EVENT)
    {
        ud->random_event_active = true;
        return (fsm_state_t)ST_RANDOM_EVENT;
    }

    return (fsm_state_t)ST_IN_GAME;
}

static fsm_state_t handler_squad(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);
    pubg_user_data_t *ud = (pubg_user_data_t *)fsm_get_user_data(ctx);
    fsm_state_t cur = fsm_get_current_state(ctx);

    if (ev == EV_HIT || ev == EV_KNOCKED)
    {
        return (fsm_state_t)ST_KNOCKED;
    }

    if (ev == EV_ELIMINATED)
    {
        ud->kills++;
        return (fsm_state_t)ST_LOOTING;
    }

    if (ev == EV_SUPPORT_CALL)
    {
        /* 队友支援：在三支队之间轮换 */
        if (cur == (fsm_state_t)ST_ENEMY_SQUAD_A)
        {
            return (fsm_state_t)ST_ENEMY_SQUAD_B;
        }
        if (cur == (fsm_state_t)ST_ENEMY_SQUAD_B)
        {
            return (fsm_state_t)ST_ENEMY_SQUAD_C;
        }
        if (cur == (fsm_state_t)ST_ENEMY_SQUAD_C)
        {
            return (fsm_state_t)ST_ENEMY_SQUAD_A;
        }
    }

    if (ev == EV_VICTORY)
    {
        return (fsm_state_t)ST_WIN;
    }

    return cur;
}

static fsm_state_t handler_safe_zone(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);

    /* safe zone 结束后回到游戏 */
    if (ev == EV_ZONE_CLOSE)
    {
        return (fsm_state_t)ST_IN_GAME;
    }

    return (fsm_state_t)ST_SAFE_ZONE;
}

static fsm_state_t handler_combat(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);
    pubg_user_data_t *ud = (pubg_user_data_t *)fsm_get_user_data(ctx);
    fsm_state_t cur = fsm_get_current_state(ctx);

    /* 如果目标小队已设置，则先进入该子状态 */
    if (ud->target_squad != 0U)
    {
        fsm_state_t next = cur;
        if (ud->target_squad == 1U)
        {
            next = (fsm_state_t)ST_ENEMY_SQUAD_A;
        }
        else if (ud->target_squad == 2U)
        {
            next = (fsm_state_t)ST_ENEMY_SQUAD_B;
        }
        else if (ud->target_squad == 3U)
        {
            next = (fsm_state_t)ST_ENEMY_SQUAD_C;
        }
        ud->target_squad = 0U;
        return next;
    }

    if (ev == EV_HIT || ev == EV_KNOCKED)
    {
        return (fsm_state_t)ST_KNOCKED;
    }
    if (ev == EV_ELIMINATED)
    {
        ud->kills++;
        return (fsm_state_t)ST_LOOTING;
    }
    if (ev == EV_SUPPORT_CALL)
    {
        if (cur == (fsm_state_t)ST_ENEMY_SQUAD_A)
        {
            return (fsm_state_t)ST_ENEMY_SQUAD_B;
        }
        if (cur == (fsm_state_t)ST_ENEMY_SQUAD_B)
        {
            return (fsm_state_t)ST_ENEMY_SQUAD_C;
        }
        if (cur == (fsm_state_t)ST_ENEMY_SQUAD_C)
        {
            return (fsm_state_t)ST_ENEMY_SQUAD_A;
        }
    }
    if (ev == EV_VICTORY)
    {
        return (fsm_state_t)ST_WIN;
    }

    return (fsm_state_t)ST_COMBAT;
}

static fsm_state_t handler_looting(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);
    pubg_user_data_t *ud = (pubg_user_data_t *)fsm_get_user_data(ctx);

    if (ev == EV_LOOT_FOUND)
    {
        ud->loot_count++;
        return (fsm_state_t)ST_IN_GAME;
    }
    if (ev == EV_AIRDROP_INCOMING)
    {
        ud->airdrop_active = true;
        return (fsm_state_t)ST_AIRDROP;
    }
    if (ev == EV_ENEMY_SQUAD_A || ev == EV_ENEMY_SQUAD_B || ev == EV_ENEMY_SQUAD_C)
    {
        ud->enemy_squads++;
        return (fsm_state_t)ST_COMBAT;
    }

    return (fsm_state_t)ST_LOOTING;
}

static fsm_state_t handler_airdrop(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);
    pubg_user_data_t *ud = (pubg_user_data_t *)fsm_get_user_data(ctx);

    if (ev == EV_AIRDROP_LOOTED)
    {
        ud->airdrop_active = false;
        ud->loot_count += 3;
        return (fsm_state_t)ST_IN_GAME;
    }
    if (ev == EV_ENEMY_SQUAD_A || ev == EV_ENEMY_SQUAD_B || ev == EV_ENEMY_SQUAD_C)
    {
        ud->enemy_squads++;
        return (fsm_state_t)ST_COMBAT;
    }

    return (fsm_state_t)ST_AIRDROP;
}

static fsm_state_t handler_teammate(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);
    pubg_user_data_t *ud = (pubg_user_data_t *)fsm_get_user_data(ctx);

    if (ev == EV_TEAMMATE_REVIVED)
    {
        ud->teammates_alive++;
        return (fsm_state_t)ST_IN_GAME;
    }
    if (ev == EV_RANDOM_EVENT)
    {
        ud->random_event_active = true;
        return (fsm_state_t)ST_RANDOM_EVENT;
    }

    return (fsm_state_t)ST_TEAMMATE;
}

static fsm_state_t handler_random_event(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);
    pubg_user_data_t *ud = (pubg_user_data_t *)fsm_get_user_data(ctx);

    if (ev == EV_RANDOM_EVENT)
    {
        /* 随机事件影响多个字段 */
        switch (ud->tick_count % 3U)
        {
        case 0:
            ud->safe_zone_active = !ud->safe_zone_active;
            break;
        case 1:
            ud->loot_count += 2;
            break;
        case 2:
            ud->teammates_alive += 1;
            break;
        }

        ud->random_event_active = false;
        return (fsm_state_t)ST_IN_GAME;
    }

    /* 保持在事件状态，直到收到随机事件结束信号 */
    return (fsm_state_t)ST_RANDOM_EVENT;
}

static fsm_state_t handler_knocked(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);

    if (ev == EV_REVIVED)
    {
        return (fsm_state_t)ST_REVIVING;
    }
    if (ev == EV_ELIMINATED || ev == EV_GAME_OVER)
    {
        return (fsm_state_t)ST_LOSE;
    }

    return (fsm_state_t)ST_KNOCKED;
}

static fsm_state_t handler_reviving(fsm_context_t *ctx)
{
    fsm_event_t ev = fsm_get_current_event(ctx);
    if (ev == EV_ELIMINATED || ev == EV_GAME_OVER)
    {
        return (fsm_state_t)ST_LOSE;
    }
    if (ev == EV_VICTORY)
    {
        return (fsm_state_t)ST_WIN;
    }
    return (fsm_state_t)ST_IN_GAME;
}

static fsm_state_t handler_win(fsm_context_t *ctx)
{
    (void)ctx;
    return (fsm_state_t)ST_SUMMARY;
}

static fsm_state_t handler_lose(fsm_context_t *ctx)
{
    (void)ctx;
    return (fsm_state_t)ST_SUMMARY;
}

static fsm_state_t handler_summary(fsm_context_t *ctx)
{
    (void)ctx;
    return (fsm_state_t)ST_SUMMARY;
}

/* 场景构建 */
static fsm_context_t g_pubg_fsm;
static pubg_user_data_t g_pubg_data;

static void setup_pubg_fsm(void)
{
    memset(&g_pubg_data, 0, sizeof(g_pubg_data));
    g_pubg_tick = 0;
    pubg_log_reset();

    TEST_ASSERT(fsm_init(&g_pubg_fsm, (fsm_state_t)ST_MENU, &g_pubg_data) == FSM_OK);
    TEST_ASSERT(fsm_set_state_names(&g_pubg_fsm, PUBGM_STATE_NAMES, (uint8_t)ST_COUNT) == FSM_OK);
    TEST_ASSERT(fsm_set_tick_fn(&g_pubg_fsm, pubg_tick_fn) == FSM_OK);
    TEST_ASSERT(fsm_set_callbacks(&g_pubg_fsm, pubg_on_entry, pubg_on_exit) == FSM_OK);

    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_MENU,           handler_menu)           == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_MATCHMAKING,    handler_matchmaking)    == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_LOADING,        handler_loading)        == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_PARACHUTE,      handler_parachute)      == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_DROP_SELECTION, handler_drop_selection) == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_FALLING,        handler_falling)        == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_LANDING,        handler_landing)        == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_IN_GAME,        handler_in_game)        == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_SAFE_ZONE,      handler_safe_zone)      == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_COMBAT,         handler_combat)         == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_ENEMY_SQUAD_A,  handler_squad)          == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_ENEMY_SQUAD_B,  handler_squad)          == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_ENEMY_SQUAD_C,  handler_squad)          == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_LOOTING,        handler_looting)        == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_AIRDROP,        handler_airdrop)        == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_TEAMMATE,       handler_teammate)       == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_RANDOM_EVENT,   handler_random_event)   == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_KNOCKED,        handler_knocked)        == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_REVIVING,       handler_reviving)       == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_WIN,            handler_win)            == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_LOSE,           handler_lose)           == FSM_OK);
    TEST_ASSERT(fsm_register_handler(&g_pubg_fsm, ST_SUMMARY,        handler_summary)        == FSM_OK);

    /* HSM 层级（parent -> child）
     *  - ST_PARACHUTE 为父状态：包含 DROP_SELECTION/FALLING/LANDING
     *  - ST_IN_GAME 为父状态：包含 SAFE_ZONE/COMBAT/LOOTING/AIRDROP/TEAMMATE/RANDOM_EVENT/KNOCKED/REVIVING
     *  - ST_COMBAT 为父状态：包含 ENEMY_SQUAD_A/B/C
     */
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_MENU,          FSM_HSM_NO_PARENT)              == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_MATCHMAKING,   (fsm_state_t)ST_MENU)          == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_LOADING,       (fsm_state_t)ST_MENU)          == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_PARACHUTE,     (fsm_state_t)ST_MENU)          == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_DROP_SELECTION,(fsm_state_t)ST_PARACHUTE)    == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_FALLING,       (fsm_state_t)ST_PARACHUTE)    == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_LANDING,       (fsm_state_t)ST_PARACHUTE)    == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_IN_GAME,       (fsm_state_t)ST_MENU)          == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_SAFE_ZONE,     (fsm_state_t)ST_IN_GAME)       == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_COMBAT,        (fsm_state_t)ST_IN_GAME)       == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_ENEMY_SQUAD_A, (fsm_state_t)ST_COMBAT)        == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_ENEMY_SQUAD_B, (fsm_state_t)ST_COMBAT)        == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_ENEMY_SQUAD_C, (fsm_state_t)ST_COMBAT)        == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_LOOTING,       (fsm_state_t)ST_IN_GAME)       == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_AIRDROP,       (fsm_state_t)ST_IN_GAME)       == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_TEAMMATE,      (fsm_state_t)ST_IN_GAME)       == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_RANDOM_EVENT,  (fsm_state_t)ST_IN_GAME)       == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_KNOCKED,       (fsm_state_t)ST_IN_GAME)       == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_REVIVING,      (fsm_state_t)ST_IN_GAME)       == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_WIN,           (fsm_state_t)ST_MENU)          == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_LOSE,          (fsm_state_t)ST_MENU)          == FSM_OK);
    TEST_ASSERT(fsm_hsm_set_parent(&g_pubg_fsm, ST_SUMMARY,       (fsm_state_t)ST_MENU)          == FSM_OK);

    /* 转换注册 */
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_MENU,          ST_MATCHMAKING,    NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_MATCHMAKING,   ST_MENU,           NULL)     == FSM_OK); /* 超时返回大厅 */
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_MATCHMAKING,   ST_LOADING,        NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_LOADING,       ST_PARACHUTE,      NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_PARACHUTE,     ST_DROP_SELECTION, NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_DROP_SELECTION,ST_FALLING,        NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_FALLING,       ST_LANDING,        NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_LANDING,       ST_IN_GAME,        NULL)     == FSM_OK);

    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_IN_GAME,       ST_SAFE_ZONE,      NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_IN_GAME,       ST_COMBAT,         NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_COMBAT,        ST_ENEMY_SQUAD_A,  NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_COMBAT,        ST_ENEMY_SQUAD_B,  NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_COMBAT,        ST_ENEMY_SQUAD_C,  NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_IN_GAME,       ST_LOOTING,        NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_IN_GAME,       ST_AIRDROP,        NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_IN_GAME,       ST_TEAMMATE,       NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_IN_GAME,       ST_RANDOM_EVENT,   NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_IN_GAME,       ST_KNOCKED,        NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_IN_GAME,       ST_WIN,            NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_IN_GAME,       ST_LOSE,           NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_LOOTING,       ST_IN_GAME,        NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_LOOTING,       ST_AIRDROP,        NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_AIRDROP,       ST_IN_GAME,        NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_AIRDROP,       ST_COMBAT,         NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_TEAMMATE,      ST_IN_GAME,        NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_TEAMMATE,      ST_RANDOM_EVENT,   NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_RANDOM_EVENT,  ST_IN_GAME,        NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_SAFE_ZONE,     ST_IN_GAME,        NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_COMBAT,        ST_KNOCKED,        NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_COMBAT,        ST_IN_GAME,        NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_COMBAT,        ST_WIN,            NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_KNOCKED,       ST_REVIVING,       NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_KNOCKED,       ST_LOSE,           NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_REVIVING,      ST_IN_GAME,        NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_REVIVING,      ST_WIN,            NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_REVIVING,      ST_LOSE,           NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_WIN,           ST_SUMMARY,        NULL)     == FSM_OK);
    TEST_ASSERT(fsm_add_transition(&g_pubg_fsm, ST_LOSE,          ST_SUMMARY,        NULL)     == FSM_OK);

    /* 超时 */
    TEST_ASSERT(fsm_add_timeout(&g_pubg_fsm, ST_MATCHMAKING, 100U, (fsm_state_t)ST_MENU)      == FSM_OK);
    TEST_ASSERT(fsm_add_timeout(&g_pubg_fsm, ST_LOADING,      50U,  (fsm_state_t)ST_IN_GAME)   == FSM_OK);
    TEST_ASSERT(fsm_add_timeout(&g_pubg_fsm, ST_IN_GAME,      80U,  (fsm_state_t)ST_SAFE_ZONE) == FSM_OK); /* 安全区倒计时 */
    TEST_ASSERT(fsm_add_timeout(&g_pubg_fsm, ST_KNOCKED,      40U,  (fsm_state_t)ST_LOSE)     == FSM_OK);
    TEST_ASSERT(fsm_add_timeout(&g_pubg_fsm, ST_SAFE_ZONE,    60U,  (fsm_state_t)ST_IN_GAME)   == FSM_OK);
    TEST_ASSERT(fsm_add_timeout(&g_pubg_fsm, ST_LOOTING,      30U,  (fsm_state_t)ST_IN_GAME)   == FSM_OK);
    TEST_ASSERT(fsm_add_timeout(&g_pubg_fsm, ST_AIRDROP,      40U,  (fsm_state_t)ST_IN_GAME)   == FSM_OK);
    TEST_ASSERT(fsm_add_timeout(&g_pubg_fsm, ST_RANDOM_EVENT, 50U,  (fsm_state_t)ST_IN_GAME)   == FSM_OK);
    TEST_ASSERT(fsm_add_timeout(&g_pubg_fsm, ST_REVIVING,     30U,  (fsm_state_t)ST_IN_GAME)   == FSM_OK);
}

static void dump_log(void)
{
    for (int i = 0; i < g_pubg_log_count; ++i)
    {
        printf("  %s\n", g_pubg_log[i]);
    }
}

static void advance_ticks(uint32_t ticks)
{
    for (uint32_t i = 0; i < ticks; i++)
    {
        g_pubg_tick++;
        g_pubg_data.tick_count++;
        fsm_step(&g_pubg_fsm);
    }
}

static void run_scenario(void)
{
    setup_pubg_fsm();

    /* 初始值 */
    g_pubg_data.teammates_alive = 2;
    g_pubg_data.enemy_squads = 1;

    printf("=== PUBG FSM 模拟场景 ===\n");
    printf("初始状态: %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    /* 1) 开始匹配 */
    fsm_post_event(&g_pubg_fsm, EV_START_MATCH);
    fsm_step(&g_pubg_fsm);
    printf("[1] 发起匹配 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    /* 2) 匹配成功 -> 载入 */
    fsm_post_event(&g_pubg_fsm, EV_MATCH_FOUND);
    fsm_step(&g_pubg_fsm);
    printf("[2] 匹配成功 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    /* 3) 加载完成 -> 跳伞阶段（层次状态） */
    fsm_post_event(&g_pubg_fsm, EV_LOAD_DONE);
    fsm_step(&g_pubg_fsm);
    printf("[3] 加载完成 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    /* 4) 选择落点 -> 开伞 -> 着陆 */
    fsm_post_event(&g_pubg_fsm, EV_DROP_POINT_SELECTED);
    fsm_step(&g_pubg_fsm);
    printf("[4] 选择落点 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    fsm_post_event(&g_pubg_fsm, EV_PARACHUTE_OPEN);
    fsm_step(&g_pubg_fsm);
    printf("[5] 开伞 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    fsm_post_event(&g_pubg_fsm, EV_LANDED);
    fsm_step(&g_pubg_fsm);
    printf("[6] 着陆 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    /* 7) 安全区倒计时触发（通过超时） */
    advance_ticks(90);
    printf("[7] 安全区收缩 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    /* 8) 安全区结束回到游戏 */
    advance_ticks(65);
    printf("[8] 安全区结束 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    /* 9) 遇到敌人小队 A，战斗开始 */
    fsm_post_event(&g_pubg_fsm, EV_ENEMY_SQUAD_A);
    fsm_step(&g_pubg_fsm);
    printf("[9] 遇到敌人小队 A → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    /* 10) 请求支援，使战斗切换到 B -> C */
    fsm_post_event(&g_pubg_fsm, EV_SUPPORT_CALL);
    fsm_step(&g_pubg_fsm);
    printf("[10] 请求支援 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    fsm_post_event(&g_pubg_fsm, EV_SUPPORT_CALL);
    fsm_step(&g_pubg_fsm);
    printf("[11] 再次支援 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    /* 12) 战斗中被击倒 -> 复活 */
    fsm_post_event(&g_pubg_fsm, EV_HIT);
    fsm_step(&g_pubg_fsm);
    printf("[12] 被击中 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    fsm_post_event(&g_pubg_fsm, EV_REVIVED);
    fsm_step(&g_pubg_fsm);
    printf("[13] 复活中 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    /* 等待复活结束回到游戏 */
    advance_ticks(35);
    printf("[14] 复活完成 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    /* 15) 击杀敌人，拾取物资 */
    fsm_post_event(&g_pubg_fsm, EV_ELIMINATED);
    fsm_step(&g_pubg_fsm);
    printf("[15] 击杀敌人 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    fsm_post_event(&g_pubg_fsm, EV_LOOT_FOUND);
    fsm_step(&g_pubg_fsm);
    printf("[16] 拾取物资 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    /* 16) 空投来临 -> 争夺空投 */
    fsm_post_event(&g_pubg_fsm, EV_AIRDROP_INCOMING);
    fsm_step(&g_pubg_fsm);
    printf("[17] 空投来临 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    fsm_post_event(&g_pubg_fsm, EV_AIRDROP_LOOTED);
    fsm_step(&g_pubg_fsm);
    printf("[18] 空投拾取 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    /* 17) 队友倒地 -> 复活 */
    fsm_post_event(&g_pubg_fsm, EV_TEAMMATE_DOWN);
    fsm_step(&g_pubg_fsm);
    printf("[19] 队友倒地 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    fsm_post_event(&g_pubg_fsm, EV_TEAMMATE_REVIVED);
    fsm_step(&g_pubg_fsm);
    printf("[20] 队友复活 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    /* 18) 随机事件（如毒圈变化） */
    fsm_post_event(&g_pubg_fsm, EV_RANDOM_EVENT);
    fsm_step(&g_pubg_fsm);
    printf("[21] 随机事件触发 → %s (safe_zone=%d, loot=%u, teammates=%u)\n",
           fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)),
           (int)g_pubg_data.safe_zone_active,
           (unsigned)g_pubg_data.loot_count,
           (unsigned)g_pubg_data.teammates_alive);

    fsm_post_event(&g_pubg_fsm, EV_RANDOM_EVENT);
    fsm_step(&g_pubg_fsm);
    printf("[22] 随机事件结束 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    /* 19) 决胜时刻，胜利 */
    fsm_post_event(&g_pubg_fsm, EV_VICTORY);
    fsm_step(&g_pubg_fsm);
    printf("[23] 胜利 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    /* 20) 汇总 */
    fsm_step(&g_pubg_fsm);
    printf("[24] 汇总 → %s\n", fsm_get_state_name(&g_pubg_fsm, fsm_get_current_state(&g_pubg_fsm)));

    printf("\n日志：\n");
    dump_log();
}

void pubg_run_scenario(void)
{
    run_scenario();
}

int main(void)
{
    pubg_run_scenario();
    return 0;
}
