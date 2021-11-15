/*
 * Copyright (C) 2021 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#define MAX_BUF_SIZE 256

enum MODE_CMD {
    SET_CUR_VALUE = 0,
    GET_CUR_VALUE,
    GET_DEF_VALUE,
    GET_MIN_VALUE,
    GET_MAX_VALUE,
    GET_MODE_VALUE,
    RESET_MODE,
    SET_LONG_VALUE,
};

enum  MODE_TYPE {
    Touch_Game_Mode        = 0,
    Touch_Active_MODE      = 1,
#ifdef CONFIG_NEW_TOUCH_GAMEMODE
    Touch_Tap_Sensitivity	    = 2,
    Touch_Follow_Performance    = 3,
    Touch_Aim_Sensitivity       = 4,
    Touch_Tap_Stability         = 5,
    Touch_Expert_Mode           = 6,
#else
    Touch_UP_THRESHOLD     = 2,
    Touch_Tolerance        = 3,
    Touch_Wgh_Min          = 4,
    Touch_Wgh_Max          = 5,
    Touch_Wgh_Step         = 6,
#endif
    Touch_Edge_Filter      = 7,
    Touch_Panel_Orientation = 8,
    Touch_Report_Rate      = 9,
    Touch_Fod_Enable       = 10,
    Touch_Aod_Enable       = 11,
    Touch_Resist_RF        = 12,
    Touch_Idle_Time        = 13,
    Touch_Doubletap_Mode   = 14,
    Touch_Grip_Mode        = 15,
    Touch_FodIcon_Enable   = 16,
    Touch_Nonui_Mode       = 17,
    Touch_Debug_Level      = 18,
    Touch_Power_Status     = 19,
    Touch_Mode_NUM         = 20,
};

#define TOUCH_DEV_PATH "/dev/xiaomi-touch"
#define TOUCH_MAGIC 0x5400

#define TOUCH_IOC_SET_CUR_VALUE TOUCH_MAGIC + SET_CUR_VALUE
#define TOUCH_IOC_GET_CUR_VALUE TOUCH_MAGIC + GET_CUR_VALUE
#define TOUCH_IOC_GET_DEF_VALUE TOUCH_MAGIC + GET_DEF_VALUE
#define TOUCH_IOC_GET_MIN_VALUE TOUCH_MAGIC + GET_MIN_VALUE
#define TOUCH_IOC_GET_MAX_VALUE TOUCH_MAGIC + GET_MAX_VALUE
#define TOUCH_IOC_GET_MODE_VALUE TOUCH_MAGIC + GET_MODE_VALUE
#define TOUCH_IOC_RESET_MODE TOUCH_MAGIC + RESET_MODE
#define TOUCH_IOC_SET_LONG_VALUE TOUCH_MAGIC + SET_LONG_VALUE
