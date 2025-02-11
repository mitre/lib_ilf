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
#include <random>
#include <ctime>
#include <list>
#include <climits>
#include <arpa/inet.h>
#include "parser.h"
#include "atomicops.h"
#include "ilf.h"

// TODO: why do we output ILFs as strings to be processed? Strings 
// are slow to process.

std::string *event_t_mapping;

struct Data {
    Data() { }

    Data(unsigned int type, unsigned int src, unsigned int dst, 
        std::time_t time,
        double val1,
        bool val2,
        std::string const& val3) : 
        _type(type), 
        _src(src), 
        _dst(dst), 
        _time(time),
        _val1(val1),
        _val2(val2),
        _val3(val3) { }

    unsigned int _type, _src, _dst;
    std::time_t _time;
    double _val1;
    bool _val2;
    std::string _val3;
};

AE_FORCEINLINE void data_to_ilf(Data const& data, libilf::ILF& ilf) {
    // TODO: std::move?
    char ip_buf[32];
    ilf._event_t = event_t_mapping[data._type];
    assert(inet_ntop(AF_INET, &data._src, ip_buf, 32));
    ilf._sender = std::string(ip_buf);
    assert(inet_ntop(AF_INET, &data._dst, ip_buf, 32));
    ilf._receiver = std::string(ip_buf);
    ilf._time = std::to_string(data._time);
    ilf._pairs = std::vector<libilf::KeyValue>();
    ilf._pairs.push_back(libilf::KeyValue("val1", std::to_string(data._val1), true));
    ilf._pairs.push_back(libilf::KeyValue("val2", std::to_string(data._val2), true));
    ilf._pairs.push_back(libilf::KeyValue("val3", data._val3, true));
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "usage: <num_inputs> <num_threads>" << std::endl;
        return -1;
    }
    const int NUM_INPUTS = std::stoi(argv[1]), NUM_THREADS = std::stoi(argv[2]), NUM_EVENT_TYPES = 4;
    libilf::Parser<Data,libilf::ILF> *parser;
    try {
        parser = new libilf::Parser<Data,libilf::ILF>(data_to_ilf, NUM_THREADS, 4096);
        // parser = new libilf::Parser<Data,libilf::ILF>(data_to_ilf);
    } catch (std::bad_alloc const& e) {
        std::cerr << "ERROR: parser initialization failed" << std::endl;
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    event_t_mapping = new std::string[NUM_EVENT_TYPES];
    event_t_mapping[0] = std::string("ProcessCreate");
    event_t_mapping[1] = std::string("FileCreate");
    event_t_mapping[2] = std::string("FlowStart");
    event_t_mapping[3] = std::string("LogOn");
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned int> global_dist(0, UINT_MAX);
    std::uniform_int_distribution<int> event_t_dist(0, NUM_EVENT_TYPES - 1);
    std::uniform_real_distribution<double> real_dist(0, 1024);
    std::vector<Data> data_vec;
    for (int i = 0; i < NUM_INPUTS; i++) {
        Data cur_data = Data(
            event_t_dist(gen),
            global_dist(gen),
            global_dist(gen),
            std::time(0),
            real_dist(gen),
            (i % 2 == 0),
            std::to_string(global_dist(gen))
        );
        data_vec.push_back(cur_data);
        parser->push(cur_data);
    }
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    // parser->start_max_throughput();
    // parser->stop_max_throughput();
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    for (int i = 0; i < NUM_INPUTS; i++) {
        libilf::ILF cur_output, expected_output;
        assert(parser->pop(cur_output));
        data_to_ilf(data_vec[i], expected_output);
        assert(expected_output == cur_output);
        // std::cout << cur_output << std::endl;
    }
    assert(parser->input_size() == 0 && parser->output_size() == 0);
    std::chrono::duration<double> elapsed_time = end - start;
    std::cout << "Processed " << NUM_INPUTS << " integers in " << elapsed_time.count() << " seconds using " << NUM_THREADS << " threads" << std::endl;
    std::cout << "Throughput: " << (double) NUM_INPUTS / elapsed_time.count() << " integers per second" << std::endl;
    return 0;
}

