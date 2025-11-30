// Titan Memory Management Unit Tests

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../../src/core/memory.hpp"

using namespace titan::core;

TEST_CASE("Arena basic allocation", "[memory][arena]") {
    Arena arena(1024);

    SECTION("Allocate single object") {
        int* ptr = arena.allocate<int>();
        REQUIRE(ptr != nullptr);
        *ptr = 42;
        REQUIRE(*ptr == 42);
    }

    SECTION("Allocate multiple objects") {
        int* arr = arena.allocate<int>(10);
        REQUIRE(arr != nullptr);

        for (int i = 0; i < 10; ++i) {
            arr[i] = i;
        }

        for (int i = 0; i < 10; ++i) {
            REQUIRE(arr[i] == i);
        }
    }

    SECTION("Allocate with custom alignment") {
        void* ptr = arena.allocate(64, 64);
        REQUIRE(ptr != nullptr);
        REQUIRE(reinterpret_cast<uintptr_t>(ptr) % 64 == 0);
    }
}

TEST_CASE("Arena string copy", "[memory][arena]") {
    Arena arena(1024);

    SECTION("Copy short string") {
        std::string_view original = "Hello, World!";
        std::string_view copied = arena.copy_string(original);

        REQUIRE(copied == original);
        REQUIRE(copied.data() != original.data());  // Different memory
    }

    SECTION("Copy empty string") {
        std::string_view copied = arena.copy_string("");
        REQUIRE(copied.empty());
    }
}

TEST_CASE("Arena reset", "[memory][arena]") {
    Arena arena(1024);

    SECTION("Reset clears allocations") {
        size_t initial_used = arena.bytes_allocated();
        REQUIRE(initial_used == 0);

        arena.allocate<int>(100);
        size_t after_alloc = arena.bytes_allocated();
        REQUIRE(after_alloc > 0);

        arena.reset();
        REQUIRE(arena.bytes_allocated() == 0);
    }
}

TEST_CASE("Arena growth", "[memory][arena]") {
    Arena arena(64);  // Small initial size

    SECTION("Grows when needed") {
        size_t initial_capacity = arena.capacity();

        // Allocate more than initial capacity
        void* ptr = arena.allocate(128);
        REQUIRE(ptr != nullptr);
        REQUIRE(arena.capacity() > initial_capacity);
    }
}

TEST_CASE("Arena construct", "[memory][arena]") {
    Arena arena(1024);

    struct TestStruct {
        int x;
        double y;
        TestStruct(int x_, double y_) : x(x_), y(y_) {}
    };

    SECTION("Construct single object") {
        TestStruct* obj = arena.construct<TestStruct>(42, 3.14);
        REQUIRE(obj != nullptr);
        REQUIRE(obj->x == 42);
        REQUIRE(obj->y == 3.14);
    }
}

TEST_CASE("ArenaScope RAII", "[memory][arena]") {
    Arena arena(1024);

    arena.allocate<int>(10);
    REQUIRE(arena.bytes_allocated() > 0);

    {
        ArenaScope scope(arena);
        arena.allocate<int>(10);
        REQUIRE(arena.bytes_allocated() > 0);
    }

    // Should be reset after scope ends
    REQUIRE(arena.bytes_allocated() == 0);
}

TEST_CASE("Arena allocate_array returns span", "[memory][arena][span]") {
    Arena arena(1024);

    SECTION("Allocate and access via span") {
        auto buffer = arena.allocate_array<int>(5);
        REQUIRE(buffer.size() == 5);

        // Initialize values
        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = static_cast<int>(i * 10);
        }

        // Verify values
        REQUIRE(buffer[0] == 0);
        REQUIRE(buffer[1] == 10);
        REQUIRE(buffer[2] == 20);
        REQUIRE(buffer[3] == 30);
        REQUIRE(buffer[4] == 40);
    }

    SECTION("Range-based for loop") {
        auto buffer = arena.allocate_array<int>(3);
        buffer[0] = 1;
        buffer[1] = 2;
        buffer[2] = 3;

        int sum = 0;
        for (int val : buffer) {
            sum += val;
        }
        REQUIRE(sum == 6);
    }
}

TEST_CASE("ObjectPool acquire and release", "[memory][pool]") {
    ObjectPool<int, 4> pool;

    SECTION("Acquire from pool") {
        int* obj1 = pool.acquire(42);
        REQUIRE(obj1 != nullptr);
        REQUIRE(*obj1 == 42);

        pool.release(obj1);
    }

    SECTION("Pool reuse") {
        int* obj1 = pool.acquire(1);
        int* first_addr = obj1;
        pool.release(obj1);

        int* obj2 = pool.acquire(2);
        REQUIRE(obj2 == first_addr);  // Should reuse same address
        REQUIRE(*obj2 == 2);

        pool.release(obj2);
    }

    SECTION("Pool exhaustion") {
        std::vector<int*> objects;

        // Exhaust pool (size 4)
        for (int i = 0; i < 4; ++i) {
            objects.push_back(pool.acquire(i));
        }

        // Next allocation should go to heap
        int* heap_obj = pool.acquire(999);
        REQUIRE(heap_obj != nullptr);

        for (auto* obj : objects) {
            pool.release(obj);
        }
        pool.release(heap_obj);
    }
}
