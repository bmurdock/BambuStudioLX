#include <catch2/catch.hpp>
#include "slic3r/GUI/fila_manager/CloudRequestWorker.h"
#include <future>
#include <vector>

using Slic3r::GUI::CloudRequestWorker;

TEST_CASE("Cloud requests reuse one worker and preserve order", "[fila_manager][worker]")
{
    CloudRequestWorker worker;
    std::vector<int> order;
    std::vector<std::thread::id> threads;
    std::promise<void> completed;
    for (int i = 0; i < 40; ++i) worker.submit([&, i] {
        order.push_back(i);
        threads.push_back(std::this_thread::get_id());
        if (i == 39) completed.set_value();
    });
    completed.get_future().get();
    worker.shutdown();
    REQUIRE(order.size() == 40);
    for (int i = 0; i < 40; ++i) {
        REQUIRE(order[i] == i);
        REQUIRE(threads[i] == threads.front());
        REQUIRE(threads[i] != std::this_thread::get_id());
    }
}

TEST_CASE("Cloud worker joins active requests and rejects work after shutdown", "[fila_manager][worker]")
{
    CloudRequestWorker worker;
    std::promise<void> entered, release;
    auto gate = release.get_future();
    bool finished = false;
    worker.submit([&] { entered.set_value(); gate.wait(); finished = true; });
    entered.get_future().get();
    auto stopper = std::async(std::launch::async, [&] { worker.shutdown(); });
    release.set_value();
    stopper.get();
    REQUIRE(finished);
    worker.submit([&] { finished = false; });
    worker.shutdown();
    REQUIRE(finished);
}
