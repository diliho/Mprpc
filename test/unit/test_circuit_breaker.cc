#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include "circuit_breaker.h"

using namespace mprpc;

void TestCircuitBreakerBasic() {
    std::cout << "Testing CircuitBreaker Basic..." << std::endl;
    
    SlidingWindowCircuitBreaker cb(10, 5, 1, 3);
    
    assert(cb.GetState() == SlidingWindowCircuitBreaker::State::CLOSED);
    assert(cb.AllowRequest());
    
    for (int i = 0; i < 4; ++i) {
        cb.RecordFailure();
        assert(cb.GetState() == SlidingWindowCircuitBreaker::State::CLOSED);
    }
    
    cb.RecordFailure();
    assert(cb.GetState() == SlidingWindowCircuitBreaker::State::OPEN);
    assert(!cb.AllowRequest());
    
    std::cout << "CircuitBreaker Basic: PASSED" << std::endl;
}

void TestCircuitBreakerRecovery() {
    std::cout << "Testing CircuitBreaker Recovery..." << std::endl;
    
    SlidingWindowCircuitBreaker cb(10, 3, 1, 2);
    
    cb.RecordFailure();
    cb.RecordFailure();
    cb.RecordFailure();
    assert(cb.GetState() == SlidingWindowCircuitBreaker::State::OPEN);
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    assert(cb.AllowRequest());
    assert(cb.GetState() == SlidingWindowCircuitBreaker::State::HALF_OPEN);
    
    cb.RecordSuccess();
    cb.RecordSuccess();
    assert(cb.GetState() == SlidingWindowCircuitBreaker::State::CLOSED);
    
    std::cout << "CircuitBreaker Recovery: PASSED" << std::endl;
}

void TestCircuitBreakerHalfOpenFailure() {
    std::cout << "Testing CircuitBreaker HalfOpen Failure..." << std::endl;
    
    SlidingWindowCircuitBreaker cb(10, 3, 1, 2);
    
    cb.RecordFailure();
    cb.RecordFailure();
    cb.RecordFailure();
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    cb.AllowRequest();
    assert(cb.GetState() == SlidingWindowCircuitBreaker::State::HALF_OPEN);
    
    cb.RecordFailure();
    assert(cb.GetState() == SlidingWindowCircuitBreaker::State::OPEN);
    
    std::cout << "CircuitBreaker HalfOpen Failure: PASSED" << std::endl;
}

void TestCircuitBreakerReset() {
    std::cout << "Testing CircuitBreaker Reset..." << std::endl;
    
    SlidingWindowCircuitBreaker cb(10, 3, 1, 2);
    
    cb.RecordFailure();
    cb.RecordFailure();
    cb.RecordFailure();
    
    cb.Reset();
    assert(cb.GetState() == SlidingWindowCircuitBreaker::State::CLOSED);
    assert(cb.AllowRequest());
    
    std::cout << "CircuitBreaker Reset: PASSED" << std::endl;
}

void TestCircuitBreakerFailureRate() {
    std::cout << "Testing CircuitBreaker FailureRate..." << std::endl;
    
    SlidingWindowCircuitBreaker cb(10, 100, 1, 3);
    
    for (int i = 0; i < 5; ++i) cb.RecordSuccess();
    for (int i = 0; i < 5; ++i) cb.RecordFailure();
    
    double rate = cb.GetFailureRate();
    assert(rate >= 0.49 && rate <= 0.51);
    
    std::cout << "CircuitBreaker FailureRate: PASSED" << std::endl;
}

int main() {
    TestCircuitBreakerBasic();
    TestCircuitBreakerRecovery();
    TestCircuitBreakerHalfOpenFailure();
    TestCircuitBreakerReset();
    TestCircuitBreakerFailureRate();
    
    std::cout << "\nAll circuit breaker tests PASSED!" << std::endl;
    return 0;
}
