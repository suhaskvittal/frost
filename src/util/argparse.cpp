/*
 *  author: Suhas Vittal
 *  date:   5 December 2024
 * */

#include "utils/argparse.h"

#include <iomanip>
#include <sstream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

ArgParseResult::ArgParseResult(
        int argc,
        char** argv,
        required_t required,
        optional_t optional)
{
    // For simplicity, decrement `argc`.
    --argc;
    // Create help string
    std::stringstream ss;
    ss << "usage:";
    for (std::string_view s : required)
        ss << " <" << s << ">";
    ss << " [ options ]\n";
       << "------------------------------------------------- Available Options -------------------------------------------------\n";
    for (const auto& [ flag, desc, default_value ] : optional) {
        std::string descs = "\"" + std::string(desc) + "\"";
        std::string defs = default_value.empty() ? "" : "default: " + std::string(default_value);
        ss << "\t-" << std::setw(12) << std::left << flag
            << std::setw(48) << std::left << descs
            << std::setw(16) << std::left << defs << "\n";
    }
    help = ss.str();
    // Get required arguments
    if (argc < required.size())
        print_help_and_die();        

    int ii = 1;
    for (const auto& arg : required) {
        parse_data[arg] = std::string(argv[ii]);
        ++ii;
    }
    // Setup defaults
    for (const auto& [flag, desc, default_value])
        parse_data[flag] = default_value;
    // Parse optional arguments
    while (ii <= argc) {
        // Check that this is an option
        if (argv[ii][0] != '-') {
            std::cerr << "Expected option but got \"" << argv[ii] << "\".\n";
            print_help_and_die();
        }
        std::string opt(argv[ii]+1);
        ++ii;
        // Get value
        if (parse_data[opt].empty()) {
            // This is a flag
            parse_data[opt] = "y";
        } else {
            if (ii > argc) {
                std::cerr << "Expected value for argument \"-" << opt << "\" but reached end.\n";
                print_help_and_die();
            }
            if (argv[ii][0] == '-') {
                std::cerr << "Expected value for argument \"-" << opt 
                    << "\" but got new argument \"" << argv[ii] << "\".\n";
                print_help_and_die();
            }
            parse_data[opt] = argv[ii];
            ++ii;
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

