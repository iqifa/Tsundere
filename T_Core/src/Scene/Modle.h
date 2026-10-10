#pragma once
#ifndef MODEL_H
#define MODEL_H

#include"Mesh.h"
#include <string>
#include <vector>

struct ModelBone
{
    std::string name;
    glm::mat4 offset{ 1.0f };
};

class Model
{
public:
    std::vector<Mesh> meshes;
    std::vector<ModelBone> bones;

    Model() = default;
};
#endif
