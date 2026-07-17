#include <iostream>
#include <cassert>
#include <vector>
#include <thread>
#include "pool/memory_pool.h"
#include "pool/object_pool.h"

using namespace mprpc;

void TestMemoryPool() {
    std::cout << "Testing MemoryPool..." << std::endl;
    
    MemoryPool pool(64, 10);
    
    std::vector<void*> ptrs;
    for (int i = 0; i < 100; ++i) {
        void* ptr = pool.Allocate();
        assert(ptr != nullptr);
        ptrs.push_back(ptr);
    }
    
    assert(pool.GetAllocatedCount() == 100);
    
    for (void* ptr : ptrs) {
        pool.Deallocate(ptr);
    }
    
    assert(pool.GetAllocatedCount() == 0);
    
    std::cout << "MemoryPool: PASSED" << std::endl;
}

void TestObjectPool() {
    std::cout << "Testing ObjectPool..." << std::endl;
    
    ObjectPool<int> pool(10);
    
    std::vector<std::unique_ptr<int, std::function<void(int*)>>> objs;
    for (int i = 0; i < 20; ++i) {
        auto obj = pool.Acquire();
        assert(obj != nullptr);
        *obj = i;
        objs.push_back(std::move(obj));
    }
    
    assert(pool.InUseCount() == 20);
    
    objs.clear();
    
    assert(pool.FreeCount() == 20);
    assert(pool.InUseCount() == 0);
    
    std::cout << "ObjectPool: PASSED" << std::endl;
}

void TestObjectPoolConcurrency() {
    std::cout << "Testing ObjectPool Concurrency..." << std::endl;
    
    ObjectPool<int> pool(10);
    std::vector<std::thread> threads;
    
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&pool, t]() {
            for (int i = 0; i < 100; ++i) {
                auto obj = pool.Acquire();
                *obj = t * 100 + i;
                std::this_thread::yield();
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    std::cout << "ObjectPool Concurrency: PASSED" << std::endl;
}

int main() {
    TestMemoryPool();
    TestObjectPool();
    TestObjectPoolConcurrency();
    
    std::cout << "\nAll pool tests PASSED!" << std::endl;
    return 0;
}
