/*

    Copyright (c) 2023 The MITRE Corporation.

    ALL RIGHTS RESERVED. This copyright notice must

    not be removed from this software, absent MITRE's

    express written permission.

*/

#include <iostream>
#include <cassert>
#include <chrono>
#include <string>
#include <stdexcept>
#include "parser.h"
#include "atomicops.h"
#include "ilf.h"

AE_FORCEINLINE void int_to_string(int const& n, std::string& str) {
    str = std::to_string(n);
}

int main() {
    const int N = 10000000;
    libilf::Parser<int,std::string> *parser;
    try {
        parser = new libilf::Parser<int,std::string>(int_to_string);
    } catch (std::bad_alloc const& e) {
        std::cerr << "ERROR: parser initialization failed" << std::endl;
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    for (int i = 0; i < N; i++) {
        parser->push(i);
    }
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    // parser->start_max_throughput();
    // parser->stop_max_throughput();
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    for (int expected = 0; expected < N; expected++) {
        std::string cur_output;
        assert(parser->pop(cur_output));
        assert(stoi(cur_output) == expected);
    }
    assert(parser->input_size() == 0 && parser->output_size() == 0);
    std::chrono::duration<double> elapsed_time = end - start;
    std::cout << "Processed " << N << " integers in " << elapsed_time.count() << " seconds" << std::endl;
    std::cout << "Throughput: " << (double) N / elapsed_time.count() << " integers per second" << std::endl;
    return 0;
}

