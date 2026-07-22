#pragma once

#include <QObject>
#include <QSemaphore>
#include <QThread>

namespace render
{
	/**
	 * Runs `fn` on `receiver`'s thread and returns its result, blocking until it has run. Runs
	 * inline when the caller is already on that thread, so a closure may call back in.
	 *
	 * A throw inside `fn` comes back to the caller rather than escaping into the receiver's event
	 * loop (where it would terminate) and leaving the semaphore unreleased (deadlocking this wait).
	 * Reference capture is safe: acquire() blocks here until the closure has released, so the
	 * captures outlive it.
	 */
	template <typename Fn>
	std::invoke_result_t<Fn>
	Invoke(QObject& receiver, Fn&& fn)
	{
		using Result = std::invoke_result_t<Fn>;

		if (receiver.thread() == QThread::currentThread())
			return std::invoke(std::forward<Fn>(fn));

		QSemaphore         done;
		std::exception_ptr error;
		if constexpr (std::is_void_v<Result>)
		{
			QMetaObject::invokeMethod(
				&receiver,
				[&] {
					try
					{
						std::invoke(fn);
					}
					catch (...)
					{
						error = std::current_exception();
					}
					done.release();
				},
				Qt::QueuedConnection);
			done.acquire();
			if (error)
				std::rethrow_exception(error);
		}
		else
		{
			Result result{};
			QMetaObject::invokeMethod(
				&receiver,
				[&] {
					try
					{
						result = std::invoke(fn);
					}
					catch (...)
					{
						error = std::current_exception();
					}
					done.release();
				},
				Qt::QueuedConnection);
			done.acquire();
			if (error)
				std::rethrow_exception(error);
			return result;
		}
	}
}
