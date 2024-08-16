#include "Camera.h"
namespace Anni {

Camera::Camera()
    : eye()
    , at()
    , up()
{
    Reset();
}

void Camera::Get3DViewProjMatrices(glm::mat4* view, glm::mat4* proj, const float fov_in_degrees, const float screen_width, const float screen_height, const float znear, const float zfar) const
{
    const float fovAngleY = fov_in_degrees * glm::pi<float>() / 180.0f;
    *view = glm::lookAtLH(glm::vec3(eye), glm::vec3(at), glm::vec3(up));
    // 一般在dx程序中，hlsl里面喜欢向量放在最左边，没办法只能转置一下
    *view = glm::transpose(*view);

    *proj = glm::perspectiveFovLH_ZO(fovAngleY, screen_width, screen_height , znear,zfar);
    // 一般在dx程序中，hlsl里面喜欢向量放在最左边，没办法只能转置一下
    *proj = glm::transpose(*proj);

}

void Camera::GetOrthoProjMatrices(glm::mat4* view, glm::mat4* proj, const float width, const float height) const
{
    *view = glm::lookAtLH(glm::vec3(eye), glm::vec3(at), glm::vec3(up));
    // 一般hlsl里面喜欢向量放在最左边，没办法只能转置一下
    *view = glm::transpose(*view);

    *proj = glm::orthoLH_ZO(-width / 2.f, width / 2, -height / 2, height / 2, 0.01f, 800.0f);
    // 一般hlsl里面喜欢向量放在最左边，没办法只能转置一下
    *proj = glm::transpose(*proj);
}

// void Camera::RotateYaw(float deg)
//{
//     XMMATRIX rotation = XMMatrixRotationAxis(mUp, deg);
//
//     mEye = XMVector3TransformCoord(mEye, rotation);
// }
//
// void Camera::RotatePitch(float deg)
//{
//     XMVECTOR right = XMVector3Normalize(XMVector3Cross(mEye, mUp));
//     XMMATRIX rotation = XMMatrixRotationAxis(right, deg);
//
//     mEye = XMVector3TransformCoord(mEye, rotation);
// }

void Camera::Reset()
{
    eye = glm::vec4(0.0f, 500.0f, 0.0f, 0.0f);
    at = glm::vec4(0.0f, -1.0f, 1.0f, 0.0f);
    up = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
}

void Camera::Set(const glm::vec4 eye_set, const glm::vec4 at_set, const glm::vec4 up_set)
{
    this->eye = eye_set;
    this->at = at_set;
    this->up = up_set;
}
}
