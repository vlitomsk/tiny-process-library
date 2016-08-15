#include "process.hpp"

std::list<Process*> Process::instances = std::list<Process*>();

Process::Process(const string_type &command, const string_type &path,
                 std::function<void (int)> process_finished,
                 std::function<void(const char* bytes, size_t n)> read_stdout,
                 std::function<void(const char* bytes, size_t n)> read_stderr,
                 bool open_stdin, size_t buffer_size):
                 closed(true), process_finished(process_finished), read_stdout(read_stdout), read_stderr(read_stderr), open_stdin(open_stdin), buffer_size(buffer_size) {
  instances.push_back(this);
  open(command, path);
  async_read();
}

Process::~Process() {
  instances.remove(this);
  close_fds();
}

Process::id_type Process::get_id() {
  return data.id;
}

bool Process::write(const std::string &data) {
  return write(data.c_str(), data.size());
}
