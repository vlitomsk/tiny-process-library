#include "process.hpp"
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <stdexcept>

Process::Data::Data(): id(-1) {}

Process::id_type Process::open(const std::string &command, const std::string &path) {
  if(open_stdin)
    stdin_fd=std::unique_ptr<fd_type>(new fd_type);
  if(read_stdout)
    stdout_fd=std::unique_ptr<fd_type>(new fd_type);
  if(read_stderr)
    stderr_fd=std::unique_ptr<fd_type>(new fd_type);
  
  int stdin_p[2], stdout_p[2], stderr_p[2];

  if(stdin_fd && pipe(stdin_p)!=0) {
    close(stdin_p[0]);close(stdin_p[1]);
    return -1;
  }
  if(stdout_fd && pipe(stdout_p)!=0) {
    if(stdin_fd) {close(stdin_p[0]);close(stdin_p[1]);}
    close(stdout_p[0]);close(stdout_p[1]);
    return -1;
  }
  if(stderr_fd && pipe(stderr_p)!=0) {
    if(stdin_fd) {close(stdin_p[0]);close(stdin_p[1]);}
    if(stdout_fd) {close(stdout_p[0]);close(stdout_p[1]);}
    close(stderr_p[0]);close(stderr_p[1]);
    return -1;
  }
  
  id_type pid = fork();

  if (pid < 0) {
    if(stdin_fd) {close(stdin_p[0]);close(stdin_p[1]);}
    if(stdout_fd) {close(stdout_p[0]);close(stdout_p[1]);}
    if(stderr_fd) {close(stderr_p[0]);close(stderr_p[1]);}
    return pid;
  }
  else if (pid == 0) {  
    if(stdin_fd) dup2(stdin_p[0], 0);
    if(stdout_fd) dup2(stdout_p[1], 1);
    if(stderr_fd) dup2(stderr_p[1], 2);
    if(stdin_fd) {close(stdin_p[0]);close(stdin_p[1]);}
    if(stdout_fd) {close(stdout_p[0]);close(stdout_p[1]);}
    if(stderr_fd) {close(stderr_p[0]);close(stderr_p[1]);}

    //Based on http://stackoverflow.com/a/899533/3808293
    int fd_max=sysconf(_SC_OPEN_MAX);
    for(int fd=3;fd<fd_max;fd++)
      close(fd);

    prctl(PR_SET_PDEATHSIG, SIGTERM);
    //setpgid(0, 0);
    //TODO: See here on how to emulate tty for colors: http://stackoverflow.com/questions/1401002/trick-an-application-into-thinking-its-stdin-is-interactive-not-a-pipe
    //TODO: One solution is: echo "command;exit"|script -q /dev/null
    
    if(!path.empty()) {
      auto path_escaped=path;
      size_t pos=0;
      //Based on https://www.reddit.com/r/cpp/comments/3vpjqg/a_new_platform_independent_process_library_for_c11/cxsxyb7
      while((pos=path_escaped.find('\'', pos))!=std::string::npos) {
        path_escaped.replace(pos, 1, "'\\''");
        pos+=4;
      }
      execl("/bin/sh", "sh", "-c", ("cd '"+path_escaped+"' && "+command).c_str(), NULL);
    }
    else
      execl("/bin/sh", "sh", "-c", command.c_str(), NULL);
    
    _exit(EXIT_FAILURE);
  }

  signal(SIGCHLD, &Process::callHandlers);

  if(stdin_fd) close(stdin_p[0]);
  if(stdout_fd) close(stdout_p[1]);
  if(stderr_fd) close(stderr_p[1]);
  
  if(stdin_fd) *stdin_fd = stdin_p[1];
  if(stdout_fd) *stdout_fd = stdout_p[0];
  if(stderr_fd) *stderr_fd = stderr_p[0];

  closed=false;
  data.id=pid;
  return pid;
}

void Process::async_read() {
  if(data.id<=0)
    return;
  if(stdout_fd) {
    stdout_thread=std::thread([this](){
      auto buffer = std::unique_ptr<char[]>( new char[buffer_size] );
      ssize_t n;
      while ((n=read(*stdout_fd, buffer.get(), buffer_size)) > 0)
        read_stdout(buffer.get(), static_cast<size_t>(n));
    });
  }
  if(stderr_fd) {
    stderr_thread=std::thread([this](){
      auto buffer = std::unique_ptr<char[]>( new char[buffer_size] );
      ssize_t n;
      while ((n=read(*stderr_fd, buffer.get(), buffer_size)) > 0)
        read_stderr(buffer.get(), static_cast<size_t>(n));
    });
  }
}

int Process::get_exit_status() {
  if(data.id<=0)
    return -1;
  int exit_status;
  waitpid(data.id, &exit_status, 0);
  {
    std::lock_guard<std::mutex> lock(close_mutex);
    closed=true;
  }
  close_fds();

  return exit_status;
}

void Process::close_fds() {
  if(stdout_thread.joinable())
    stdout_thread.join();
  if(stderr_thread.joinable())
    stderr_thread.join();
  
  if(stdin_fd)
    close_stdin();
  if(stdout_fd) {
    if(data.id>0)
      close(*stdout_fd);
    stdout_fd.reset();
  }
  if(stderr_fd) {
    if(data.id>0)
      close(*stderr_fd);
    stderr_fd.reset();
  }
}

bool Process::write(const char *bytes, size_t n) {
  if(!open_stdin)
    throw std::invalid_argument("Can't write to an unopened stdin pipe. Please set open_stdin=true when constructing the process.");

  std::lock_guard<std::mutex> lock(stdin_mutex);
  if(stdin_fd) {
    if(::write(*stdin_fd, bytes, n)>=0) {
      return true;
    }
    else {
      return false;
    }
  }
  return false;
}

void Process::close_stdin() {
  std::lock_guard<std::mutex> lock(stdin_mutex);
  if(stdin_fd) {
    if(data.id>0)
      close(*stdin_fd);
    stdin_fd.reset();
  }
}

void Process::kill(bool force) {
  std::lock_guard<std::mutex> lock(close_mutex);
  if(data.id>0 && !closed) {
    if(force)
      ::kill(-data.id, SIGTERM);
    else
      ::kill(-data.id, SIGINT);
  }
}

void Process::kill(id_type id, bool force) {
  if(id<=0)
    return;
  if(force)
    ::kill(-id, SIGTERM);
  else
    ::kill(-id, SIGINT);
}

void Process::callHandlers (int signum) {
    std::for_each(instances.begin(), instances.end(),
                  std::bind2nd(std::mem_fun(&Process::sigHandler), signum));
}

void Process::sigHandler(int signum) {
    if (signum == SIGCHLD) {
        int exit_status;
        waitpid(data.id, &exit_status, 0);
        process_finished(exit_status);
    }
}
