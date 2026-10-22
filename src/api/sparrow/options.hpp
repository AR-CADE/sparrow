#ifndef SPARROW_OPTIONS_H
#define SPARROW_OPTIONS_H

#include <string>

struct sparrow_options
{
    std::string assets_path   = "";
    std::string icu_data_path = "";
    std::string elf_file_path = "";
    int argc = 0;
    const char*const *argv = nullptr;
};

#endif
