#pragma once

#include "Math.hpp"


namespace leopph {
struct Frustum {
	Vector3 rightTopNear;
	Vector3 leftTopNear;
	Vector3 leftBottomNear;
	Vector3 rightBottomNear;
	Vector3 rightTopFar;
	Vector3 leftTopFar;
	Vector3 leftBottomFar;
	Vector3 rightBottomFar;
};


struct BoundingSphere {
	Vector3 center;
	float radius;

	[[nodiscard]] bool IsInFrustum(Frustum const& frustum, Matrix4 const& modelViewMat) const noexcept;
};


struct AABB {
	Vector3 min;
	Vector3 max;

	[[nodiscard]] bool IsInFrustum(Frustum const& frustum, Matrix4 const& modelViewMat) const noexcept;
};
}
