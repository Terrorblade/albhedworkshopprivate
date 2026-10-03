#include "workshop/Protocol.h"

namespace workshop
{

	const char* MessageKindName(uint8_t kind)
	{
		switch (kind)
		{
		case MessageHello:
			return "hello";
		case MessageWelcome:
			return "welcome";
		case MessageReject:
			return "reject";
		case MessageHeartbeat:
			return "heartbeat";
		case MessageGoodbye:
			return "goodbye";
		case MessageInput:
			return "input";
		case MessageCommand:
			return "command";
		case MessageCommandAsk:
			return "command ask";
		case MessageChecksum:
			return "checksum";
		case MessageWorldRequest:
			return "world request";
		case MessageWorldSnapshot:
			return "world snapshot";
		case MessageWorldApplied:
			return "world applied";
		case MessageWorldAnchor:
			return "world anchor";
		default:
			return "unknown";
		}
	}

	// Worded for the player rather than for the developer, because these end up in
	// front of whoever could not connect. "Connection failed" sends people hunting
	// their firewall when the real answer is that one of them forgot to rebuild.
	const char* RejectReasonText(uint8_t reason)
	{
		switch (reason)
		{
		case RejectVersionMismatch:
			return "the two copies speak different protocol versions, so one of "
			       "you is on an older build of the mod";
		case RejectBuildMismatch:
			return "same protocol but a different mod build, so rebuild and copy "
			       "the same DLL to both machines";
		case RejectSessionFull:
			return "the session is already full";
		case RejectSettingsMismatch:
			return "a setting that has to match does not, check the shared "
			       "settings on both machines";
		case RejectNotHosting:
			return "that machine is not hosting";
		default:
			return "no reason given";
		}
	}

} // namespace workshop
