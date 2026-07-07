// SPDX-License-Identifier: BSD-3-Clause

#include <map>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <cstdio>

#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include <lib/fxl/strings/string_printf.h>
#include <lib/fxl/logging.h>

#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"
#include "vmlog_store.h"

namespace {
// TODO: investigate rename log file issue under /data/vmlog sub directory
// constexpr char kVmlogStorageDir[] = "/data/vmlog";
constexpr char kVmlogStorageDir[] = "/data";

// TODO: read vm name table from config file
// format: {vmid, vm_name}
std::map<int32_t, std::string> vm_names = {
    {-1, "yocto"},
    {0, "alps"},
    {1, "tbox"},
};

std::string ErrnoString(int err) {
  return fxl::StringPrintf("%s(%d)", strerror(err), err);
}

} // namespace

void VmlogStore::WriteDlogLocked(const char* line_buffer, int len, uint32_t flags) {
  int n = snprintf(log_with_prefix_, sizeof(log_with_prefix_),
                   "[VM:%2d]%.*s", vmid_, len, line_buffer);
  if (n > (int)sizeof(log_with_prefix_)) {
    n = sizeof(log_with_prefix_);
  }

  if (n < 0) {
    FXL_LOG(WARNING) << "snprintf error: " << n;
    return;
  }

  zx_status_t status = dlog_.write(n, log_with_prefix_, flags);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "dlog write error: " << status;
  }
}

void VmlogStore::WriteFileLocked(const char* line_buffer, int len) {
  if (!log_fp_) {
    FXL_LOG(ERROR) << "unable to open file: " << log_file_ << ", " << ErrnoString(errno);
    return;
  }

  zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
  size_t n = snprintf(log_with_prefix_, sizeof(log_with_prefix_), "[%05d.%03d][VM:%2d]%.*s",
                   (int) (now / ZX_SEC(1)),
                   (int) ((now / ZX_MSEC(1)) % 1000ULL),
                   vmid_, len, line_buffer);
  if (n > sizeof(log_with_prefix_)) {
    n = sizeof(log_with_prefix_);
  }

  if (n < 0) {
    FXL_LOG(WARNING) << "snprintf error: " << n;
    return;
  }

  size_t written = fwrite(log_with_prefix_, 1, n, log_fp_);
  if (written != n) {
    FXL_LOG(WARNING) << "vmlog file write error: " << ErrnoString(errno);
  }

  log_size_counter_.fetch_add(written);
}

void VmlogStore::WriteLog(const char* log, size_t len) {
  std::lock_guard<std::mutex> guard(store_lock_);
  int n = (int)len;

  if (unlikely(shutdown_)) {
    FXL_LOG(WARNING) << "log store is shutdown, ignore log";
    return;
  }

  if (n > VMLOG_STORE_LINE_BUFFER_SIZE) {
    FXL_LOG(WARNING) << "log line too long, truncate to "
                     << VMLOG_STORE_LINE_BUFFER_SIZE;
    n = VMLOG_STORE_LINE_BUFFER_SIZE;
  }

  if (flags_ & (TO_SERIAL | TO_CONSOLE)) {
    // default do not save vmlog to nebula log file
    uint32_t dlog_flags = ZX_LOG_FLAG_NO_FILE;

    dlog_flags |= (flags_ & TO_SERIAL) ? 0 : ZX_LOG_FLAG_NO_SERIAL;
    dlog_flags |= (flags_ & TO_CONSOLE) ? 0 : ZX_LOG_FLAG_NO_CONSOLE;
    WriteDlogLocked(log, n, dlog_flags);
  }

  if (flags_ & TO_FILE) {
    WriteFileLocked(log, n);
  }
}

void VmlogStore::LogFileSyncLoop(void) {
  int fd = fileno(log_fp_);
  do {
    zx_nanosleep(zx_deadline_after(ZX_SEC(LOG_FILE_SYNC_PERIOD_SEC)));
    if (log_size_counter_.load() > 0) {
      log_size_counter_.store(0);
      fflush(log_fp_); // flush data from user space buffer to kernel buffer
      if (fsync(fd) != 0) { // flush data from kernel buffer to storage
        FXL_LOG(WARNING) << "vmlog file fsync error: " << ErrnoString(errno);
      }
    }
  } while (shutdown_ == false);

  fflush(log_fp_);
  fsync(fd);
  FXL_LOG(INFO) << "LogFileSyncLoop thread stopped";
}

zx_status_t VmlogStore::LogFileRotate(std::string base_filename) {
  int max_backups = VMLOG_STORE_MAX_FILE_BACKUPS;

  std::string max_backups_name = base_filename + "." + std::to_string(max_backups);
  if (files::IsFile(max_backups_name)) {
    FXL_LOG(INFO) << "remove vmlog file: " << max_backups_name;
    remove(max_backups_name.c_str());
    sync();
  }

  // Shift existing backups (e.g., log.1 -> log.2)
  for (int i = max_backups - 1; i >= 1; --i) {
      std::string old_name = base_filename + "." + std::to_string(i);
      std::string new_name = base_filename + "." + std::to_string(i + 1);
      if (files::IsFile(old_name)) {
          FXL_LOG(INFO) << "rotate vmlog file: " << old_name << " to " << new_name;
          rename(old_name.c_str(), new_name.c_str());
          sync();
      }
  }

  // Rename current file to .1
  if (files::IsFile(base_filename)) {
    std::string new_name = base_filename + ".1";
    FXL_LOG(INFO) << "rotate vmlog file: " << base_filename << " to " << new_name;
    rename(base_filename.c_str(), new_name.c_str());
  }
  sync();
  return ZX_OK;
}

zx_status_t VmlogStore::Initialize(int32_t vmid, uint32_t flags) {
  zx_status_t status = zx::log::create(&dlog_, 0);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to open debuglog for vmlog: status=" << status;
    return status;
  }

  // TODO: investigate rename log file issue under /data/vmlog sub directory
  // if (!files::IsDirectory(kVmlogStorageDir) &&
  //     !files::CreateDirectory(kVmlogStorageDir)) {
  //   FXL_LOG(ERROR) << "Failed to create directory: " << kVmlogStorageDir;
  //   return ZX_ERR_IO;
  // }

  if (vm_names.find(vmid) == vm_names.end()) {
    FXL_LOG(ERROR) << "VM name not found for vmid: "<< vmid;
    return ZX_ERR_INVALID_ARGS;
  }

  std::string log_file = fxl::StringPrintf("%s/vm_%s.log", kVmlogStorageDir, vm_names[vmid].c_str());
  status = LogFileRotate(log_file);
  if (status != ZX_OK) {
    return status;
  }

  FILE *fp = fopen(log_file.c_str(), "w+");
  if (!fp) {
    FXL_LOG(ERROR) << "unable to open file: " << log_file
                   << ", " << ErrnoString(errno);
    return ZX_ERR_IO;
  }

  // set auto flush buffer to LOG_FILE_SYNC_THRESHOLD_BYTES
  setvbuf(fp, nullptr, _IOFBF, LOG_FILE_SYNC_THRESHOLD_BYTES);

  FXL_LOG(INFO) << "vmlog save to file path: " << log_file;

  log_fp_ = fp;
  log_file_ = log_file;
  vm_name_ = vm_names[vmid];
  vmid_ = vmid;
  flags_ = flags;

  file_sync_thrd_ = std::thread(&VmlogStore::LogFileSyncLoop, this);
  return ZX_OK;
}

void VmlogStore::Shutdown(void) {
  std::lock_guard<std::mutex> guard(store_lock_);
  if (!shutdown_) {
    shutdown_ = true;
    if (file_sync_thrd_.joinable()) {
      file_sync_thrd_.join();
    }
    if (log_fp_) {
      fclose(log_fp_);
      log_fp_ = nullptr;
    }
  }
}

VmlogStore::VmlogStore() : log_with_prefix_{}, flags_(0u), shutdown_(false) {
  log_fp_ = nullptr;
}

VmlogStore::~VmlogStore() {
  Shutdown();
}