#ifndef more_dispatch_h
#define more_dispatch_h

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <queue>
#include <utility>

namespace more
{
	struct dispatch_block
	{
		virtual void invoke() = 0;
		virtual ~dispatch_block() = default;

		typedef std::unique_ptr<dispatch_block> ptr;
	};

	template <typename F> class dispatch_lambda : public dispatch_block
	{
		F _lambda;
	public:
		explicit dispatch_lambda(F&& lambda) : _lambda(std::move(lambda)) {}
		void invoke() { _lambda(); }
	};

	class dispatch_queue
	{
		std::mutex _mutex;
		std::condition_variable _cond;
		bool _done = false;
		std::vector<dispatch_block::ptr> _queue;

	public:
		~dispatch_queue()
		{
			wait_until_done();
		}

		bool dispatch(dispatch_block::ptr& block)
		{
			std::lock_guard<std::mutex> lock (_mutex);
			if (_done) return false;

			_queue.push_back(std::move(block));
			_cond.notify_all();
			return true;
		}

		template <typename F> bool dispatch(F&& function)
		{
			typedef dispatch_lambda<F> lambda;
			auto block = dispatch_block::ptr(new lambda(std::move(function)));
			return dispatch(block);
		}

		void stop()
		{
			std::lock_guard<std::mutex> lock (_mutex);
			_done = true;
			_cond.notify_all();
		}

		void wait_until_done()
		{
			std::unique_lock<std::mutex> lock (_mutex);
			while (!_done || !_queue.empty())
				_cond.wait(lock);
		}

		void run_once()
		{
			std::vector<dispatch_block::ptr> q;
			{
				std::lock_guard<std::mutex> lock (_mutex);
				std::swap(q, _queue);
			}

			for (auto& block : q)
				block->invoke();
		}

		void run_forever()
		{
			while (true) {
				std::vector<dispatch_block::ptr> q;
				{
					std::unique_lock<std::mutex> lock (_mutex);
					while (_queue.empty() && !_done)
						_cond.wait(lock);
					std::swap(q, _queue);
				}
				if (q.empty())
					return;
				for (auto& block : q)
					block->invoke();
			}
		}
	};

	class dispatch_thread
	{
		dispatch_queue _queue;
		std::thread _thread;

	public:
		dispatch_thread() : _thread(&dispatch_queue::run_forever, &_queue) {}

		~dispatch_thread() {
			_queue.stop();
			_queue.wait_until_done();
			_thread.join();
		}

		bool dispatch(dispatch_block::ptr& block)
		{
			return _queue.dispatch(block);
		}

		template <typename F> bool dispatch(F&& function)
		{
			return _queue.dispatch<F>(std::move(function));
		}
	};
}

#endif // more_dispatch_h
