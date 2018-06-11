#ifndef more_dispatch_h
#define more_dispatch_h

#include <assert.h>

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

namespace more
{
	// -------------------------------------------------------------------------
	// dispatch_block: container for any no-args function returning void.
	// These are the objects that you can send to a dispatch queue.

	struct movable_function_base
	{
		virtual void invoke() = 0;
		virtual void move_to(void* dest, size_t size) = 0;
		virtual ~movable_function_base() = default;
	};

	template <typename F> struct movable_function : public movable_function_base
	{
		F _function;

		movable_function(F&& function) : _function(std::move(function)) {}

		virtual void invoke() { _function(); }

		virtual void move_to(void* dest, size_t size)
		{
			assert(size >= sizeof(*this));
			new (dest) movable_function(std::move(*this));
		}
	};

	struct dispatch_block : public movable_function_base
	{
		void* _space[3];

		dispatch_block() = delete;
		dispatch_block(const dispatch_block&) = delete;

		dispatch_block(dispatch_block&& other)
		{
			other.move_to(this, sizeof(*this));
		}

		dispatch_block(movable_function_base&& other)
		{
			other.move_to(this, sizeof(*this));
		}

		virtual void move_to(void* dest, size_t size) { assert(false); }

		virtual void invoke() { assert(false); }

		void operator()() { invoke(); }
	};

	// -------------------------------------------------------------------------
	// dispatch_queue: receives blocks, executes them in FIFO order.
	//
	// Call dispatch() to add a block to the queue.
	// Call run_once() or run_forever() to execute queued blocks.
	// Call stop() to stop accepting new blocks.
	// Call wait_until_done() to wait until the queue is empty.
	//
	// All methods are thread-safe. However, to ensure that blocks are
	// executed in strict FIFO order, only one thread should call run_*().
	//
	// Normally it's easier to use dispatch_thread (below) which handles
	// threading automatically. This class is useful for integrating into an
	// existing thread; just call run_once() from your inner loop.
	//
	// The destructor calls stop() and wait_until_done(). Beware that this may
	// deadlock unless another thread is calling run_once() or run_forever().
	// (Alternatively, you could call stop() and run_forever() on your main
	// thread to flush out the queue before destroying it.)

	class dispatch_queue
	{
		std::mutex _mutex;
		std::condition_variable _cond;
		bool _done = false;
		std::vector<dispatch_block> _queue;

		bool _dispatch(dispatch_block&& block)
		{
			std::lock_guard<std::mutex> lock(_mutex);
			if (_done) return false;

			_queue.push_back(std::move(block));
			_cond.notify_all();
			return true;
		}

	public:
		// Queue a lambda expression or functor for execution.
		// Returns true on success, false if the queue is stopped.
		template <typename F> bool dispatch(F&& lambda)
		{
			movable_function<F> function(std::move(lambda));
			return _dispatch(std::move(function));
		}

		// Stop accepting new blocks. dispatch() will now return false.
		// This method is idempotent; it's safe to call it multiple times.
		void stop()
		{
			std::lock_guard<std::mutex> lock(_mutex);
			_done = true;
			_cond.notify_all();
		}

		// Wait until the queue is stopped and all outstanding blocks have run.
		void wait_until_done()
		{
			std::unique_lock<std::mutex> lock(_mutex);
			while (!_done || !_queue.empty())
				_cond.wait(lock);
		}

		// Grab some blocks from the queue, if any are available, and run them.
		void run_once()
		{
			std::vector<dispatch_block> q;
			{
				std::lock_guard<std::mutex> lock(_mutex);
				std::swap(q, _queue);
			}
			if (q.empty() && _done) {
				_cond.notify_all();
				return;
			}

			for (auto& block : q)
				block.invoke();
		}

		// Run blocks as they arrive. Will not return until stop() is called.
		// When it does return, the queue is guaranteed to be stopped and empty.
		// This method should typically be called from a background thread.
		void run_forever()
		{
			while (true) {
				std::vector<dispatch_block> q;
				{
					std::unique_lock<std::mutex> lock(_mutex);
					while (_queue.empty() && !_done)
						_cond.wait(lock);
					std::swap(q, _queue);
				}
				if (q.empty()) {
					assert(_done);
					_cond.notify_all();
					return;
				}

				for (auto& block : q)
					block.invoke();
			}
		}

		// Destructor. Stops the queue and waits for outstanding blocks to run.
		~dispatch_queue()
		{
			stop();
			wait_until_done();
		}
	};

	// -------------------------------------------------------------------------
	// dispatch_thread: runs a dispatch_queue in a single background thread.
	//
	// Call dispatch() to execute a block in the background.
	// Call stop() to stop accepting new blocks.
	//
	// The destructor calls stop() then waits for outstanding blocks to finish.

	class dispatch_thread
	{
		dispatch_queue _queue;
		std::thread _thread;

	public:
		dispatch_thread() : _thread(&dispatch_queue::run_forever, &_queue) {}

		// Get this thread's dispatch queue.
		dispatch_queue& queue() { return _queue; }

		// Post a lambda expression for execution on the background thread.
		// Returns true on success, false if the queue is stopped.
		template <typename F> bool dispatch(F&& function)
		{
			return _queue.dispatch<F>(std::move(function));
		}

		// Stop accepting new blocks. dispatch() will now return false.
		// This method is idempotent; it's safe to call it multiple times.
		void stop() { _queue.stop(); }

		// Destructor. Stops the queue and waits for outstanding blocks to run.
		~dispatch_thread()
		{
			_queue.stop();
			_queue.wait_until_done();
			_thread.join();
		}
	};
} // namespace more

#endif // more_dispatch_h
