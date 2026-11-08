/*
 * Declarations of setting_store.c.
 */
#ifndef G1_SERVICES_STORAGE_SETTING_STORE_H_
#define G1_SERVICES_STORAGE_SETTING_STORE_H_

#include "services/storage/storage.h"

extern struct app_setting CURRENT_LANGUAGE;
int clearSettingStorageQueue(void);
int upgradeAppLanguageInfoToFlash(uint8_t language);
int cleanAppLanguageInfo(void);
int upgradeDashboardStartupModeInfoToFlash(uint8_t mode);
int cleanDashBoardStartUpModeInfo(void);
int upgradeDoubleTapCustomizeToFlash(uint32_t mode);
int upgradeLongpressTapCustomizeToFlash(uint32_t mode);
uint8_t getAppLanguageInfofromFlash(void);
uint8_t getDashboardStartUpModeInfofromFlash(void);
uint8_t get_current_language(void);
uint8_t get_dashboard_startup_mode(void);
int getSettingStorageQueueNum(void);
int SettingStoreHandler(void);

#endif /* G1_SERVICES_STORAGE_SETTING_STORE_H_ */
