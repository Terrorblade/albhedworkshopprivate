#pragma once

namespace ffx
{

	// The active camera's yaw, in the same convention as m_moveDir. Returns false
	// when there is no usable camera, in which case the caller should fall back to
	// world-relative input rather than guessing.
	bool ActiveCameraYaw(float* outYaw);

} // namespace ffx
