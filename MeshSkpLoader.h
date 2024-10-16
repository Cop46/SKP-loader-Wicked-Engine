#pragma once
#include "WickedEngine.h"
#include "xatlas.h"
#include "sketchup.h"

using namespace wi::ecs;

std::vector<Entity> ImportSkpMesh(const std::string& fileName, wi::scene::Scene& scene, const std::string& objName = "");
std::string getStdStringFromSkpString(SUStringRef stringName);
XMFLOAT4X4 getWickedMatrixFromSkpTransformation(SUTransformation* transformation);
SUTransformation multiplyMatrix(SUTransformation transform1, SUTransformation transform2);
void processMeshFromSkp(SUEntitiesRef entities, SUTransformation transform, wi::scene::Scene& scene, wi::ecs::Entity rootEntity, std::vector<Entity>& listOfEntity);
void readSurfaceFromSkp(SUEntitiesRef ents, SUTransformation transform, wi::scene::Scene& scene, wi::ecs::Entity rootEntity, std::vector<Entity>& listOfEntity);
void initSkpMatrix(SUTransformation* transform);