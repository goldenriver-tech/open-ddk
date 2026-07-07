// SPDX-License-Identifier: BSD-3-Clause

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fdio/watcher.h>

#include "nebula_logger.h"

namespace {

#define LOG_FILE_MAX_BACKUPS 5
#define LOG_FILENAME_MAX_LEN 256
#define LOG_FILE_MAXIMUM_ALLOWED_SIZE (2 * 1024 * 1024)  // 2MB
#define LOG_FILE_SYNC_THRESHOLD_BYTES (4096)             // 4KB sync threshold

static constexpr char default_log_file[] = "/data/nebula.log";

static std::string ErrnoString(int err) {
    return fxl::StringPrintf("%s(%d)", strerror(err), err);
}

static int usage(void) {
    fprintf(stderr,
        "usage: nebula_logger    dump the nebula debug log\n"
        "\n"
        "options: -f        don't exit, keep waiting for new messages\n"
        "         -p <pid>  only show messages from specified pid\n"
        "         -t        only show the text of messages (no metadata)\n"
        "         -s        saved nebula log to file\n"
        "         -h        show help\n"
    );
    return -1;
}

} // namespace

static zx_status_t MatchBlockDeviceToPath(int dirfd, int event, const char *fn,
                                          void *cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
        return ZX_OK;
    }
    if (strcmp(fn, (const char *)cookie) == 0) {
        return ZX_ERR_STOP;
    }
    return ZX_OK;
}

bool NebulaLogger::is_file(const char* path) {
    struct stat buf;
    if (stat(path, &buf) != 0)
        return false;
    return S_ISREG(buf.st_mode);
}

void NebulaLogger::sync_file(FILE* fp) {
    if (!fp) return;
    fflush(fp);
    fsync(fileno(fp));
}

void NebulaLogger::close_file_safely(FILE* fp) {
    if (!fp) return;
    sync_file(fp);
    fclose(fp);
}

FILE* NebulaLogger::open_log_file(const char* path) {
    FILE* fp = fopen(path, "w+");
    if (!fp) {
        FXL_LOG(ERROR) << "Could not open file: " << path << ", " << ErrnoString(errno);
        return nullptr;
    }
    setvbuf(fp, nullptr, _IOFBF, LOG_FILE_SYNC_THRESHOLD_BYTES);
    return fp;
}

void NebulaLogger::rename_file_safe(const char* old_path, const char* new_path) {
    if (is_file(old_path)) {
        FXL_LOG(INFO) << "rotate nebula log file: "
                      << fxl::StringPrintf("%s to %s", old_path, new_path);
        rename(old_path, new_path);
        sync();
    }
}

void NebulaLogger::remove_file_safe(const char* path) {
    if (is_file(path)) {
        FXL_LOG(INFO) << "remove nebula log file: " << path;
        remove(path);
        sync();
    }
}

int NebulaLogger::detect_fs_is_mounted(void) {
    int ret;
    uint32_t product_id;

    const char* qemu_dev = "002";
    const char* little_fs_dev = "080";
    const char* mt8668_dev = "/dev/class/block/";
    const char* temp_dev_node = nullptr;

    _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_PRODUCT_ID, &product_id);
    temp_dev_node = (product_id == BOARD_VID_QEMU) ? qemu_dev : little_fs_dev;

    FXL_LOG(INFO) << "fs_dev_node=" << temp_dev_node;

    int fd = open(mt8668_dev, O_DIRECTORY | O_RDONLY);
    if (fd < 0) {
        return FS_DETECT_FAILED;
    }

    ret = fdio_watch_directory(
        fd, MatchBlockDeviceToPath, ZX_TIME_INFINITE, (void *)temp_dev_node);
    close(fd);
    return (ret == ZX_ERR_STOP) ? FS_DETECT_SUCCESS : FS_DETECT_FAILED;
}

void NebulaLogger::log_file_rotate(const char* base_filename) {
    int max_backups = LOG_FILE_MAX_BACKUPS;
    char old_file[LOG_FILENAME_MAX_LEN] = {};
    char new_file[LOG_FILENAME_MAX_LEN] = {};

    // Remove oldest backup files
    snprintf(old_file, sizeof(old_file)-1, "%s.%d", base_filename, max_backups);
    remove_file_safe(old_file);

    snprintf(old_file, sizeof(old_file)-1, "%s.older.%d", base_filename, max_backups);
    remove_file_safe(old_file);

    // Shift existing backup files
    for (int i = max_backups - 1; i >= 1; --i) {
        snprintf(old_file, sizeof(old_file)-1, "%s.%d", base_filename, i);
        snprintf(new_file, sizeof(new_file)-1, "%s.%d", base_filename, i+1);
        rename_file_safe(old_file, new_file);

        snprintf(old_file, sizeof(old_file)-1, "%s.older.%d", base_filename, i);
        snprintf(new_file, sizeof(new_file)-1, "%s.older.%d", base_filename, i+1);
        rename_file_safe(old_file, new_file);
    }

    // Rotate current log file
    snprintf(old_file, sizeof(old_file)-1, "%s", base_filename);
    snprintf(new_file, sizeof(new_file)-1, "%s.1", base_filename);
    rename_file_safe(old_file, new_file);

    snprintf(old_file, sizeof(old_file)-1, "%s.older", base_filename);
    snprintf(new_file, sizeof(new_file)-1, "%s.older.1", base_filename);
    rename_file_safe(old_file, new_file);

    sync();
}

void NebulaLogger::write_to_file(FILE* fp, zx_log_record_t* rec, bool plain) {
    if (!fp || !rec) return;

    if (!plain) {
        char tmp[32];
        size_t len = snprintf(tmp, sizeof(tmp), "(%lu)[%05d.%03d] ",
                             rec->cpu,
                             (int)(rec->timestamp / ZX_SEC(1)),
                             (int)((rec->timestamp / ZX_MSEC(1)) % 1000));
        fwrite(tmp, 1, std::min(len, sizeof(tmp)), fp);
    }

    fwrite(rec->data, 1, rec->datalen, fp);
    if (rec->datalen == 0 || rec->data[rec->datalen - 1] != '\n') {
        fputc('\n', fp);
    }
}

void NebulaLogger::write_to_stdout(const zx_log_record_t& rec, bool plain) {
    write_to_file(stdout, const_cast<zx_log_record_t*>(&rec), plain);
}

NebulaLogger::QueuedLog::QueuedLog(const zx_log_record_t& rec) {
    timestamp = rec.timestamp;
    pid = rec.pid;
    tid = rec.tid;
    cpu = rec.cpu;
    flags = rec.flags;
    datalen = rec.datalen;
    memcpy(data, rec.data, datalen);
}

zx_status_t NebulaLogger::Initialize(bool plain) {
    worker_ = std::thread(&NebulaLogger::writer_thread, this);
    plain_ = plain;
    return ZX_OK;
}

NebulaLogger::NebulaLogger() {
    FXL_LOG(INFO) << "NebulaLogger Constructor";
}

NebulaLogger::~NebulaLogger() {
    stop();
}

void NebulaLogger::enqueue(const zx_log_record_t& rec) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        queue_.emplace(rec);
    }
    cv_.notify_one();
}

void NebulaLogger::flush_file(void) {
    FILE* fp = log_fp_.load();
    if (fp) sync_file(fp);
}

void NebulaLogger::stop(void) {
    if (exiting_.exchange(true))
        return;

    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void NebulaLogger::check_size_and_rotate() {
    if (log_size_counter_.load() <= LOG_FILE_MAXIMUM_ALLOWED_SIZE)
        return;

    FXL_LOG(INFO) << "current nebula-logger exceeds file size limit, size: "
                  << log_size_counter_.load();

    // Close old file
    FILE* old_fp = log_fp_.exchange(nullptr);
    close_file_safely(old_fp);

    char cur_file[LOG_FILENAME_MAX_LEN] = {};
    char new_file[LOG_FILENAME_MAX_LEN] = {};

    // Remove previous .older file
    snprintf(cur_file, sizeof(cur_file)-1, "%s.older", default_log_file);
    remove_file_safe(cur_file);

    // Rename current log to .older
    snprintf(cur_file, sizeof(cur_file)-1, "%s", default_log_file);
    snprintf(new_file, sizeof(new_file)-1, "%s.older", default_log_file);
    rename_file_safe(cur_file, new_file);

    // Open new clean log file
    FILE* new_fp = open_log_file(default_log_file);
    if (!new_fp) {
        FXL_LOG(ERROR) << "Failed to reopen log file after rotation";
        return;
    }

    log_fp_.store(new_fp);
    log_size_counter_.store(0);
}

zx_status_t NebulaLogger::writer_thread(void) {
    // Detect filesystem status
    int ret = detect_fs_is_mounted();
    FXL_LOG(INFO) << "detect_fs_is_mounted ret: " << ret;
    //zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));

    // Rotate old logs and open new file
    FXL_LOG(INFO) << "nebula-logger: saving debug log to " << default_log_file;
    log_file_rotate(default_log_file);

    FILE* log_fp = open_log_file(default_log_file);
    if (!log_fp) return ZX_ERR_IO;

    log_fp_.store(log_fp);
    log_size_counter_.store(0);

    // Main log processing loop
    while (true) {
        QueuedLog entry;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] {
                return !queue_.empty() || exiting_.load();
            });

            if (exiting_.load() && queue_.empty())
                break;

            entry = std::move(queue_.front());
            queue_.pop();
        }

        // Reconstruct log record
        zx_log_record_t rec{};
        rec.timestamp = entry.timestamp;
        rec.pid = entry.pid;
        rec.tid = entry.tid;
        rec.cpu = entry.cpu;
        rec.flags = entry.flags;
        rec.datalen = entry.datalen;
        memcpy(rec.data, entry.data, entry.datalen);

        // Write to file and check size
        log_size_counter_.fetch_add(rec.datalen);
        write_to_file(log_fp_.load(), &rec, plain_);
        check_size_and_rotate();
    }

    // Cleanup on exit
    close_file_safely(log_fp_.load());
    log_fp_.store(nullptr);
    return ZX_OK;
}

int main(int argc, char** argv) {
    bool tail = false;
    bool filter_pid = false;
    bool plain = false;
    bool save_to_file = false;
    zx_koid_t pid = 0;
    zx_handle_t h;
    std::unique_ptr<NebulaLogger> logger_;

    FXL_LOG(INFO) << "nebula_logger start, " << argv[0];
    logger_ = std::make_unique<NebulaLogger>();
    if (!logger_) {
        FXL_LOG(ERROR) << "Failed to create NebulaLogger";
        return ZX_ERR_NO_MEMORY;
    }

    while (1) {
        static struct option opts[] = {
            {"tail", no_argument, nullptr, 'f'},
            {"plain", no_argument, nullptr, 't'},
            {"filter_pid", required_argument, nullptr, 'p'},
            {"save_to_file", no_argument, nullptr, 's'},
            {"help", no_argument, nullptr, 'h'},
            {nullptr, 0, nullptr, 0},
        };
        int opt_index;
        int c = getopt_long(argc, argv, "ftp:sh", opts, &opt_index);
        if (c < 0) break;

        switch (c) {
            case 'f': tail = true; break;
            case 't': plain = true; break;
            case 'p':
                errno = 0;
                pid = strtoull(optarg, NULL, 0);
                if (errno) {
                    fprintf(stderr, "nebula-logger: invalid pid\n");
                    return -1;
                }
                filter_pid = true;
                break;
            case 's': save_to_file = true; break;
            case 'h':
            default:
                return usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (save_to_file) {
        logger_->Initialize(plain);
    }

    if (zx_log_create(ZX_LOG_FLAG_READABLE, &h) < 0) {
        FXL_LOG(ERROR) << "nebula-logger: cannot open debug log";
        return -1;
    }

    char buf[ZX_LOG_RECORD_MAX] = {};
    zx_log_record_t* rec = (zx_log_record_t*)buf;
    for (;;) {
        zx_status_t status = zx_log_read(h, ZX_LOG_RECORD_MAX, rec, 0);
        if (status < 0) {
            if (status == ZX_ERR_SHOULD_WAIT) {
                logger_->flush_file();
                zx_object_wait_one(h, ZX_LOG_READABLE, ZX_TIME_INFINITE, NULL);
                continue;
            }
            break;
        }

        if (filter_pid && pid != rec->pid)
            continue;

        if (save_to_file) {
            if ((rec->flags & ZX_LOG_FLAG_NO_FILE) != 0)
                continue;
            logger_->enqueue(*rec);
        } else {
            logger_->write_to_stdout(*rec, plain);
        }
    }

    if (save_to_file) {
        logger_->stop();
    }

    return 0;
}
