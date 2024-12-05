/*
 *  author: Suhas Vittal
 *  date:   5 December 2024
 * */

#ifndef UTILS_ARGPARSE_h
#define UTILS_ARGPARSE_h

#include <unordered_map>
#include <string>
#include <string_view>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class ArgParseResult
{
public:
    using required_t = std::initializer_list<std::string_view>;
    using option_t = std::tuple<std::string_view, std::string_view, std::string_view>;
    using optional_t = std::initializer_list<option_t>;
    using parse_map_t = std::unordered_map<std::string_view, std::string>;

    std::string help;
private:
    parse_map_t parse_data;
public:
    ArgParseResult(int argc, char** argv, required_t, optional_t);

    template <class T>
    void operator()(std::string_view argname, T& argout)
    {
        auto arg_it = parse_data.find(argname);
        if (arg_it == parse_data.end()) {
            std::cerr << "Unknown argument \"" << argname << "\"\n" << help_;
            exit(1);
        }

        std::string value = it->second;
        std::string typenamestr;
        try {
            if constexpr (std::is_integral<T>::value) {
                if constexpr (std::is_same<T, bool>::value) {
                    typenamestr = "bool";
                    argout = value != "";
                } else if constexpr (std::is_unsigned<T>::value) {
                    typenamestr = "uint64_t";
                    argout = static_cast<T>(std::stoull(value));
                } else {
                    typenamestr = "int64_t";
                    argout = static_cast<T>(std::stoll(value));
                }
            } else if constexpr (std::is_floating_point<T>::value) {
                typenamestr = "double";
                argout = static_cast<T>(std::stof(value));
            } else {
                typenamestr = "std::string";
                argout = value;
            }
        } catch (...) {
            std::cerr << "Could not parse data for " << argname << " as type \"" << typenamestr << "\"\n" << help_;
            exit(1);
        }
    }
private:
    inline void print_help_and_die(void)
    {
        std::cerr << help;
        exit(1);
    }
};

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

#endif  // UTILS_ARGPARSE_h
