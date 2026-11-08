/*
 * Declarations of quicknote_store.c.
 */
#ifndef G1_SERVICES_STORAGE_QUICKNOTE_STORE_H_
#define G1_SERVICES_STORAGE_QUICKNOTE_STORE_H_

#include "services/storage/storage.h"

int clearQuickNoteQueue(void);
int upgradeQuickNoteDataToFlash(int index);
int deleteQuickNoteData(int index);
int getQuickNoteDataFromFlash(uint32_t index, void *title, int title_len, void *text, int text_len);
int getQuickNoteQueueNum(void);
int QuickNoteStoreHandler(void);

#endif /* G1_SERVICES_STORAGE_QUICKNOTE_STORE_H_ */
