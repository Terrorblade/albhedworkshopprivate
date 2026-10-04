#pragma once

#include <stdint.h>

// The event hub. The workshop raises these, a plugin subscribes to the ones it
// cares about, and several plugins can take the same event without fighting over
// a hook slot.
//
//     static void OnStep(const workshop::Event& e, void*)
//     {
//         DoThing(e.step);
//     }
//
//     workshop::Subscribe(workshop::EventStep, &OnStep);
//
// Want something the hub does not raise? Detour it yourself and Publish your own
// event, or just call your code. Nothing here is mandatory.
//
// Everything dispatches synchronously on the thread that published, which for
// every event the hub raises is the game thread.

namespace workshop
{

	enum EventId
	{
		// step is the simulation step. value and pointer are unused unless noted.
		EventFrame = 0,    // once per rendered frame
		EventStep,         // once per simulation step, 29.97 Hz, after the gate
		EventSubStep,      // once per sub step, value = which one, 0 based
		EventShutdown,     // the plugin is unloading

		EventSessionStarted, // value = 1 when hosting
		EventSessionStopped,
		EventPeerJoined, // value = peer index
		EventPeerLeft,   // value = peer index

		EventMapChanged,  // value = the new map id
		EventBattleBegin, // value = the encounter id
		EventBattleEnd,
		EventMenuOpened, // value = the menu module id
		EventMenuClosed,
		EventPaused, // value = a bitmask of why
		EventResumed,
		EventFmvBegin,
		EventFmvEnd,

		EventCount
	};

	struct Event
	{
		EventId id;
		uint32_t step;
		uint32_t value;
		void* pointer;
	};

	typedef void (*EventFn)(const Event& event, void* user);

	// Returns a token, or 0 when the table is full. Subscribers run in the order
	// they subscribed.
	int Subscribe(EventId id, EventFn fn, void* user = nullptr);
	void Unsubscribe(int token);

	// Safe to call from inside a subscriber, including Unsubscribe on yourself.
	void Publish(const Event& event);

	// Shorthand for the common shapes.
	void Publish(EventId id, uint32_t step);
	void Publish(EventId id, uint32_t step, uint32_t value);

	int SubscriberCount(EventId id);
	const char* EventName(EventId id);

	// How many times each event has been raised, for a readout.
	uint32_t PublishCount(EventId id);
	void LogEvents();

} // namespace workshop
