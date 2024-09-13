/**
 ******************************************************************************
 *
 *  @file bl_debugfs.h
 *
 *  Copyright (C) BouffaloLab 2017-2024
 *
 *  Licensed under the Apache License, Version 2.0 (the License);
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an ASIS BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************
 */



#ifndef _BL_DEBUGFS_H_
#define _BL_DEBUGFS_H_

#include <linux/workqueue.h>

struct bl_hw;
struct bl_sta;

#ifdef CONFIG_BL_DEBUGFS

struct bl_debugfs {
    unsigned long long rateidx;
    struct dentry *dir;
    bool trace_prst;

    char helper_cmd[64];
    struct work_struct helper_work;
    bool helper_scheduled;
    spinlock_t umh_lock;

    bool unregistering;

#ifdef CONFIG_BL_FULLMAC
    struct work_struct rc_stat_work;
    uint8_t rc_sta[NX_REMOTE_STA_MAX];
    uint8_t rc_write;
    uint8_t rc_read;
    struct dentry *dir_rc;
    struct dentry *dir_sta[NX_REMOTE_STA_MAX];
#endif
};

int bl_dbgfs_register(struct bl_hw *bl_hw, const char *name);
void bl_dbgfs_unregister(struct bl_hw *bl_hw);
int bl_um_helper(struct bl_debugfs *bl_debugfs, const char *cmd);
int bl_trigger_um_helper(struct bl_debugfs *bl_debugfs);
#ifdef CONFIG_BL_FULLMAC
void bl_dbgfs_register_rc_stat(struct bl_hw *bl_hw, struct bl_sta *sta);
void bl_dbgfs_unregister_rc_stat(struct bl_hw *bl_hw, struct bl_sta *sta);
#endif

void bl_dump_trace(struct bl_hw *bl_hw);
void bl_reset_trace(struct bl_hw *bl_hw);

#else

struct bl_debugfs {
};

static inline int bl_dbgfs_register(struct bl_hw *bl_hw, const char *name) { return 0; }
static inline void bl_dbgfs_unregister(struct bl_hw *bl_hw) {}
static inline int bl_um_helper(struct bl_debugfs *bl_debugfs, const char *cmd) { return 0; }
static inline int bl_trigger_um_helper(struct bl_debugfs *bl_debugfs) { return 0; }
#ifdef CONFIG_BL_FULLMAC
static inline void bl_dbgfs_register_rc_stat(struct bl_hw *bl_hw, struct bl_sta *sta)  {}
static inline void bl_dbgfs_unregister_rc_stat(struct bl_hw *bl_hw, struct bl_sta *sta)  {}
#endif

static inline void bl_dump_trace(struct bl_hw *bl_hw) {}
static inline void bl_reset_trace(struct bl_hw *bl_hw) {}

#endif /* CONFIG_BL_DEBUGFS */


#endif /* _BL_DEBUGFS_H_ */
