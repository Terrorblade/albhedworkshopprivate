#include "workshop/Events.h"

#include <string.h>

#include "workshop/Log.h"

namespace workshop
{

	namespace
	{

		// Fixed size on purpose. Dispatch runs on the frame path, so nothing here
		// allocates and nothing takes a lock.
		const int MaxSubscribers = 64;

		struct Subscriber
		{
			EventId id;
			EventFn fn;
			void* user;
			int token;
			bool dead;
		};

		Subscriber g_subscribers[MaxSubscribers];
		int g_nextToken = 1;
		int g_dispatchDepth = 0;
		bool g_hasDead = false;
		uint32_t g_counts[EventCount];

		const char* const EventNames[EventCount] = {
			"Frame", "Step", "SubStep", "Shutdown",
			"SessionStarted", "SessionStopped", "PeerJoined", "PeerLeft",
			"MapChanged", "BattleBegin", "BattleEnd", "MenuOpened", "MenuClosed",
			"Paused", "Resumed", "FmvBegin", "FmvEnd"
		};

		typedef char EventNamesCoverEveryId[(sizeof(EventNames) / sizeof(EventNames[0]) == EventCount) ? 1 : -1];

		// Unsubscribing during dispatch only marks the slot, because the loop is
		// holding an index into the table. This is where it actually goes.
		void Compact()
		{
			if (!g_hasDead || g_dispatchDepth != 0)
				return;

			int write = 0;
			for (int read = 0; read < MaxSubscribers; ++read)
			{
				if (!g_subscribers[read].fn || g_subscribers[read].dead)
					continue;
				if (write != read)
					g_subscribers[write] = g_subscribers[read];
				++write;
			}
			for (int i = write; i < MaxSubscribers; ++i)
				memset(&g_subscribers[i], 0, sizeof(Subscriber));
			g_hasDead = false;
		}

	} // namespace

	int Subscribe(EventId id, EventFn fn, void* user)
	{
		if (!fn || id < 0 || id >= EventCount)
			return 0;

		Compact();

		for (int i = 0; i < MaxSubscribers; ++i)
		{
			if (g_subscribers[i].fn)
				continue;
			g_subscribers[i].id = id;
			g_subscribers[i].fn = fn;
			g_subscribers[i].user = user;
			g_subscribers[i].token = g_nextToken++;
			g_subscribers[i].dead = false;
			return g_subscribers[i].token;
		}

		Log("events: no room for a subscriber on %s, %d is the limit", EventName(id), MaxSubscribers);
		return 0;
	}

	void Unsubscribe(int token)
	{
		if (token <= 0)
			return;
		for (int i = 0; i < MaxSubscribers; ++i)
			if (g_subscribers[i].fn && g_subscribers[i].token == token)
			{
				g_subscribers[i].dead = true;
				g_hasDead = true;
				Compact();
				return;
			}
	}

	void Publish(const Event& event)
	{
		if (event.id < 0 || event.id >= EventCount)
			return;

		++g_counts[event.id];
		++g_dispatchDepth;
		for (int i = 0; i < MaxSubscribers; ++i)
		{
			const Subscriber& sub = g_subscribers[i];
			if (!sub.fn || sub.dead || sub.id != event.id)
				continue;
			sub.fn(event, sub.user);
		}
		--g_dispatchDepth;
		Compact();
	}

	void Publish(EventId id, uint32_t step)
	{
		Event event;
		event.id = id;
		event.step = step;
		event.value = 0;
		event.pointer = nullptr;
		Publish(event);
	}

	void Publish(EventId id, uint32_t step, uint32_t value)
	{
		Event event;
		event.id = id;
		event.step = step;
		event.value = value;
		event.pointer = nullptr;
		Publish(event);
	}

	int SubscriberCount(EventId id)
	{
		int count = 0;
		for (int i = 0; i < MaxSubscribers; ++i)
			if (g_subscribers[i].fn && !g_subscribers[i].dead && g_subscribers[i].id == id)
				++count;
		return count;
	}

	const char* EventName(EventId id)
	{
		if (id < 0 || id >= EventCount)
			return "?";
		return EventNames[id];
	}

	uint32_t PublishCount(EventId id)
	{
		if (id < 0 || id >= EventCount)
			return 0;
		return g_counts[id];
	}

	void LogEvents()
	{
		for (int i = 0; i < EventCount; ++i)
		{
			const EventId id = (EventId)i;
			const int subs = SubscriberCount(id);
			if (subs == 0 && g_counts[i] == 0)
				continue;
			Log("events : %-15s %d subscriber%s, raised %lu time%s",
			    EventName(id), subs, subs == 1 ? "" : "s",
			    (unsigned long)g_counts[i], g_counts[i] == 1 ? "" : "s");
		}
	}

} // namespace workshop
