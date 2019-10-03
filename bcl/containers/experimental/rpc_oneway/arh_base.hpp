//
// Created by Jiakun Yan on 10/1/19.
//

#ifndef BCL_ARH_BASE_HPP
#define BCL_ARH_BASE_HPP

#include "bcl/bcl.hpp"
#include "arh_am.hpp"
#include <functional>
#include <iostream>
#include <thread>
#include <vector>
#include <unordered_map>
#include <atomic>

namespace ARH {
  std::unordered_map<std::thread::id, size_t> worker_ids;
  std::unordered_map<std::thread::id, size_t> progress_ids;
  size_t num_threads_per_node = 32;
  size_t num_workers_per_node = 30;
  std::atomic<bool> worker_run = true;
  std::atomic<size_t> workers_initialized;

  inline size_t my_worker() {
    return worker_ids[std::this_thread::get_id()];
  }

  inline size_t nworkers() {
    return BCL::nprocs() * num_workers_per_node;
  }

  inline size_t my_local_worker() {
    return my_worker() % num_workers_per_node;
  }

  inline size_t my_proc() {
    return BCL::rank();
  }

  inline size_t nprocs() {
    return BCL::nprocs();
  }

  inline void barrier() {
    flush_am();
    if (my_local_worker() == 0) {
      BCL::barrier();
    }
    // local_barrier();
  }

  void progress() {
    gasnet_AMPoll();
  }

  // progress thread
  void progress_thread() {
    while (worker_run.load()) {
      progress();
    }
  }

  inline void init(size_t shared_segment_size = 256) {
    BCL::init(shared_segment_size, true);
    init_am();
  }

  inline void finalize() {
    BCL::finalize();
  }

  void worker_handler(const std::function<void(void)>& worker) {
    while (workers_initialized != num_workers_per_node) {}
    barrier();
    worker();
  }

  void run(const std::function<void(void)> &worker, size_t custom_num_workers_per_node = 30,
           size_t custom_num_threads_per_node = 32) {
//    std::vector<std::thread> thread_pool(num_workers_per_node, std::thread(worker));
    num_workers_per_node = custom_num_workers_per_node;
    num_threads_per_node = custom_num_threads_per_node;

    std::vector<std::thread> worker_pool;

    workers_initialized = 0;

    cpu_set_t cpuset[8];

    for (size_t i = 0; i < num_workers_per_node; ++i) {
      auto t = std::thread(worker_handler, worker);
      CPU_ZERO(&cpuset);
      CPU_SET(i, (cpu_set_t *) &cpuset);
      // int rv = pthread_setaffinity_np(t.native_handle(), 8, (cpu_set_t *) &cpuset);
      int rv = 0;
      if (rv != 0) {
        throw std::runtime_error("ERROR Didn't work.");
      }
      worker_ids[t.get_id()] = i + BCL::rank() * num_workers_per_node;
      workers_initialized++;
      worker_pool.push_back(std::move(t));
    }

    std::vector<std::thread> progress_pool;
    size_t num_progress_per_node = num_threads_per_node - num_workers_per_node;
    for (size_t i = 0; i < num_progress_per_node; ++i) {
      auto t = std::thread(progress_thread);
      CPU_ZERO(&cpuset);
      CPU_SET(i + num_workers_per_node, (cpu_set_t *) &cpuset);
      // int rv = pthread_setaffinity_np(t.native_handle(), 8, (cpu_set_t *) &cpuset);
      int rv = 0;
      if (rv != 0) {
        throw std::runtime_error("ERROR Didn't work.");
      }
      progress_ids[t.get_id()] = i + BCL::rank() * num_workers_per_node;
      progress_pool.push_back(std::move(t));
    }

    for (size_t i = 0; i < num_workers_per_node; ++i) {
      worker_pool[i].join();
    }

    ARH::barrier();
    worker_run = false;
//    std::printf("process %lu finished\n", my_proc());

    for (size_t i = 0; i < num_progress_per_node; ++i) {
      progress_pool[i].join();
    }
  }

  template <typename ...Args>
  void print(std::string format, Args... args) {
    fflush(stdout);
    ARH::barrier();
    if (ARH::my_worker() == 0) {
      std::printf(format.c_str(), args...);
    }
    fflush(stdout);
    ARH::barrier();
  }

}

#endif //BCL_ARH_BASE_HPP
