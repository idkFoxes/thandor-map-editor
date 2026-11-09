#include "thandor/simulation.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
std::uint32_t parse_hex32(const std::string& text) {
    return static_cast<std::uint32_t>(std::stoull(text, nullptr, 16));
}
}

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("usage: v543_binary_oracle_compare <oracle.tsv>");
    std::ifstream input(argv[1]);
    if (!input) throw std::runtime_error("cannot open oracle TSV");
    std::string line;
    std::getline(input, line); // header
    std::size_t compared = 0U;
    std::size_t reference_only = 0U;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        std::istringstream row(line);
        std::string function, p0s, p1s, eaxs, edxs;
        std::getline(row, function, '\t');
        std::getline(row, p0s, '\t');
        std::getline(row, p1s, '\t');
        std::getline(row, eaxs, '\t');
        std::getline(row, edxs, '\t');
        const auto p0 = static_cast<std::int32_t>(std::stoll(p0s));
        const auto p1 = static_cast<std::int32_t>(std::stoll(p1s));
        if (function == "FixedMath_Atan2Angle16") {
            const std::uint32_t binary = parse_hex32(eaxs) & 0xFFFFU;
            const std::uint32_t editor = thandor::radians_to_turn16(
                std::atan2(static_cast<float>(p0), static_cast<float>(p1))) & 0xFFFFU;
            if (binary != editor) {
                std::ostringstream error;
                error << "atan2 parity mismatch y=" << p0 << " x=" << p1
                      << " binary=0x" << std::hex << binary << " editor=0x" << editor;
                throw std::runtime_error(error.str());
            }
            ++compared;
        } else if (function == "FixedMath_SinCosScaled") {
            // Retain these rows as an executable reference fixture for the factory exit/spawner
            // work. The map editor currently has no public exact-Q28 table helper to compare.
            ++reference_only;
        }
    }
    if (compared == 0U) throw std::runtime_error("oracle contained no comparable atan2 rows");
    std::cout << "V543_BINARY_ORACLE_COMPARE PASS: " << compared
              << " exact editor comparisons; " << reference_only
              << " binary reference-only SinCos rows\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V543_BINARY_ORACLE_COMPARE FAIL: " << error.what() << '\n';
    return 1;
}
