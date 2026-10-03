#pragma once

#include <windows.h>

#include "ffx/Api.h"

// Reading back what the engine is actually doing with a character's model.
//
// This block is kept even though the invisibility it was built to chase is fixed,
// because it is the fastest way to recognise a render failure next time. A healthy
// clone reads: hide 0 (visible), a bound walkmesh triangle, shade alpha 1.000, and
// a draw gate of "drawing: 1 of 1 sub-meshes linked, shown=1".

namespace ffx
{

	// How many sub-meshes the instance has, and how many of them have no Phyre node.
	// Negative values mean the instance could not be read: -1 unreadable, -2 the
	// count is nonsense, -3 the mesh array is unreadable.
	//
	// A mesh with no node can never be linked into the render graph, because the link
	// is a store through that node pointer. The visibility predicate also treats a
	// null node as "always visible", so the two readings differ and it is worth
	// counting separately rather than assuming. Pass null for outWithoutNode to skip
	// the walk.
	LONG SubMeshCount(Character* chr, LONG* outWithoutNode);

	// How many sub-meshes are actually linked into the Phyre render graph, and what
	// the instance's own shown byte says. This is the end-to-end test, downstream of
	// every FFX-side gate, so it answers "is the engine drawing this" with no
	// inference. Same negative codes as SubMeshCount.
	LONG LinkedSubMeshCount(Character* chr, LONG* outShownByte);

	// Which gate in the real draw path is rejecting this character, as readable text.
	// Returns a description of the first failure, or what it is drawing if it passes
	// everything. The buffer is static, so one call per log line.
	const char* FirstFailingDrawGate(Character* chr);

	// The bounds box the frustum test uses, the transform that positions it and the
	// link state, for one character. Log the player next to a clone, because every
	// one of these numbers is only meaningful by comparison.
	void LogInstanceGeometry(Character* chr, const char* label);

} // namespace ffx
