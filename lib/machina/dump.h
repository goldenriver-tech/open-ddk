/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef NEBULA_SCHED_DUMP_H_
#define NEBULA_SCHED_DUMP_H_
#include <stdatomic.h>
#include <stdint.h>

#define CROSS_VM_DUMP_VERSION 0x0000000002000003ull

#define MAX_CRITICAL_PATH_COUNT 63

#define CROSS_VM_SCHED_CTRL_SIZE (0x1000)

#define CROSS_VM_DUMP_CTRL_OFFSET (CROSS_VM_SCHED_CTRL_SIZE)
#define CROSS_VM_DUMP_CTRL_SIZE (0x1000)

#define CROSS_VM_DUMP_SHARE_OFFSET \
  (CROSS_VM_DUMP_CTRL_OFFSET + CROSS_VM_DUMP_CTRL_SIZE)
#define CROSS_VM_DUMP_SHARE_SIZE (0x1000)

#define CROSS_VM_TOTAL_SIZE                             \
  (CROSS_VM_SCHED_CTRL_SIZE + CROSS_VM_DUMP_CTRL_SIZE + \
   CROSS_VM_DUMP_SHARE_SIZE)

enum {
  CVMD_CP_STATE_MONITORING = 0,  // monitoring timeout.
  CVMD_CP_STATE_PENDING = 1,     // timeout detected and waiting for dump.
  CVMD_CP_STATE_DUMPING = 2,     // critical path data is dumping.
};

enum {
  CROSS_VM_DUMP_VDEV_NOTIFY_G2H = 2,
  CROSS_VM_DUMP_VDEV_NOTIFY_H2G = 3,
};

typedef struct cvmd_cp {
  /* Output fields */
  uint32_t state;
  uint32_t mp_shm_off;
  uint32_t mp_shm_size;
  uint32_t tp_shm_off;
  uint32_t tp_shm_size;
  atomic_uint tp_unused;
  uint64_t last_ticks;
  uint64_t last_begin_count;

  /* Input fields */
  uint64_t timeout_ticks;

  /* Input&Output fields */
  atomic_ullong begin_count;
  atomic_ullong end_count;
} cvmd_cp_t;

typedef struct cvmd_ctl {
  /* Output fields */
  uint64_t version;

  /* Input&Output fields */
  uint8_t dump_irq_done;
  uint8_t dumper_done;
  uint8_t reserved1[6];

  /* Input fields */
  uint64_t cp_bitmap;

  uint8_t reserved2[40];

  /* critical paths */
  cvmd_cp_t cps[];
} cvmd_ctl_t;

typedef struct cvmd_mp {
  uint64_t value;
} cvmd_mp_t;

typedef struct cvmd_tp {
  uint64_t tpid;
  uint64_t key;
  uint64_t value;
} cvmd_tp_t;

extern cvmd_ctl_t* g_cvmd_ctl;
extern uint8_t* g_cvmd_share;
extern unsigned int g_cvmd_share_size;

void cvmd_init(uint64_t sched_shm_base, uint64_t sched_shm_size);
void cvmd_deinit();
static inline void cvmd_cp_begin(unsigned cpid) {
  cvmd_ctl_t* ctl = g_cvmd_ctl;
  cvmd_cp_t* cp;

  if (!ctl || cpid >= MAX_CRITICAL_PATH_COUNT) {
    return;
  }

  if (!(ctl->cp_bitmap & (1ULL << cpid))) {
    return;
  }

  cp = &ctl->cps[cpid];
  if (cp->state != CVMD_CP_STATE_MONITORING) {
    return;
  }

  atomic_fetch_add(&cp->begin_count, 1);
}

static inline void cvmd_cp_end(unsigned cpid) {
  cvmd_ctl_t* ctl = g_cvmd_ctl;
  cvmd_cp_t* cp;

  if (!ctl || cpid >= MAX_CRITICAL_PATH_COUNT) {
    return;
  }

  if (!(ctl->cp_bitmap & (1ULL << cpid))) {
    return;
  }

  cp = &ctl->cps[cpid];
  if (cp->state != CVMD_CP_STATE_MONITORING) {
    return;
  }

  atomic_fetch_add(&cp->end_count, 1);
}

static inline int cvmd_mp_set(unsigned cpid, unsigned mpid, uint64_t value) {
  cvmd_ctl_t* ctl = g_cvmd_ctl;
  cvmd_cp_t* cp;
  uint8_t* share;
  unsigned size;
  uint32_t mp_off;
  uint32_t mp_size;
  uint32_t mp_count;
  cvmd_mp_t* mps;

  if (!ctl || cpid >= MAX_CRITICAL_PATH_COUNT) {
    return -1;
  }

  if (!(ctl->cp_bitmap & (1ULL << cpid))) {
    return -2;
  }

  cp = &ctl->cps[cpid];
  mp_size = cp->mp_shm_size;
  if (!mp_size || cp->state != CVMD_CP_STATE_MONITORING) {
    return -3;
  }

  share = g_cvmd_share;
  size = g_cvmd_share_size;
  mp_off = cp->mp_shm_off;
  mp_count = mp_size / sizeof(cvmd_mp_t);

  if (!share || mp_off >= size || mp_off + mp_size >= size ||
      mpid >= mp_count) {
    return -4;
  }

  mps = (cvmd_mp_t*)(share + mp_off + mpid * sizeof(cvmd_mp_t));
  mps->value = value;

  return 0;
}

static inline int cvmd_mp_set_ticks(unsigned cpid, unsigned mpid) {
  cvmd_ctl_t* ctl = g_cvmd_ctl;
  cvmd_cp_t* cp;
  uint8_t* share;
  unsigned size;
  uint32_t mp_off;
  uint32_t mp_size;
  uint32_t mp_count;
  cvmd_mp_t* mps;
  uint64_t tk;

  if (!ctl || cpid >= MAX_CRITICAL_PATH_COUNT) {
    return -1;
  }

  if (!(ctl->cp_bitmap & (1ULL << cpid))) {
    return -2;
  }

  cp = &ctl->cps[cpid];
  mp_size = cp->mp_shm_size;
  if (!mp_size || cp->state != CVMD_CP_STATE_MONITORING) {
    return -3;
  }

  share = g_cvmd_share;
  size = g_cvmd_share_size;
  mp_off = cp->mp_shm_off;
  mp_count = mp_size / sizeof(cvmd_mp_t);

  if (!share || mpid >= mp_count || mp_off >= size ||
      mp_off + mp_size >= size) {
    return -4;
  }

  __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tk));

  mps = (cvmd_mp_t*)(share + mp_off + mpid * sizeof(cvmd_mp_t));
  mps->value = tk;

  return 0;
}

static inline int cvmd_tp_add(unsigned cpid,
                              unsigned tpid,
                              uint64_t key,
                              uint64_t value) {
  cvmd_ctl_t* ctl = g_cvmd_ctl;
  cvmd_cp_t* cp;
  uint8_t* share;
  unsigned size;
  uint32_t tp_off;
  uint32_t tp_size;
  uint32_t tp_count;
  cvmd_tp_t* tps;
  uint32_t slot;

  if (!ctl || cpid >= MAX_CRITICAL_PATH_COUNT) {
    return -1;
  }

  if (!(ctl->cp_bitmap & (1ULL << cpid))) {
    return -2;
  }

  cp = &ctl->cps[cpid];
  tp_size = cp->tp_shm_size;
  if (!tp_size || cp->state != CVMD_CP_STATE_MONITORING) {
    return -3;
  }

  share = g_cvmd_share;
  size = g_cvmd_share_size;
  tp_off = cp->tp_shm_off;

  if (!share || tp_off >= size || tp_off + tp_size >= size) {
    return -4;
  }

  tp_count = tp_size / sizeof(cvmd_tp_t);
  slot = atomic_fetch_add(&cp->tp_unused, 1) % tp_count;
  tps = (cvmd_tp_t*)(share + tp_off + slot * sizeof(cvmd_tp_t));
  tps->tpid = tpid;
  tps->key = key;
  tps->value = value;

  return 0;
}

static inline int cvmd_tp_add_ticks(unsigned cpid,
                                    unsigned tpid,
                                    uint64_t key) {
  cvmd_ctl_t* ctl = g_cvmd_ctl;
  cvmd_cp_t* cp;
  uint8_t* share;
  unsigned size;
  uint32_t tp_off;
  uint32_t tp_size;
  uint32_t tp_count;
  cvmd_tp_t* tps;
  uint32_t slot;
  uint64_t tk;

  if (!ctl || cpid >= MAX_CRITICAL_PATH_COUNT) {
    return -1;
  }

  if (!(ctl->cp_bitmap & (1ULL << cpid))) {
    return -2;
  }

  cp = &ctl->cps[cpid];
  tp_size = cp->tp_shm_size;
  if (!tp_size || cp->state != CVMD_CP_STATE_MONITORING) {
    return -3;
  }

  share = g_cvmd_share;
  size = g_cvmd_share_size;
  tp_off = cp->tp_shm_off;

  if (!share || tp_off >= size || tp_off + tp_size >= size) {
    return -4;
  }

  tp_count = tp_size / sizeof(cvmd_tp_t);
  slot = atomic_fetch_add(&cp->tp_unused, 1) % tp_count;
  tps = (cvmd_tp_t*)(share + tp_off + slot * sizeof(cvmd_tp_t));

  __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tk));

  tps->tpid = tpid;
  tps->key = key;
  tps->value = tk;

  return 0;
}

#endif /* NEBULA_SCHED_DUMP_H_ */
