#include <assert.h>
#include <stdio.h>

#include <chrono>
#include <thread>

#include "../include/more_dispatch/more_dispatch.h"

void test_dispatch_queue()
{
	printf("Basic dispatch_queue test (no thread)...\n");

	more::dispatch_queue t;
	t.dispatch([] { printf(" Hello"); });
	t.dispatch([] { printf(" world!"); });
	t.dispatch([] { printf("\n"); });
	t.stop();

	bool success = t.dispatch([] { printf("This should be not be printed\n"); });
	assert(!success);

	t.run_forever();
}

void test_dispatch_thread()
{
	printf("Basic dispatch_thread test...\n");

	more::dispatch_thread t;
	t.dispatch([] { printf(" Hello"); });
	t.dispatch([] { printf(" world!"); });
	t.dispatch([] { printf("\n"); });
}

void infinite_increment(int& counter, more::dispatch_thread& t)
{
	++counter;
	t.dispatch([&] { infinite_increment(counter, t); });
}

void test_infinite_recursion()
{
	printf("Testing that we can stop() a recursive loop...\n");
	int count1 = 0;
	int count2 = 0;
	{
		more::dispatch_thread t;
		t.dispatch([&] { infinite_increment(count1, t); });
		t.dispatch([&] { infinite_increment(count2, t); });
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	printf(" count1: %d, count2: %d\n", count1, count2);
}

int main(int argc, const char* argv[])
{
	test_dispatch_queue();
	test_dispatch_thread();
	test_infinite_recursion();
	return 0;
}
