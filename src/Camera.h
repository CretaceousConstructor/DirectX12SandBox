#pragma once
#include "AnniMath.h"

namespace Anni {

class Camera {
public:
    Camera();

    void Get3DViewProjMatrices(glm::mat4* view, glm::mat4* proj, const float fov_in_degrees, const float screen_width, const float screen_height, const float znear = 0.1f, const float zfar = 100.f) const;
    void GetOrthoProjMatrices(glm::mat4* view, glm::mat4* proj, float width, float height) const;
    void Reset();
    void Set(const glm::vec4 eye, const glm::vec4 at, const glm::vec4 up);

    glm::vec4 eye;
    glm::vec4 at;
    glm::vec4 up;

    // void RotateYaw(float deg);
    // void RotatePitch(float deg);
};

}
