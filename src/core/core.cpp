/*
 * Copyright 2025 Titan Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


// Titan Core Engine - Implementation
// Thread utilities for CPU affinity and core count

#include "core.hpp"

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

#include <thread>

namespace titan::core {

std::error_code pin_thread_to_core(uint32_t core_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        return std::error_code(ret, std::system_category());
    }
#elif defined(__APPLE__)
    // macOS doesn't support thread affinity in the same way
    // This is a no-op on macOS
    (void)core_id;
#endif

    return {};
}

uint32_t get_cpu_count() {
    return std::thread::hardware_concurrency();
}

} // namespace titan::core
