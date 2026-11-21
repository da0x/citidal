#pragma once

#include "platform/install.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace platform {

struct process_result {
    int exit_code = -1;
    std::string output;
};

process_result run(const std::vector<std::string>& arguments,
                   const std::filesystem::path& working_folder,
                   const environment& variables = {});

bool launch(const std::vector<std::string>& arguments);

}
