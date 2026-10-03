#include "ffx/RenderProbe.h"

#include <stdio.h>

#include "workshop/HostModule.h"
#include "workshop/Log.h"
#include "ffx/HideFlags.h"
#include "ffx/Character.h"
#include "ffx/Layout.h"

namespace ffx
{

	using workshop::Log;
	using workshop::Readable;
	namespace
	{

		// A sub-mesh count above this is not a count, it is garbage, so do not walk it.
		const DWORD SaneMeshLimit = 256;

		// Resolves the instance record and checks it is readable. Returns null if not.
		BYTE* ReadableInstance(Character* chr)
		{
			if (!chr)
				return NULL;
			BYTE* instance = (BYTE*)(UINT_PTR)DwordAt(chr, Chr::Instance);
			return Readable(instance, 0x20) ? instance : NULL;
		}

	} // namespace

	LONG SubMeshCount(Character* chr, LONG* outWithoutNode)
	{
		if (outWithoutNode)
			*outWithoutNode = 0;
		BYTE* instance = ReadableInstance(chr);
		if (!instance)
			return -1;

		DWORD count = *(volatile DWORD*)(instance + Instance::SubMeshCount) & 0x7FFFFFFFu;
		if (count > SaneMeshLimit)
			return -2;

		BYTE** meshes = *(BYTE***)(instance + Instance::MeshArray);
		if (count && !Readable(meshes, 4 * count))
			return -3;

		for (DWORD i = 0; i < count && outWithoutNode; ++i)
		{
			BYTE* mesh = meshes[i];
			if (!Readable(mesh, Mesh::Node + 4))
				continue;
			if (*(volatile DWORD*)(mesh + Mesh::Node) == 0)
				++*outWithoutNode;
		}
		return (LONG)count;
	}

	LONG LinkedSubMeshCount(Character* chr, LONG* outShownByte)
	{
		if (outShownByte)
			*outShownByte = -1;
		BYTE* instance = ReadableInstance(chr);
		if (!instance)
			return -1;

		if (outShownByte)
			*outShownByte = *(volatile BYTE*)(instance + Instance::ShownByte);

		DWORD count = *(volatile DWORD*)(instance + Instance::SubMeshCount) & 0x7FFFFFFFu;
		if (count > SaneMeshLimit)
			return -2;

		BYTE** meshes = *(BYTE***)(instance + Instance::MeshArray);
		if (count && !Readable(meshes, 4 * count))
			return -3;

		LONG linked = 0;
		for (DWORD i = 0; i < count; ++i)
		{
			BYTE* mesh = meshes[i];
			if (!Readable(mesh, Mesh::Node + 4))
				continue;
			BYTE* node = *(BYTE**)(mesh + Mesh::Node);
			if (!Readable(node, Node::MeshBackPointer + 4))
				continue;
			if (*(BYTE**)(node + Node::MeshBackPointer) == mesh)
				++linked;
		}
		return linked;
	}

	// A CORRECTION WORTH KEEPING. An earlier version of this read the gate chain in
	// FFX_Ch_UpdateRenderAll, which looked right and was wrong: that function has a
	// single caller, FFX_Ch_UpdateRenderForMagic573 0x826950, which only calls it when
	// MagicFile__getMagicId() == 573. It is a bone-matrix refresh for one magic effect
	// and it does not run in normal play. It merely happens to carry a gate chain that
	// resembles the real one.
	//
	// The real per-frame draw submission is FFX_Ch_StepAll loop C, 0x82F224 to
	// 0x82F82C, and its chain is:
	//
	//   0082F224  m_inUse == 0                      -> skip
	//   0082F242  m_flags2 & 0x20                   -> skip
	//   0082F256  FFX_Ch_UpdateCameLenAndZClip != 0 -> hide (this sets m_hideFlags)
	//   0082F4C3  m_animShadeA.m_cur == 0.0         -> hide
	//   0082F54D  m_hideFlags != 0 ? SetHidden(instance, 0) : SetHidden(instance, 1)
	//
	// and FFX_Ch_UpdateCameLenAndZClip itself returns non-zero when a hide bit is
	// already set, when the instance has no visible renderable (setting Disp), or when
	// m_cameLen > m_clipZ (setting Z).
	const char* FirstFailingDrawGate(Character* chr)
	{
		static char text[128];
		if (!chr)
			return "no character";

		if (ByteAt(chr, Chr::InUse) == 0)
			return "m_inUse is 0";
		if (DwordAt(chr, Chr::Flags2) & 0x20u)
			return "m_flags2 bit 0x20 set, the model packet was purged";
		if (DwordAt(chr, Chr::Instance) == 0)
			return "m_instance is null, no model attached";
		if ((DwordAt(chr, Chr::Flags1) & 0x800u) == 0)
			return "m_flags1 bit 0x800 clear, so the z-clip check is skipped entirely";
		if (FloatAt(chr, Chr::ShadeAlpha) == 0.0f)
			return "m_animShadeA.m_cur is 0.0, fully transparent";

		BYTE hideFlags = ByteAt(chr, Chr::HideFlags);
		if (hideFlags != 0)
		{
			if (hideFlags & Hide::PastClipZ)
				_snprintf_s(text, sizeof(text), _TRUNCATE,
				    "hide 0x%02X (%s) cameLen %.0f vs clipZ %.0f",
				    hideFlags, HideFlagNames(hideFlags),
				    (double)FloatAt(chr, Chr::CameLength),
				    (double)FloatAt(chr, Chr::ClipZ));
			else
				_snprintf_s(text, sizeof(text), _TRUNCATE, "hide 0x%02X (%s)",
				    hideFlags, HideFlagNames(hideFlags));
			return text;
		}

		LONG shownByte = -1;
		LONG linked = LinkedSubMeshCount(chr, &shownByte);
		LONG total = SubMeshCount(chr, NULL);
		if (linked <= 0)
		{
			_snprintf_s(text, sizeof(text), _TRUNCATE,
			    "passes every FFX gate, but %ld of %ld sub-meshes are linked "
			    "(instance shown byte = %ld), so the problem is downstream of FFX",
			    linked, total, shownByte);
			return text;
		}
		_snprintf_s(text, sizeof(text), _TRUNCATE,
		    "drawing: %ld of %ld sub-meshes linked, shown=%ld", linked, total, shownByte);
		return text;
	}

	// WHY THIS IS THE DECISIVE PROBE when a model is missing rather than hidden.
	// Static analysis gets this far and no further. The chain is
	//
	//   FFX_Ch_StepAll loop 1   (gated on m_hideFlags == 0)
	//     -> sub_63C3E0(instance, worldMatrix)
	//       -> FFX_Gfx_SetMeshTransform: stores the matrix into mesh+0x04,
	//          sets bounds+0x0C
	//   FFX_Ch_StepAll loop 3
	//     -> FFX_Ch_UpdateCameLenAndZClip
	//       -> FFX_Chr_InstanceAnyMeshVisible -> FFX_Gfx_IsMeshInstanceVisible
	//          -> FFX_Gfx_TestBoundsAgainstFrustum
	//            -> FFX_Gfx_TestObbCornersAgainstFrustum: 8 OBB corners from
	//               bounds+0x00 and bounds+0x10, transformed by frustum * bounds+0x0C
	//
	// and the measurement that matters is whether this character's box is where the
	// character is. Three outcomes, each pointing somewhere different:
	//
	//   extents all zero          the template bounds were never copied in
	//   bounds+0x0C != mesh+0x04  the transform link is broken
	//   transform at the origin   loop 1 never pushed the matrix for this character,
	//                             which is what being hidden since frame 0 does
	void LogInstanceGeometry(Character* chr, const char* label)
	{
		if (!chr)
		{
			Log("  geom %-8s <null character>", label);
			return;
		}

		BYTE* instance = ReadableInstance(chr);
		if (!instance)
		{
			Log("  geom %-8s instance unreadable", label);
			return;
		}

		DWORD count = *(volatile DWORD*)(instance + Instance::SubMeshCount) & 0x7FFFFFFFu;
		Log("  geom %-8s instance=0x%08X meshes=%lu shown=%u passId=%u", label,
		    (unsigned)(UINT_PTR)instance, (unsigned long)count,
		    *(volatile BYTE*)(instance + Instance::ShownByte),
		    *(volatile BYTE*)(instance + Instance::PassId));

		// What loop 1 should have pushed, for comparison with what the transform holds.
		const float* rootJoint =
		    (const float*)FieldAt(chr, Chr::RootJoint8 + Transform::TranslationColumn);
		if (Readable(rootJoint, 12))
			Log("  geom %-8s m_rootJoint[8] translation = (%.2f %.2f %.2f)",
			    label, (double)rootJoint[0], (double)rootJoint[1], (double)rootJoint[2]);

		if (count > 32)
		{
			Log("  geom %-8s mesh count looks wrong, not walking it", label);
			return;
		}
		BYTE** meshes = *(BYTE***)(instance + Instance::MeshArray);
		if (count && !Readable(meshes, 4 * count))
		{
			Log("  geom %-8s mesh array unreadable", label);
			return;
		}

		for (DWORD i = 0; i < count; ++i)
		{
			BYTE* mesh = meshes[i];
			if (!Readable(mesh, 0x20))
			{
				Log("    [%lu] mesh unreadable", (unsigned long)i);
				continue;
			}
			BYTE* transform = *(BYTE**)(mesh + Mesh::Transform);
			BYTE* bounds = *(BYTE**)(mesh + Mesh::Node);

			if (!Readable(bounds, 0x20))
			{
				Log("    [%lu] mesh=0x%08X transform=0x%08X bounds=0x%08X  "
				    "NO BOUNDS OBJECT, so it can never be linked and never drawn",
				    (unsigned long)i, (unsigned)(UINT_PTR)mesh,
				    (unsigned)(UINT_PTR)transform, (unsigned)(UINT_PTR)bounds);
				continue;
			}

			const float* corner = (const float*)(bounds + Bounds::Corner);
			const float* extents = (const float*)(bounds + Bounds::Extents);
			BYTE* boundsTransform = *(BYTE**)(bounds + Bounds::Transform);
			BYTE* backPointer = *(BYTE**)(bounds + Node::MeshBackPointer);

			Log("    [%lu] mesh=0x%08X bounds=0x%08X corner=(%.2f %.2f %.2f) "
			    "extents=(%.2f %.2f %.2f)%s",
			    (unsigned long)i, (unsigned)(UINT_PTR)mesh, (unsigned)(UINT_PTR)bounds,
			    (double)corner[0], (double)corner[1], (double)corner[2],
			    (double)extents[0], (double)extents[1], (double)extents[2],
			    (extents[0] == 0.0f && extents[1] == 0.0f && extents[2] == 0.0f)
			        ? "   <-- EXTENTS ARE ZERO, the template box was never copied in"
			        : "");

			Log("         bounds+0x%02X=0x%08X %s mesh+0x%02X=0x%08X    linked=%s",
			    Bounds::Transform, (unsigned)(UINT_PTR)boundsTransform,
			    boundsTransform == transform ? "==" : "!=",
			    Mesh::Transform, (unsigned)(UINT_PTR)transform,
			    backPointer == mesh ? "yes" : (backPointer ? "no, points elsewhere" : "no, null"));

			if (Readable(transform, Transform::TranslationColumn + 12))
			{
				const float* t = (const float*)(transform + Transform::TranslationColumn);
				Log("         transform translation = (%.2f %.2f %.2f)%s",
				    (double)t[0], (double)t[1], (double)t[2],
				    (t[0] == 0.0f && t[1] == 0.0f && t[2] == 0.0f)
				        ? "   <-- AT THE ORIGIN, loop 1 never pushed a matrix"
				        : "");
			}
		}
	}

} // namespace ffx
