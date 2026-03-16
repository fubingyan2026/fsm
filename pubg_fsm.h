/**
 * @file    pubg_fsm.h
 * @brief   "和平精英" 场景示例：一个较复杂的 FSM 流程
 *
 * 本例展示了一个典型的射击游戏流程（从大厅到匹配，到战斗，再到胜利/失败）
 * 以及如何用 FSM 的状态、事件、超时等功能来建模。
 */

#ifndef PUBG_FSM_H
#define PUBG_FSM_H

#include "fsm.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 游戏状态（作为 FSM 状态 ID）
 */
typedef enum
{
    /* 根状态 */
    ST_MENU = 0,        /* 大厅 / 主菜单 */
    ST_MATCHMAKING,     /* 匹配中 */
    ST_LOADING,         /* 加载中 */

    /* 跳伞阶段（HSM 父状态） */
    ST_PARACHUTE,       /* 跳伞阶段（父状态） */
    ST_DROP_SELECTION,  /* - 子状态：选择落点 */
    ST_FALLING,         /* - 子状态：下落 */
    ST_LANDING,         /* - 子状态：着陆 */

    /* 游戏内阶段（HSM 父状态） */
    ST_IN_GAME,         /* 进入游戏 */
    ST_SAFE_ZONE,       /* - 子状态：安全区收缩 */
    ST_COMBAT,          /* - 父状态：战斗阶段 */
    ST_ENEMY_SQUAD_A,   /*   - 子状态：遇到敌方小队 A */
    ST_ENEMY_SQUAD_B,   /*   - 子状态：遇到敌方小队 B */
    ST_ENEMY_SQUAD_C,   /*   - 子状态：遇到敌方小队 C */

    ST_LOOTING,         /* 搜刮物资 */
    ST_AIRDROP,         /* 空投 */
    ST_TEAMMATE,        /* 队友支援 */
    ST_RANDOM_EVENT,    /* 随机事件 */
    ST_KNOCKED,         /* 被击倒 */
    ST_REVIVING,        /* 复活中 */
    ST_WIN,             /* 胜利 */
    ST_LOSE,            /* 失败 */
    ST_SUMMARY,         /* 总结 */
    ST_COUNT
} pubg_state_t;

/**
 * 事件类型（FSM 事件）
 */
#define EV_START_MATCH        ((fsm_event_t)0x01U) /* 发起匹配 */
#define EV_MATCH_FOUND        ((fsm_event_t)0x02U) /* 匹配成功 */
#define EV_LOAD_DONE          ((fsm_event_t)0x03U) /* 加载完成 */
#define EV_DROP_POINT_SELECTED ((fsm_event_t)0x04U) /* 选择落点 */
#define EV_PARACHUTE_OPEN     ((fsm_event_t)0x05U) /* 开伞 */
#define EV_LANDED             ((fsm_event_t)0x06U) /* 着陆 */
#define EV_ZONE_CLOSE         ((fsm_event_t)0x07U) /* 安全区收缩完成 */
#define EV_ENEMY_SQUAD_A      ((fsm_event_t)0x08U) /* 遇到敌方小队 A */
#define EV_ENEMY_SQUAD_B      ((fsm_event_t)0x09U) /* 遇到敌方小队 B */
#define EV_ENEMY_SQUAD_C      ((fsm_event_t)0x0AU) /* 遇到敌方小队 C */
#define EV_SUPPORT_CALL       ((fsm_event_t)0x0BU) /* 请求支援（切换目标） */
#define EV_HIT                ((fsm_event_t)0x0CU) /* 受伤 */
#define EV_KNOCKED            ((fsm_event_t)0x0DU) /* 被击倒 */
#define EV_REVIVED            ((fsm_event_t)0x0EU) /* 被队友复活 */
#define EV_ELIMINATED         ((fsm_event_t)0x0FU) /* 击杀敌人 */
#define EV_VICTORY            ((fsm_event_t)0x10U) /* 获胜 */
#define EV_GAME_OVER          ((fsm_event_t)0x11U) /* 游戏结束 */
#define EV_EXIT_GAME          ((fsm_event_t)0x12U) /* 退出游戏 */
#define EV_LOOT_FOUND         ((fsm_event_t)0x13U) /* 发现物资 */
#define EV_AIRDROP_INCOMING   ((fsm_event_t)0x14U) /* 空投到达 */
#define EV_AIRDROP_LOOTED     ((fsm_event_t)0x15U) /* 拾取空投 */
#define EV_TEAMMATE_DOWN      ((fsm_event_t)0x16U) /* 队友倒地 */
#define EV_TEAMMATE_REVIVED   ((fsm_event_t)0x17U) /* 队友复活 */
#define EV_RANDOM_EVENT       ((fsm_event_t)0x18U) /* 随机事件触发 */

/**
 * 用户数据结构
 */
typedef struct
{
    uint32_t tick_count;          /* 经过的 tick 数（模拟时间） */
    uint32_t kills;               /* 已击杀敌人数 */
    uint32_t team_kills;          /* 队友击杀数 */
    uint32_t teammates_alive;     /* 仍存活的队友数 */
    uint32_t enemy_squads;        /* 遭遇的敌方小队数 */
    uint32_t loot_count;          /* 当前持有物资数量 */
    bool     safe_zone_active;    /* 是否处于安全区阶段 */
    bool     airdrop_active;      /* 是否有空投可拾取 */
    bool     random_event_active; /* 是否处于随机事件中 */
    bool     in_combat;           /* 是否正在战斗中 */
    bool     knocked_down;        /* 是否被击倒 */
    bool     parachute_opened;    /* 是否已开伞 */
    bool     drop_point_selected; /* 是否已选择落点 */
    bool     landed;              /* 是否已着陆 */
    uint8_t  target_squad;        /* 目标敌方小队：1=A，2=B，3=C，0=无 */
} pubg_user_data_t;

/**
 * 运行示例场景，输出状态转换日志。
 */
void pubg_run_scenario(void);

#ifdef __cplusplus
}
#endif

#endif /* PUBG_FSM_H */
