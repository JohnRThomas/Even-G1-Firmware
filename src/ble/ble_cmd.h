/*
 * BLE application protocol command ids (byte 0 of every phone <-> glasses packet).
 *
 * ble_process_req_dispatch routes by range: 0x01..0x27 PUT, 0x29..0x45 GET,
 * 0x47..0x56 POST, 0xF1..0xF6 report.
 * 0x23 is never a command: it is ASCII '#', the prefix of the debug strings handled by
 * spec_ble_command_hook.
 */
#ifndef BLE_CMD_H_
#define BLE_CMD_H_

#include <zephyr/toolchain.h>

enum ble_cmd {
  /* PUT (ble_process_put_req, local part in master_process_put_req) */
  BLE_CMD_BRIGHTNESS_SET = 0x01,
  BLE_CMD_ANTI_SHAKE_SET = 0x02,          /* GET 0x2A reads it back */
  BLE_CMD_SILENT_MODE_SET = 0x03,
  BLE_CMD_NOTIFI_WHITELIST_SET = 0x04,
  BLE_CMD_DEBUG_CONTROL = 0x05,           /* head up/down simulation, LOG_LEVEL, dashboard mode */
  BLE_CMD_DASHBOARD_SET = 0x06,
  BLE_CMD_COUNTDOWN_TIMER = 0x07,
  BLE_CMD_HEAD_UP_ACTION_SET = 0x08,
  BLE_CMD_TELEPROMPTER_INFO = 0x09,
  BLE_CMD_NAVIGATION_INFO = 0x0a,
  BLE_CMD_HEAD_UP_ANGLE_SET = 0x0b,
  BLE_CMD_TRANSLATE_SRC_TEXT = 0x0d,      /* translate, source text */
  BLE_CMD_MICROPHONE_SET = 0x0e,
  BLE_CMD_TRANSLATE_DST_TEXT = 0x0f,      /* translate, translated text */
  BLE_CMD_HEAD_UP_CALIBRATION = 0x10,
  /* Vendor leftover: the PUT handler rejects it, so its master-side case (which stores 7, the countdown
   * save) and the storage thread's case (ship mode after 10 s) never run. */
  BLE_CMD_SHIP_MODE = 0x11,
  BLE_CMD_NOTIFY_EN = 0x14,               /* ANCS on/off; GET 0x38 reads it back */
  BLE_CMD_FILE_UPLOAD = 0x15,
  BLE_CMD_BITMAP_SHOW = 0x16,             /* CRC check of the uploaded file */
  BLE_CMD_UPGRADE_CONTROL = 0x17,         /* erase DFU */
  BLE_CMD_BITMAP_HIDE = 0x18,
  BLE_CMD_TRANSLATE_CONTROL = 0x1c,       /* languages, start/pause */
  BLE_CMD_QUICK_NOTE_CONTROL = 0x1e,
  BLE_CMD_TUTORIAL_CONTROL = 0x1f,        /* onboarding */
  BLE_CMD_FILE_UPLOAD_COMPLETE = 0x20,
  BLE_CMD_STATUS = 0x22,
  BLE_CMD_TELEPROMPTER_SUSPEND = 0x24,
  BLE_CMD_TELEPROMPTER_POSITION_SET = 0x25,
  BLE_CMD_HARDWARE_SET = 0x26,            /* sub-commands: enum ble_hw_sub */
  BLE_CMD_WEAR_DETECTION_SET = 0x27,

  /* GET (ble_process_get_req) */
  BLE_CMD_BRIGHTNESS_GET = 0x29,
  BLE_CMD_ANTI_SHAKE_GET = 0x2a,
  BLE_CMD_SILENT_MODE_GET = 0x2b,
  BLE_CMD_DEVICE_INFO_GET = 0x2c,
  BLE_CMD_MAC_GET = 0x2d,
  BLE_CMD_NOTIFI_WHITELIST_GET = 0x2e,
  BLE_CMD_HEAD_UP_ANGLE_GET = 0x32,
  BLE_CMD_GLASSES_SN_GET = 0x33,
  BLE_CMD_DEVICE_SN_GET = 0x34,
  BLE_CMD_ESB_CHANNEL_GET = 0x35,
  BLE_CMD_NOTIF_COUNT_GET = 0x36,
  BLE_CMD_TIME_SINCE_BOOT_GET = 0x37,
  BLE_CMD_ANCS_ENABLE_GET = 0x38,
  BLE_CMD_RUNNING_APP_GET = 0x39,
  BLE_CMD_WEAR_DETECTION_GET = 0x3a,
  BLE_CMD_RASTER_CONFIG_GET = 0x3b,
  BLE_CMD_NOTIF_AUTO_DISPLAY_GET = 0x3c,
  BLE_CMD_LANGUAGE_SET = 0x3d,            /* handled in the GET range */
  /* Usage statistics ("burial points").  The vendor firmware collected them and flushed them to flash from the
   * storage thread; 1.6.7 removed the tracking, and the request is answered with an empty record. */
  BLE_CMD_BURIAL_POINT_GET = 0x3e,
  BLE_CMD_HARDWARE_GET = 0x3f,            /* sub-commands 5..8 of enum ble_hw_sub */

  /* POST (ble_process_post_req) */
  BLE_CMD_BT_UNPAIR = 0x47,
  BLE_CMD_POST_NOOP = 0x49,               /* acknowledged, no effect; the app never sends it */
  BLE_CMD_SYNC_DISPLAY = 0x4a,            /* flags 2 = ESB sync packet to the slave, 1 = display refresh */
  BLE_CMD_NOTIFICATION_SEND = 0x4b,
  BLE_CMD_NOTIFICATION_CLEAR = 0x4c,
  BLE_CMD_MTU_SET = 0x4d,                 /* marks the app session as established */
  BLE_CMD_TEXT_SET = 0x4e,
  BLE_CMD_NOTIF_AUTO_DISPLAY_SET = 0x4f,
  BLE_CMD_DASHBOARD_LOCK_SET = 0x50,
  BLE_CMD_QUICK_NOTE_UPDATE_TASK = 0x51,
  BLE_CMD_TRANSCRIBE_CONTROL = 0x52,
  BLE_CMD_TRANSCRIBE_STATUS = 0x53,
  BLE_CMD_EVENAI = 0x54,
  BLE_CMD_EVENAI_STATUS = 0x55,
  BLE_CMD_EVENAI_EXIT = 0x56,

  /* report (ble_process_report_response), glasses -> phone */
  BLE_CMD_AUDIO = 0xf1,
  BLE_CMD_DEBUG = 0xf4,
  BLE_CMD_EVENT = 0xf5,
  BLE_CMD_WHITELIST_APP_ADD = 0xf6,       /* app of a notification that is not in the whitelist */

  /* limits of the ranges ble_process_req_dispatch and master_process_put_req route by */
  BLE_CMD_PUT_FIRST = BLE_CMD_BRIGHTNESS_SET,
  BLE_CMD_PUT_LAST = BLE_CMD_WEAR_DETECTION_SET,
  BLE_CMD_GET_FIRST = BLE_CMD_BRIGHTNESS_GET,
  BLE_CMD_GET_LAST = 0x45,
  BLE_CMD_POST_FIRST = BLE_CMD_BT_UNPAIR,
  BLE_CMD_POST_LAST = BLE_CMD_EVENAI_EXIT,
  BLE_CMD_REPORT_FIRST = BLE_CMD_AUDIO,
  BLE_CMD_REPORT_LAST = BLE_CMD_WHITELIST_APP_ADD,
};

/* Dashboard Set (0x06) sub-commands, data[4] */
enum ble_dashboard_sub {
  DASHBOARD_SUB_TIME_WEATHER = 1,
  DASHBOARD_SUB_WEATHER = 2,
  DASHBOARD_SUB_PANE_CALENDAR = 3,
  DASHBOARD_SUB_PANE_STOCK = 4,
  DASHBOARD_SUB_PANE_NEWS = 5,
  DASHBOARD_SUB_PANE_MODE = 6,
  DASHBOARD_SUB_PANE_CANVAS = 7, /* citywalk / map */
};

/* Hardware Set (0x26) sub-commands, data[4]; Hardware Get (0x3F) reads 5..8 back, data[1] */
enum ble_hw_sub {
  HW_SUB_USER_DATA_RESET = 1,      /* sets user_data_reset_pending */
  HW_SUB_HEIGHT_DEPTH = 2,
  HW_SUB_GIVE_MIC_TRANSM_SEM = 3,
  HW_SUB_LUM_GEAR = 4,
  HW_SUB_DOUBLE_TAP_ACTION = 5,
  HW_SUB_LUM_COEFFICIENT = 6,      /* calibration mode */
  HW_SUB_LONG_PRESS_ENABLE = 7,
  HW_SUB_MIC_ON_HEAD_UP = 8,
};

/* Debug Control (0x05) sub-commands, data[1]; the argument follows in data[2] */
enum ble_debug_control_sub {
  DEBUG_CONTROL_SUB_IMU_STATE = 1,      /* head up / down simulation (trigger_imu_state_change) */
  DEBUG_CONTROL_SUB_LOG_LEVEL = 2,
  DEBUG_CONTROL_SUB_DASHBOARD_MODE = 3, /* sets dashboard_mode */
};

/* Head Up Action Set (0x08) sub-commands, data[4]: what the dashboard does on head up */
enum ble_head_up_action_sub {
  HEAD_UP_ACTION_SUB_SET = 3, /* SendDashBoardStartupModeInfoToSlave */
  HEAD_UP_ACTION_SUB_GET = 4, /* get_dashboard_startup_mode */
};

/*
 * Teleprompter sub-commands, data[4] (the logs call it the action id): Teleprompter Info (0x09)
 * takes all but two of them, Teleprompter Suspend (0x24) only SUSPEND and Teleprompter Position Set
 * (0x25) only APP_SYNC.
 */
enum ble_teleprompter_sub {
  TELEPROMPTER_SUB_INIT = 1,
  TELEPROMPTER_SUB_MARK_POSITION = 2,
  TELEPROMPTER_SUB_TEXT_UPGRADE = 3,
  TELEPROMPTER_SUB_APP_SYNC = 4,
  TELEPROMPTER_SUB_EXIT = 5,
  TELEPROMPTER_SUB_SUSPEND = 6,
  TELEPROMPTER_SUB_TEXT_UPGRADE_SCROLL = 7, /* text upgrade that also sets tp_scroll_pending */
};

/*
 * Navigation Info (0x0A) sub-commands, data[4].  struct nav_info.type keeps the last one that brought
 * navigation data.
 */
enum ble_navigation_sub {
  NAVIGATION_SUB_STARTUP = 0,
  NAVIGATION_SUB_TRIP_STATUS = 1, /* direction, position and the five trip texts */
  NAVIGATION_SUB_OVERVIEW_MAP = 2,
  NAVIGATION_SUB_PANORAMIC_MAP = 3,
  NAVIGATION_SUB_APP_SYNC = 4,
  NAVIGATION_SUB_EXIT = 5,
  NAVIGATION_SUB_ARRIVED = 6,     /* arrived status and prompt */
} __packed;
BUILD_ASSERT(sizeof(enum ble_navigation_sub) == 1);

/* Head Up Calibration (0x10) sub-commands, data[4] */
enum ble_head_up_calibration_sub {
  HEAD_UP_CALIBRATION_SUB_CLEAR = 1, /* clears the user's horizontal pitch */
  HEAD_UP_CALIBRATION_SUB_SET = 2,   /* calibration screen on / off, takes the current pitch */
};

/*
 * Quick Note Control (0x1E) sub-commands, data[4].  The glasses send the audio info and the audio
 * file packets with the same sub-command.
 */
enum ble_quick_note_sub {
  QUICK_NOTE_SUB_AUDIO_INFO = 1,     /* list of the recordings (requestAudioInfoToApp) */
  QUICK_NOTE_SUB_AUDIO_FILE = 2,     /* download of a recording (sendAudioStreamFileToApp) and its receipt */
  QUICK_NOTE_SUB_NOTE_DATA = 3,      /* title and text of a note slot, empty to delete it */
  QUICK_NOTE_SUB_AUDIO_DELETE = 4,   /* one recording or all (delAudioStreamRecord) */
  QUICK_NOTE_SUB_AUDIO_CLEAN = 5,    /* all recordings */
  QUICK_NOTE_SUB_SHOW_START = 7,     /* opens the quick note screen (quicknote_show_info) */
  QUICK_NOTE_SUB_SHOW_TEXT = 8,      /* result code, scroll line and text for the screen */
  QUICK_NOTE_SUB_SHOW_HEARTBEAT = 9, /* restarts the screen's timeout, answers whether it is open */
  QUICK_NOTE_SUB_SHOW_RESULT = 10,   /* result code alone */
};

/* Status (0x22) sub-commands, data[4], in the request and in the packets the glasses send unasked */
enum ble_status_sub {
  STATUS_SUB_MESSAGE_COUNT = 1, /* send_message_count_to_app */
  STATUS_SUB_KEY_EVENT = 2,     /* sync_dashboard_key_event_to_app, glasses -> phone only */
};

/* Transcribe Control (0x52) sub-commands, data[4]; Transcribe Status (0x53) takes only STATUS */
enum ble_transcribe_sub {
  TRANSCRIBE_SUB_MODE = 1, /* stop / start / pause / resume */
  TRANSCRIBE_SUB_TEXT = 2,
  TRANSCRIBE_SUB_STATUS = 3,
};

/*
 * Even AI sub-commands, data[4]: Even AI (0x54) takes MODE .. STATUS, Even AI Status (0x55) only
 * STATUS and Even AI Exit (0x56) only EXIT.
 */
enum ble_evenai_sub {
  EVENAI_SUB_MODE = 1,
  EVENAI_SUB_LISTEN_TEXT = 2,
  EVENAI_SUB_ANSWER_TEXT = 3,
  EVENAI_SUB_COMMAND_TEXT = 4,
  EVENAI_SUB_STATUS = 5,
  EVENAI_SUB_EXIT = 8,
};

/* Device Info Get (0x2C) argument, data[1]: the phone the request comes from */
enum ble_phone_os {
  PHONE_OS_ANDROID = 1,
  PHONE_OS_IOS = 2,
};

/*
 * Status byte of a reply, behind the command id or behind the echoed header.  The packets the
 * firmware queues for its own BLE work thread (0xF1 / 0xF4 / 0xF5) carry it in byte 2.
 */
enum ble_resp {
  BLE_RESP_OK = 0xc9,
  BLE_RESP_ERR = 0xca,
  BLE_RESP_MORE = 0xcb,      /* chunk taken, the message is not complete yet */
  BLE_RESP_DMIC_SEND = 0xcc, /* never a reply: 0xF1 packet that makes the work thread send the queued audio */
};

/*
 * Event (0xF5) code, byte 1 of the packet: what the glasses report to the phone unasked.
 * report_event_to_phone() queues one for the BLE work thread (last_ble_cmd), deal_event_to_phone()
 * sends it; byte 2 is a payload where noted.  The EVENT_* names are the vendor's, from its logs.
 */
enum ble_event {
  BLE_EVT_DOUBLE_TAP = 0x00,              /* also when the glasses close the running screen themselves */
  BLE_EVT_SINGLE_TAP = 0x01,
  BLE_EVT_HEAD_UP = 0x02,
  BLE_EVT_HEAD_DOWN = 0x03,
  BLE_EVT_NOT_DISTURB_ENTER = 0x04,       /* silent mode on: triple tap, or the battery is too hot or cold */
  BLE_EVT_NOT_DISTURB_EXIT = 0x05,        /* silent mode off: triple tap */
  BLE_EVT_WORN = 0x06,                    /* EVENT_ENTER_WARED */
  BLE_EVT_UNWORN_OUT_OF_BOX = 0x07,       /* EVENT_UNWARED_OUTBOX: taken off, not in the case */
  BLE_EVT_IN_BOX_OPEN = 0x08,             /* EVENT_PUT_IN_GLASS_BOX_OPEN: in the case, lid open */
  BLE_EVT_CHARGING = 0x09,                /* EVENT_STATE_CHARGING; payload 0 / 1 */
  BLE_EVT_BATTERY_PERCENT = 0x0a,         /* EVENT_STATE_BATTERY_PERCENT; payload 0..100 */
  BLE_EVT_IN_BOX_CLOSED = 0x0b,           /* EVENT_PUT_IN_GLASS_BOX_CLOSE: in the case, lid closed */
  BLE_EVT_BOX_STATUS = 0x0e,              /* EVENT_GLASS_BOX_STATUS; payload: charging status of the case */
  BLE_EVT_BOX_SOC = 0x0f,                 /* EVENT_GLASS_BOX_SOC; payload: battery of the case, 0..100 */
  BLE_EVT_ONBOARDING_HEAD_UP_HINT = 0x10, /* the "Head up to see what it looks like" step is showing */
  BLE_EVT_BIND_FINISHED = 0x11,           /* EVENT_GLASS_HAS_FINISH_BIND */
  BLE_EVT_BRIGHTNESS_LEVEL = 0x12,        /* auto brightness changed the level; payload: the level, master only */
  BLE_EVT_ONBOARDING_DASHBOARD_SHOWN = 0x13,         /* head up opened the dashboard of the last step */
  BLE_EVT_ONBOARDING_DASHBOARD_CLOSED = 0x14,        /* head down closed it again; repeated on the closing page */
  BLE_EVT_ONBOARDING_NOTIFICATION_SHOWN = 0x15,      /* head up opened the sample notification */
  BLE_EVT_ONBOARDING_NOTIFICATION_DISMISSED = 0x16,  /* head down dismissed it */
  BLE_EVT_LONG_PRESS = 0x17,              /* touchpad of the slave arm held with the phone connected */
  BLE_EVT_LONG_PRESS_RELEASE = 0x18,      /* it was released, or Even AI closed the microphone */
  BLE_EVT_ONBOARDING_DOUBLE_TAP = 0x19,
  BLE_EVT_DASHBOARD_START = 0x1e,
  BLE_EVT_DASHBOARD_EXIT = 0x1f,
  BLE_EVT_DOUBLE_TAP_CUSTOMIZE = 0x20,    /* double tap with a customised action: the phone starts it */

  /*
   * Not sent as events: deal_event_to_phone() answers the last chunk of the named text command with
   * them, once the text is drawn.
   */
  BLE_EVT_ACK_TRANSLATE_SRC_TEXT = 0xf0,
  BLE_EVT_ACK_TRANSLATE_DST_TEXT = 0xf1,
  BLE_EVT_ACK_TEXT_SET = 0xf2,

  BLE_EVT_NONE = 0xff,                    /* no event pending; no wear state reported yet */
} __packed;
BUILD_ASSERT(sizeof(enum ble_event) == 1);

/* highest panel brightness level of Brightness Set (0x01) and of the "#i" debug command */
#define BLE_BRIGHTNESS_LEVEL_MAX 0x2a

#endif /* BLE_CMD_H_ */
