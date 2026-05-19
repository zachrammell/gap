#include "os.h"

#include <cassert>

#include <forward_list>
#include <string_view>

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <copyfile.h>
#include <time.h>
#include <unistd.h>
#include <libproc.h>

#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <dispatch/dispatch.h>

#include "arena.h"
#include "config.h"
#include "constants.h"
#include "enum-utils.h"
#include "gap-core.h"
#include "gap-strings.h"
#include "list-helpers.h"
#include "macros.h"
#include "renderer.h"
#include "thread-ctx.h"
#include "utf-8.h"
#include "util.h"
#include "vec.h"

namespace OS
{
  namespace
  {
    using namespace Constants;
    constexpr int internal_max_path = 1024 * 2;
    constexpr uint64_t pipe_buffer_size = KB(16);

    struct MacEntity;

    struct IOPipeEndpoint
    {
      int read_pipe;
      char buf[pipe_buffer_size];
    };

    struct IOPipeData
    {
      int exit_code;
      int child_pid;
      IOPipeEndpoint stdout_ep;
      IOPipeEndpoint stderr_ep;
    };

    using PipeDataAlloc = std::forward_list<IOPipeData>;
    struct AllocPipeDataInput
    {
      int child_pid;
      int stdout_pipe;
      int stderr_pipe;
    };

    struct DirIterData
    {
      DIR *dir;
      dirent *dp;
      std::string core_dir;
      DirIterFlags flags;
    };

    using DirIterDataAlloc = std::forward_list<DirIterData>;

    struct ThreadData
    {
      ThreadEntryPointFunctionType func;
      void *data_p;
      pthread_t handle;
    };

    struct MacEntity
    {
      MacEntity *next;
      MacEntity *prev;
      union
      {
        //- brt: thread
        ThreadData thread;

        //- brt: mutex
        pthread_mutex_t mutex;

        //- brt: condition variable
        pthread_cond_t cv;
      };
      uint64_t gen;
    };

    struct MacEntityList
    {
      MacEntity *first;
      MacEntity *last;
      uint64_t count;
    };

    struct MacBackendData
    {
      RenderCoreData *render_data;
      pthread_mutex_t mac_arena_mutex;
      Arena::Arena *mac_arena;
      MacEntityList thread_lst;
      MacEntityList mutex_lst;
      MacEntityList cond_var_lst;
      MacEntity *entity_free_list;
      SystemInfo sys_info;
      Ticks double_click_time;
      bool fullscreened;
    };

    MacBackendData impl_data;

    MacBackendData *mac_data()
    {
      return &impl_data;
    }
    
    MacEntity *push_entity(MacBackendData *data, MacEntityList *lst)
    {
      MacEntity *node = nullptr;
      pthread_mutex_lock(&data->mac_arena_mutex);
      if (data->entity_free_list != nullptr)
      {
        node = data->entity_free_list;
        SLLStackPop(data->entity_free_list);
        zero_bytes(node);
      }
      else
      {
        node = Arena::push_array<MacEntity>(data->mac_arena, 1);
      }
      DLLPushBack(lst->first, lst->last, node);
      ++lst->count;
      pthread_mutex_unlock(&data->mac_arena_mutex);
      return node;
    }

    void release_entity(MacBackendData *data, MacEntityList *lst, MacEntity *e)
    {
      pthread_mutex_lock(&data->mac_arena_mutex);
      DLLRemove(lst->first, lst->last, e);
      --lst->count;
      SLLStackPush(data->entity_free_list, e);
      pthread_mutex_unlock(&data->mac_arena_mutex);
    }

    FileHandle os_file_handle(int fd)
    {
      return FileHandle(fd);
    }

    int mac_file_handle(FileHandle fd)
    {
      return static_cast<int>(rep(fd));
    }

    LibraryHandle os_library_handle(void *so)
    {
      return LibraryHandle{ reinterpret_cast<size_t>(so) };
    }

    void *mac_library_handle(LibraryHandle handle)
    {
      return reinterpret_cast<void *>(rep(handle));
    }

    IOPipe os_pipe_handle(IOPipeData *p)
    {
      return IOPipe{ reinterpret_cast<PrimitiveType<IOPipe>>(p) };
    }

    DirIter os_dir_iter(DirIterData *d)
    {
      return DirIter{ reinterpret_cast<PrimitiveType<DirIter>>(d) };
    }

    DirIterData *mac_dir_iter(DirIter h)
    {
      return reinterpret_cast<DirIterData *>(h);
    }

    ProcessHandle os_process_handle(pid_t id)
    {
      return static_cast<ProcessHandle>(id);
    }

    pid_t mac_process_handle(ProcessHandle h)
    {
      return static_cast<pid_t>(h);
    }

    uint64_t sysctl_u64(char *name)
    {
      uint64_t result = 0;

      uint8_t buff[8];
      size_t buff_len = sizeof(buff);

      if (sysctlbyname(name, buff, &buff_len, 0, 0) != -1)
      {
        memcpy(&result, buff, buff_len);
      }

      return result;
    }

    bool init_mac(MacBackendData *data)
    {
      data->double_click_time = Ticks{ 500 };
      return true;
    }

    Event *push_event(Arena::Arena *arena, Events *lst, EventSort sort, OSWindow window)
    {
      Event *e = Arena::push_array<Event>(arena, 1);
      e->sort = sort;
      e->window = window;
      SLLQueuePush(lst->first, lst->last, e);
      ++lst->count;
      return e;
    }

    Mutex os_mutex(MacEntity *e)
    {
      return Mutex{ reinterpret_cast<PrimitiveType<Mutex>>(e) };
    }

    MacEntity *mac_mutex(Mutex m)
    {
      return reinterpret_cast<MacEntity *>(m);
    }
    
    MacEntity *mac_mutex_alloc(MacBackendData *data)
    {
      MacEntity *mutex = push_entity(data, &data->mutex_lst);
      pthread_mutexattr_t attr;
      pthread_mutexattr_init(&attr);
      pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
      pthread_mutex_init(&mutex->mutex, &attr);
      pthread_mutexattr_destroy(&attr);
      return mutex;
    }
    
    void release_mac_mutex(MacBackendData *data, Mutex m)
    {
      MacEntity *e = mac_mutex(m);
      pthread_mutex_destroy(&e->mutex);
      release_entity(data, &data->mutex_lst, e);
    }

    Thread os_thread(MacEntity *e)
    {
      return Thread{ reinterpret_cast<PrimitiveType<Thread>>(e) };
    }

    MacEntity *mac_thread(Thread t)
    {
      return reinterpret_cast<MacEntity *>(t);
    }

    void *mac_thread_entry_point(void *ptr)
    {
      MacEntity *e = static_cast<MacEntity *>(ptr);
      thread_entry_bridge(e->thread.func, e->thread.data_p);
      return nullptr;
    }

    MacEntity *mac_alloc_thread(MacBackendData *data, ThreadEntryPointFunctionType func, void *data_p)
    {
      MacEntity *thread = push_entity(data, &data->thread_lst);
      thread->thread.func = func;
      thread->thread.data_p = data_p;
      int result = pthread_create(&thread->thread.handle, nullptr, mac_thread_entry_point, thread);
      if (result != 0)
      {
        release_entity(data, &data->thread_lst, thread);
        thread = nullptr;
      }
      return thread;
    }

    void mac_release_thread(MacBackendData *data, Thread thread)
    {
      release_entity(data, &data->thread_lst, mac_thread(thread));
    }
  } //namespace [anon]

  void *mem_reserve(AllocationSize size)
  {
    void *result = mmap(0, rep(size), PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if(result == MAP_FAILED)
    {
      result = nullptr;
    }
    return result;
  }

  bool mem_commit(void *ptr, AllocationSize size)
  {
    int result = mprotect(ptr, rep(size), PROT_READ|PROT_WRITE);
    return result == 0;
  }

  void mem_decommit(void *ptr, AllocationSize size)
  {
    madvise(ptr, rep(size), MADV_DONTNEED);
    mprotect(ptr, rep(size), PROT_NONE);
  }

  void mem_release(void *ptr, AllocationSize size)
  {
    munmap(ptr, rep(size));
  }

  void* mem_reserve_large(AllocationSize size)
  {
    return mem_reserve(size);
  }

  bool mem_commit_large(void *ptr, AllocationSize size)
  {
    return mem_commit(ptr, size);
  }

  void mem_clear_working_set_pages()
  {
    //- brt: NYI.
  }

  ///////////////////////////////////////////////////////////////////////////////
  //~ brt: Threading

  Thread launch_thread(ThreadEntryPointFunctionType entry, void *data_p)
  {
    MacBackendData *data = mac_data();
    MacEntity *thread = mac_alloc_thread(data, entry, data_p);
    return os_thread(thread);
  }

  bool join_thread(Thread thread)
  {
    MacBackendData *data = mac_data();
    MacEntity *e = mac_thread(thread);
    int join_result = pthread_join(e->thread.handle, nullptr);
    mac_release_thread(data, thread);
    return join_result == 0;
  }

  ThreadID thread_id()
  {
    return ThreadID{ static_cast<uint32_t>(mach_thread_self()) };
  }

  ///////////////////////////////////////////////////////////////////////////////
  //~ brt: Synchronization primitives

  Mutex alloc_mutex()
  {
    MacBackendData *data = mac_data();
    return os_mutex(mac_mutex_alloc(data));
  }

  void release_mutex(Mutex mutex)
  {
    MacBackendData *data = mac_data();
    release_mac_mutex(data, mutex);
  }

  void lock_mutex(Mutex mutex)
  {
    MacEntity *e = mac_mutex(mutex);
    pthread_mutex_lock(&e->mutex);
  }

  void unlock_mutex(Mutex mutex)
  {
    MacEntity *e = mac_mutex(mutex);
    pthread_mutex_unlock(&e->mutex);
  }

  const SystemInfo *system_info()
  {
    MacBackendData *data = mac_data();
    return &data->sys_info;
  }

  MicroSec now_microseconds()
  {
    // brt: NOTE: this clock source cannot be changed or all of pthreads waits will not work...
    uint64_t ns = clock_gettime_nsec_np(CLOCK_REALTIME);
    uint64_t us = ns / 1000;
    return MicroSec{ us };
  }


  void fill_date_time_from_tm(DateTime* out, const tm& in, MicroSec us)
  {
    out->sec  = in.tm_sec;
    out->min  = in.tm_min;
    out->hour = in.tm_hour;
    out->day  = in.tm_mday-1;
    out->mon  = in.tm_mon;
    out->year = in.tm_year+1900;
    out->msec = rep(us);
  }

  void fill_tm_from_date_time(tm* out, const DateTime& in)
  {
    out->tm_sec = in.sec;
    out->tm_min = in.min;
    out->tm_hour= in.hour;
    out->tm_mday= in.day+1;
    out->tm_mon = in.mon;
    out->tm_year= in.year-1900;
  }

  DenseTime dense_time_from_timespec(const timespec& in)
  {
    struct tm tm_time{ };
    gmtime_r(&in.tv_sec, &tm_time);
    DateTime date_time{ };
    fill_date_time_from_tm(&date_time, tm_time, MicroSec(in.tv_nsec / Million(1)));
    return dense_time_from_date_time(date_time);
  }

  bool path_separator(char c)
  {
    return c == '/';
  }

  size_t path_root_end(String8 path)
  {
    // Assume relative, beginning of path.
    if (path.size == 0)
      return 0;
    // Relative path.
    if (not path_separator(path.str[0]))
      return 0;
    // Unix-style path start, e.g. /root/foo.
    return 1;
  }

  String8 parent_path(String8 path)
  {
    // Find the root path.
    auto root_pos = path_root_end(path);
    String8 relative_path = str8_substr(path, { .off = root_pos });
    // Case 1: Relative path ends in a directory separator, e.g. /root/dir/
    //         so we remove the separator and return /root/.
    // Case 2: We have a relative path which doesn't end in a directory separator
    //         e.g. /root/dir so we'll remove 'dir' and the trailing slash.
    const char* last = relative_path.str + relative_path.size;
    const char* first = relative_path.str;
    // Handle case 2 by removing the trailing filename.
    while (first != last and not path_separator(*(last - 1)))
    {
      --last;
    }
    // Case 1.
    while (first != last and path_separator(*(last - 1)))
    {
      --last;
    }
    return str8_substr(path, { .off = 0, .len = static_cast<uint64_t>(last - path.str) });
  }

  String8 combine_paths(Arena::Arena* arena, String8 a, String8 b)
  {
    // First separate the two paths by the '/' then combine them together.
    auto scratch = Arena::scratch_begin({ &arena, 1 });
    String8List split{};
    String8 path_seps = str8_mut(str8_literal("/"));
    SplitStringsInput split_in{
      .in = a,
      .seps = path_seps,
      .flags = SplitStringsFlags::NoResultClear // We want to append results.
    };
    split_strings(scratch.arena, &split, split_in);
    split_in.in = b;
    split_strings(scratch.arena, &split, split_in);
    // Combine them all together using our preferred system separator.
    // The start separator should be based on whether or not 'a' is a root path.
    String8 start_sep = str8_empty;
    if (a.size != 0 and path_separator(a.str[0]))
    {
      start_sep = str8_mut(str8_literal("/"));
    }
    JoinStringsInput join_in{
      .strings = split,
        .start_sep = start_sep,
        .sep = str8_mut(str8_literal(PATH_SEP)),
        .end_sep = str8_mut(str8_literal("")) // Don't append a separator...
    };
    String8 result = join_strings(arena, join_in);
    Arena::scratch_end(scratch);
    return result;
  }

  String8 root_of_path(String8 path)
  {
    auto end_pos = path_root_end(path);
    // Include the extra slash as well, if we're provided one.
    if (end_pos < path.size and path_separator(path.str[end_pos]))
    {
      ++end_pos;
    }
    return str8_substr(path, { .len = end_pos });
  }

  Error exe_path(Arena::Arena* arena, String8* buf)
  {
    auto scratch = Arena::scratch_begin({ &arena, 1 });

    int got_final_result = 0;
    char *buffer = 0;
    uint32_t size = 0;
    for(size_t cap = internal_max_path, r = 0; r < 4; cap *= 2, r += 1)
    {
      Arena::scratch_end(scratch);
      buffer = Arena::push_array<char>(scratch.arena, cap);
      if (_NSGetExecutablePath(buffer, &size) == 0)
      {
        got_final_result = 1;
        break;
      }
    }

    if(got_final_result && size > 0)
    {
      String8 full_name = str8(buffer, size);
      auto last_slash = str8_find_last_of(full_name, str8_mut(str8_literal("/")));
      if (last_slash != str8_index_sentinel)
      {
        String8 chopped = str8_substr(full_name, { .off = 0, .len = last_slash });
        *buf = str8_copy(arena, chopped);
      }
    }

    Arena::scratch_end(scratch);
    return Error::None;
  }

  FileHandle open_file(String8 file, FileAccess access)
  {
    int mac_flags = 0;
    if(implies(access, FileAccess::Read | FileAccess::Write))
    {
      mac_flags = O_RDWR;
      mac_flags |= O_CREAT;
    }
    else if(implies(access, FileAccess::Write))
    {
      mac_flags = O_WRONLY;
      mac_flags |= O_CREAT;
      mac_flags |= O_TRUNC;
    }
    else if(implies(access, FileAccess::Read))
    {
      mac_flags = O_RDONLY;
    }

    if(implies(access, FileAccess::Append))
    {
      mac_flags |= O_APPEND;
      mac_flags |= O_CREAT;
    }

    auto scratch = Arena::scratch_begin(Arena::no_conflicts);
    // Need null-termination.
    String8 file_copy = str8_copy(scratch.arena, file);
    int fd = open(file_copy.str, mac_flags, 0755);
    Arena::scratch_end(scratch);
    FileHandle handle = FileHandle::Sentinel;
    if(fd != -1)
    {
      handle = os_file_handle(fd);
    }
    return handle;
  }

  void close_file(FileHandle handle)
  {
    assert(handle != FileHandle::Sentinel);
    close(mac_file_handle(handle));
  }

  FileLength file_length(FileHandle handle)
  {
    struct stat file_stat{};
    int res = fstat(mac_file_handle(handle), &file_stat);
    FileLength result = FileLength::Sentinel;
    if (res != -1)
    {
      result = FileLength(file_stat.st_size);
    }
    return result;
  }

  FileLength read_file(Arena::Arena *arena, String8 *buf, FileHandle handle, FileLength count)
  {
    int fd = mac_file_handle(handle);
    uint64_t total_num_bytes_to_read = rep(count);
    uint64_t total_num_bytes_read = 0;
    uint64_t total_num_bytes_left_to_read = total_num_bytes_to_read;
    *buf = str8_cstr_alloc(arena, rep(count));
    for(;total_num_bytes_left_to_read > 0;)
    {
      int read_result = pread(fd, buf->str + total_num_bytes_read, total_num_bytes_left_to_read, total_num_bytes_read);
      if(read_result >= 0)
      {
        total_num_bytes_read += read_result;
        total_num_bytes_left_to_read -= read_result;
      }
      else if(errno != EINTR)
      {
        break;
      }
    }
    return FileLength{ total_num_bytes_read };
  }

  BytesWritten write_file(FileHandle handle, FileOffset off, String8 buf)
  {
    int fd = mac_file_handle(handle);
    uint64_t total_num_bytes_to_write = buf.size;
    uint64_t total_num_bytes_written = 0;
    uint64_t total_num_bytes_left_to_write = total_num_bytes_to_write;
    for(;total_num_bytes_left_to_write > 0;)
    {
      int write_result = pwrite(fd, buf.str + total_num_bytes_written, total_num_bytes_left_to_write, rep(off) + total_num_bytes_written);
      if(write_result >= 0)
      {
        total_num_bytes_written += write_result;
        total_num_bytes_left_to_write -= write_result;
      }
      else if(errno != EINTR)
      {
        break;
      }
    }
    return BytesWritten{ total_num_bytes_written };
  }

  FileProperties file_properties(String8 path)
  {
    struct stat file_stat{};
    auto scratch = Arena::scratch_begin(Arena::no_conflicts);
    // Null-terminate.
    String8 path_copy = str8_copy(scratch.arena, path);
    int result = stat(path_copy.str, &file_stat);
    Arena::scratch_end(scratch);
    FileProperties props{};
    if (result != -1)
    {
      props.size = FileLength(file_stat.st_size);
      props.created = dense_time_from_timespec(file_stat.st_ctimespec);
      props.modified = dense_time_from_timespec(file_stat.st_mtimespec);
      if (file_stat.st_mode & S_IFDIR)
      {
        props.props |= FileProperty::Directory;
      }
    }
    return props;
  }

  Error create_directory(String8 dir)
  {
    auto scratch = Arena::scratch_begin(Arena::no_conflicts);
    dir = str8_copy(scratch.arena, dir);
    int make_dir_r = mkdir(dir.str, 0755);
    Arena::scratch_end(scratch);
    if (make_dir_r == -1)
    {
      if (errno != EEXIST)
        return Error::CreateDirectoryFailed;
    }
    return Error::None;
  }

  bool directory_exists(String8 dir)
  {
    return implies(file_properties(dir).props, FileProperty::Directory);
  }

  bool file_or_path_exists(String8 path)
  {
    auto scratch = Arena::scratch_begin(Arena::no_conflicts);
    // Null-terminate.
    path = str8_copy(scratch.arena, path);
    auto result = access(path.str, F_OK);
    Arena::scratch_end(scratch);
    return result == 0;
  }

  bool regular_file_exists(String8 file)
  {
    struct stat file_stat{ };
    auto scratch = Arena::scratch_begin(Arena::no_conflicts);
    // Null-terminate.
    file = str8_copy(scratch.arena, file);
    int result = stat(file.str, &file_stat);
    Arena::scratch_end(scratch);
    if (result == -1)
      return false;
    return (file_stat.st_mode & S_IFREG);
  }

  Error canonical_file_path(Arena::Arena* arena, String8* buf, String8 path)
  {
    char char_buf[internal_max_path];
    if (realpath(path.str, char_buf) == nullptr)
      return Error::CanonicalizeFailedToGetPathname;
    *buf = str8_copy(arena, str8_cstr(char_buf));
    return Error::None;
  }

  bool working_directory(Arena::Arena* arena, String8* buf)
  {
    char cwd_buf[internal_max_path];
    if (getcwd(cwd_buf, std::size(cwd_buf)) == nullptr)
      return false;
    *buf = str8_copy(arena, str8_cstr(cwd_buf));
    return true;
  }

  bool set_working_directory(String8 path)
  {
    auto scratch = Arena::scratch_begin(Arena::no_conflicts);
    // Null-terminate.
    path = str8_copy(scratch.arena, path);
    int result = chdir(path.str);
    Arena::scratch_end(scratch);
    return result == 0;
  }

  ENABLE_UNHANDLED_CASE_WARNING()
    String8 error_text(Error e)
    {
      switch (e)
      {
        case Error::ClipboardTextUnavailable:
          return str8_mut(str8_literal("Clipboard text unavailable or type is not text"));
      case Error::UnableToOpenClipboard:
        return str8_mut(str8_literal("Unable to open clipboard"));
      case Error::InvalidClipboardData:
        return str8_mut(str8_literal("Invalid clipboard data type"));
      case Error::EmptyClipboardFailed:
        return str8_mut(str8_literal("Unable to empty clipboard"));
      case Error::BufferTruncated:
        return str8_mut(str8_literal("Buffer truncated"));
      case Error::CouldNotLocatePrefPath:
        return str8_mut(str8_literal("Could not locate pref path"));
      case Error::CreateDirectoryFailed:
        return str8_mut(str8_literal("Failed to create directory"));
      case Error::FailedToApplyWindowCaptionColor:
        return str8_mut(str8_literal("Failed to apply window caption color"));
      case Error::FailedToApplyWindowFontColor:
        return str8_mut(str8_literal("Failed to apply window font color"));
      case Error::CanonicalizeFailedToOpenFile:
        return str8_mut(str8_literal("Failed to open file for canonical path"));
      case Error::CanonicalizeFailedToGetPathname:
        return str8_mut(str8_literal("Failed to get invoke realpath"));
      case Error::None:
      case Error::Count:
        break;
    }
    assert(not "Unhandled case");
    return str8_empty;
  }
  DISABLE_UNHANDLED_CASE_WARNING()

  OSError last_os_error_code()
  {
    return OSError{ (uint32_t)errno };
  }

} //namespace OS

int main(int argc, char **argv)
{
  //- brt: Setup the system info.
  OS::MacBackendData *data = OS::mac_data();
  {
    data->sys_info.processor_count = OS::ProcCount(OS::sysctl_u64("hw.physicalcpu"));
    data->sys_info.page_size = OS::PageSize(OS::sysctl_u64("hw.pagesize"));
    data->sys_info.large_page_size = OS::PageSize{ MB(2) };
    data->sys_info.allocation_granularity = OS::AllocGranularity{ rep(data->sys_info.page_size) };
  }


  //- brt: Setup thread context.
  {
    Thread::TLD *tl_ctx = Thread::tld_alloc();
    Thread::tld_select(tl_ctx);
  }

  //- brt: Setup dynamic alloc state.
  {
    data->mac_arena = Arena::alloc(Arena::default_params);
  }

  //- brt: Setup computer name.
  {
    // man(2) gethostname:
    // > POSIX.1 guarantees that "Host names (not including the terminating null byte) are limited to HOST_NAME_MAX bytes".
    // Which implies we need to add an extra byte.
    constexpr int buf_size = 1024 + 1;
    char buf[buf_size];
    int result = gethostname(buf, buf_size);
    if (result == 0)
    {
      String8 str = str8_cstr(buf);
      // Copy into our arena.
      data->sys_info.machine_name = str8_copy(data->mac_arena, str);
    }
    // Note: if somehow the hostname violates the buffer size above, no host name for you, for now...
  }

  //- brt: Setup entity mutex.
  {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&data->mac_arena_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
  }

#ifdef BUILD_TRACK_ARENA
  Arena::init_tracker_arena();
#endif

  //- brt: Initialize other macOS data.
  init_mac(data);

  return gap_main_entry(argc, argv);
}
