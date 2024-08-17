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

    *proj = glm::perspectiveFovLH_ZO(fovAngleY, screen_width, screen_height, znear, zfar);
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

void Camera::Get3DViewProjMatricesForPointLight(std::array<glm::mat4, 6>* p_proj_matrices, std::array<glm::mat4, 6>* p_view_matrices, const float screen_width, const float screen_height, const float znear, const float zfar, const float fov_in_degrees) const
{
    auto& proj_matrices = *p_proj_matrices;
    auto& view_matrices = *p_view_matrices;

    const float fovAngleY = glm::radians(fov_in_degrees);

    for (auto& light_proj_matrix : proj_matrices) {
        light_proj_matrix = glm::perspectiveFovLH_ZO(fovAngleY, screen_width, screen_height, znear, zfar);
        // 一般在dx程序中，hlsl里面喜欢向量放在最左边，没办法只能转置一下
        light_proj_matrix = glm::transpose(light_proj_matrix);
    }

    view_matrices[0] = glm::lookAtLH(glm::vec3(eye), glm::vec3(eye) + glm::vec3(1.f, 0., 0.f), glm::vec3(0.f, 1.f, 0.f));
    view_matrices[1] = glm::lookAtLH(glm::vec3(eye), glm::vec3(eye) + glm::vec3(-1.f, 0., 0.f), glm::vec3(0.f, 1.f, 0.f));

    view_matrices[2] = glm::lookAtLH(glm::vec3(eye), glm::vec3(eye) + glm::vec3(0.f, 1., 0.f), glm::vec3(0.f, 0.f, -1.f));
    view_matrices[3] = glm::lookAtLH(glm::vec3(eye), glm::vec3(eye) + glm::vec3(0.f, -1., 0.f), glm::vec3(0.f, 0.f, 1.f));

    view_matrices[4] = glm::lookAtLH(glm::vec3(eye), glm::vec3(eye) + glm::vec3(0.f, 0., 1.f), glm::vec3(0.f, 1.f, 0.f));
    view_matrices[5] = glm::lookAtLH(glm::vec3(eye), glm::vec3(eye) + glm::vec3(0.f, 0., -1.f), glm::vec3(0.f, 1.f, 0.f));

	for (auto& light_view_matrix : view_matrices )
	{
        // 一般在dx程序中，hlsl里面喜欢向量放在最左边，没办法只能转置一下
        light_view_matrix = glm::transpose(light_view_matrix);
	}
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
