#include "platform/process.hpp"

#include <cstdlib>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace platform {

namespace {

std::vector<char*> argument_pointers(const std::vector<std::string>& arguments) {
    std::vector<char*> pointers;
    for (const auto& argument : arguments) pointers.push_back(const_cast<char*>(argument.c_str()));
    pointers.push_back(nullptr);
    return pointers;
}

}

process_result run(const std::vector<std::string>& arguments,
                   const std::filesystem::path& working_folder,
                   const environment& variables) {
    process_result result;
    if (arguments.empty()) return result;
    int pipe_ends[2];
    if (pipe(pipe_ends) != 0) return result;

    auto pointers = argument_pointers(arguments);
    pid_t child = fork();
    if (child < 0) {
        close(pipe_ends[0]);
        close(pipe_ends[1]);
        return result;
    }
    if (child == 0) {
        dup2(pipe_ends[1], STDOUT_FILENO);
        dup2(pipe_ends[1], STDERR_FILENO);
        close(pipe_ends[0]);
        close(pipe_ends[1]);
        if (!working_folder.empty() && chdir(working_folder.c_str()) != 0) _exit(127);
        for (const auto& [name, value] : variables) setenv(name.c_str(), value.c_str(), 1);
        execvp(pointers[0], pointers.data());
        _exit(127);
    }

    close(pipe_ends[1]);
    char buffer[4096];
    for (ssize_t count; (count = read(pipe_ends[0], buffer, sizeof buffer)) > 0;)
        result.output.append(buffer, static_cast<std::size_t>(count));
    close(pipe_ends[0]);

    int status = 0;
    waitpid(child, &status, 0);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

bool launch(const std::vector<std::string>& arguments) {
    if (arguments.empty()) return false;
    auto pointers = argument_pointers(arguments);
    pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        setsid();
        if (fork() != 0) _exit(0);
        int nothing = open("/dev/null", O_RDWR);
        dup2(nothing, STDIN_FILENO);
        dup2(nothing, STDOUT_FILENO);
        dup2(nothing, STDERR_FILENO);
        execvp(pointers[0], pointers.data());
        _exit(127);
    }
    int status = 0;
    waitpid(child, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}
