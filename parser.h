/*

    Copyright (c) 2023 The MITRE Corporation.

    ALL RIGHTS RESERVED. This copyright notice must

    not be removed from this software, absent MITRE's

    express written permission.

*/

#pragma once

#define LIKELY(x)      __builtin_expect(!!(x), 1)
#define UNLIKELY(x)    __builtin_expect(!!(x), 0)

#include <iostream>
#include <fstream>
#include <thread>
#include <stdexcept>
#include <ctime>

// Taken from https://github.com/cameron314/readerwriterqueue
//
#include "readerwriterqueue.h"
#include "atomicops.h"

namespace libilf {

/**
 * Generic parser class for converting data of type input_t to data
 * of type output_t as defined by a given conversion function.
 *
 * Guarantees that all popped elements from calling Parser::pop() will 
 * come out in the same order in which they were pushed from calling 
 * Parser::push().
 *
 * A Parser functions as follows:
 *
 * (1) For N threads, maintain a per-thread input queue and a 
 *     per-thread output queue for every thread. Although 
 *     per-thread, each queue must be able to support 
 *     concurrent operations in the scenario of 1 
 *     producer and 1 consumer.
 * (2) When pushing elements onto the parser, maintain an input counter 
 *     for the current per-thread input queue. After pushing an 
 *     element onto the current per-thread input queue, increment 
 *     the input counter by 1. If the input counter is greater than 
 *     than the number of input queues, then wrap around to the first
 *     input queue.
 * (3) When popping elements off the parser, maintain an output counter
 *     for the current per-thread output queue. After popping an element 
 *     off the current per-thread output queue, increment the output counter 
 *     by 1. If the output counter is greater than the number of output queues, 
 *     then wrap around to the first output queue.
 *     performed in (2) but for popping elements
 * (4) When converting elements from input_t to output_t, have each thread 
 *     attempt to pop an element of its per-thread input queue. If a value 
 *     was successfully popped, then call the given conversion function to 
 *     convert it into an output_t. Then, push the newly generated output_t 
 *     onto the thread's per-thread output queue.
 */
template <class input_t, class output_t>
class Parser {
public:
    /**
     * Constructor for the Parser class.
     *
     * As parameters, it takes a conversion function, the number of threads to
     * spawn, and the initial size for each thread's input and output queues.
     *
     * Throws a std::invalid_argument exception if the number of threads is 0 
     * or not a power of 2.
     *
     * Throws a std::bad_alloc exception if memory allocation fails (e.g., 
     * the initial size is too large).
     */
    Parser(void (*conversion_function)(input_t const&, output_t&), 
        const unsigned int num_threads, 
        const unsigned int init_size) :
        _num_threads(num_threads), 
        _cur_input_index(0), 
        _cur_output_index(0),
        _conversion_function(conversion_function),
        _threads_active(false) 
    {
        // Verify that num_threads is not 0 and is a power of two.
        // We require that num_threads is a power of two to allow
        // bitwise modular division.
        //
        if (num_threads == 0 || (num_threads & (num_threads - 1)) != 0) {
            throw std::invalid_argument(
                "number of threads must be greater than 0 and a power of 2"
            );
        }

        _threads = new std::thread[num_threads];
        _input_queues = new moodycamel::ReaderWriterQueue<input_t>[_num_threads];
        _output_queues = new moodycamel::ReaderWriterQueue<output_t>[_num_threads];
        for (unsigned int i = 0; i < num_threads; i++) {
            _input_queues[i] = moodycamel::ReaderWriterQueue<input_t>(init_size);
            _output_queues[i] = moodycamel::ReaderWriterQueue<output_t>(init_size);
        }
    }

    /**
     * Constructor for the Parser class.
     *
     * Calls the constructor above with the number of concurrent threads this 
     * system supports as well as an initial size of 2^12 for each thread's 
     * input and output queues.
     *
     * Throws a std::invalid_argument exception if std::thread::hardware_concurrency() 
     * fails.
     */
    Parser(void (*conversion_function)(input_t const&, output_t&)) : 
        Parser(conversion_function, std::thread::hardware_concurrency(), 4096) { } 

    /**
     * Attempts to push an element onto the parser.
     *
     * Returns false if memory allocation fails.
     */
    AE_FORCEINLINE bool push(input_t const& input) {
        moodycamel::ReaderWriterQueue<input_t>& cur_input_queue = _input_queues[_cur_input_index._val];
        bool success = cur_input_queue.enqueue(input);
        if (LIKELY(success)) {
            // Equivalent to _cur_input_index._val = (_cur_input_index._val + 1) % _num_threads;
            //
            _cur_input_index._val = (_cur_input_index._val + 1) & (_num_threads - 1);
        }
        return success;
    }

    /**
     * Attempts to pop an element off the parser.
     *
     * Returns false if queue is empty.
     */
    AE_FORCEINLINE bool pop(output_t& output) {
        moodycamel::ReaderWriterQueue<output_t>& cur_output_queue = _output_queues[_cur_output_index._val];
        bool success = cur_output_queue.try_dequeue(output);
        if (success) {
            _cur_output_index._val = (_cur_output_index._val + 1) & (_num_threads - 1);
        }
        return success;
    }

    /**
     * Returns the number of input elements that are yet to be processed.
     *
     * NOTE: size_approx is O(n). If input_size is called frequently, then it 
     * would be better to maintain a counter for the size of the input queue.
     */
    size_t input_size() {
        size_t global_total = 0;
        for (int i = 0; i < _num_threads; i++) {
            moodycamel::ReaderWriterQueue<input_t>& cur_input_queue = _input_queues[i];
            global_total += cur_input_queue.size_approx();
        }
        return global_total;
    }

    /**
     * Returns the number of output elements that have been processed
     *
     * NOTE: size_approx is O(n). If output_size is called frequently, then it 
     * would be better to maintain an atomic counter for the size of the output 
     * queue.
     */
    size_t output_size() {
        size_t global_total = 0;
        for (unsigned int i = 0; i < _num_threads; i++) {
            moodycamel::ReaderWriterQueue<output_t>& cur_output_queue = _output_queues[i];
            global_total += cur_output_queue.size_approx();
        }
        return global_total;
    }

    /**
     * Starts the parser, which allows it to begin converting elements
     * pushed on by Parser::push().
     */
    void start() {
        _threads_active = true;
        for (unsigned int i = 0; i < _num_threads; i++) {
            _threads[i] = std::thread(&Parser::thread_routine, this, i);
        }
    }

    /**
     * Starts the parser for measuring throughput. The difference between this
     * method and Parser::start() is that threads will die when their input queues 
     * are empty.
     */
    void start_wait() {
        for (unsigned int i = 0; i < _num_threads; i++) {
            _threads[i] = std::thread(&Parser::thread_routine_wait, this, i);
        }
    }

    /**
     * Starts the parser for measuring throughput. The difference between this
     * method and Parser::start() is that threads will sleep for a given interval 
     * when their input queues are empty.
     *
     * NOTE: It would be better to use std::this_thread::sleep_for than nanosleep(2) 
     * since the former is more portable.
     */
    void start_sleep(const struct timespec *req) {
        _threads_active = true;
        for (unsigned int i = 0; i < _num_threads; i++) {
            _threads[i] = std::thread(&Parser::thread_routine_sleep, this, i, req);
        }
    }

    /**
     * Stops the parser, which sets the _threads_active flag to false 
     * and kills all running threads.
     */
    void stop() {
        _threads_active = false;
        for (unsigned int i = 0; i < _num_threads; i++) {
            _threads[i].join();
        }
    }

    /**
     * Stops the parser for measuring throughput. This method behaves equivalently 
     * to Parser::stop() and is provided merely for consistency.
     */
    void stop_wait() {
        stop();
    }

    /**
     * Stops the parser for measuring throughput. This method behaves equivalently 
     * to Parser::stop() and is provided merely for consistency.
     */
    void stop_sleep() {
        stop();
    }

    ~Parser() {
        delete[] _threads;
        delete[] _input_queues;
        delete[] _output_queues;
    }

private:
    /**
     * The routine for every thread spawned by the parser in Parser::start(). Every 
     * iteration, each thread attempts to dequeue an element from its per-thread input 
     * queue. If the queue contains an element, then it is converted and pushed onto 
     * the corresponding thread's output queue. Threads remain alive until Parser::stop() 
     * is called.
     */
    void thread_routine(int index) {
        moodycamel::ReaderWriterQueue<input_t>& my_input_queue = _input_queues[index];
        moodycamel::ReaderWriterQueue<output_t>& my_output_queue = _output_queues[index];
        input_t cur_input;
        output_t cur_output;
        bool success;

        while (LIKELY(_threads_active)) {
            success = my_input_queue.try_dequeue(cur_input);
            if (!success) {
                continue;
            }
            _conversion_function(cur_input, cur_output);
            success = my_output_queue.enqueue(cur_output);
            if (UNLIKELY(!success)) {
                std::cerr << "WARNING (template): thread " << std::this_thread::get_id() <<
                    " failed to push data onto output queue" << std::endl;
            }
        }
    }

    /**
     * This method is equivalent to Parser::thread_routine with the only difference 
     * being that threads die when the input queue is empty. This scenario is ideal 
     * for measuring throughput since we do not require an additional thread waiting 
     * until the parser finishes and consuming CPU time.
     */
    void thread_routine_wait(int index) {
        moodycamel::ReaderWriterQueue<input_t> &my_input_queue = _input_queues[index];
        moodycamel::ReaderWriterQueue<output_t> &my_output_queue = _output_queues[index];
        input_t cur_input;
        output_t cur_output;
        bool success;

        while (true) {
            success = my_input_queue.try_dequeue(cur_input);
            if (!success) {
                return;
            }
            _conversion_function(cur_input, cur_output);
            success = my_output_queue.enqueue(cur_output);
            if (UNLIKELY(!success)) {
                std::cerr << "WARNING (template): thread " << std::this_thread::get_id() <<
                    " failed to push data onto output queue" << std::endl;
            }
        }
    }

    /**
     * This method is equivalent to Parser::thread_routine with the only difference 
     * being that threads sleep when their input queues are empty.
     */
    void thread_routine_sleep(int index, const struct timespec *req) {
        moodycamel::ReaderWriterQueue<input_t> &my_input_queue = _input_queues[index];
        moodycamel::ReaderWriterQueue<output_t> &my_output_queue = _output_queues[index];
        input_t cur_input;
        output_t cur_output;
        bool success;

        while (LIKELY(_threads_active)) {
            success = my_input_queue.try_dequeue(cur_input);
            if (!success) {
                nanosleep(req, nullptr);
                continue;
            }
            _conversion_function(cur_input, cur_output);
            success = my_output_queue.enqueue(cur_output);
            if (UNLIKELY(!success)) {
                std::cerr << "WARNING (template): thread " << std::this_thread::get_id() <<
                    " failed to push data onto output queue" << std::endl;
            }
        }
    }

    template <class T>
    struct PaddedValue {
        PaddedValue(T const& val) : _val(val) { }

        T _val;
        char _padding[MOODYCAMEL_CACHE_LINE_SIZE - sizeof(_val)];
    };

    std::thread *_threads;
    // No need to worry about false sharing here since 
    // sizeof(ReaderWriterQueue<T>) = 128, which is larger than
    // x86-64 cache lines (64 bytes)
    //
    moodycamel::ReaderWriterQueue<input_t> *_input_queues;
    moodycamel::ReaderWriterQueue<output_t> *_output_queues;
    const unsigned int _num_threads;
    // We pad the input and output indices to avoid false sharing 
    // between calls to Parser::push() and Parser::pop()
    //
    PaddedValue<unsigned int> _cur_input_index, _cur_output_index;
    void (*_conversion_function)(input_t const&, output_t&);
    bool _threads_active;
};

} // namespace libilf

