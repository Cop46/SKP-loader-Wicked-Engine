#include "MeshSkpLoader.h"
#include "Utils.h"

std::map<std::string, int> matMap;
SUMaterialRef MatREFT;

std::vector<Entity> ImportSkpMesh(const std::string& fileName, wi::scene::Scene& scene, const std::string& objName)
{
	std::vector<Entity> listOfEntity = {};

    std::string directory = wi::helper::GetDirectoryFromPath(fileName);
    std::string name = "";

    if (!objName.empty())
    {
        name = objName;
    }
    else
    {
        name = wi::helper::GetFileNameFromPath(fileName);
    }

    //create the root entity
    Entity rootEntity = wi::ecs::INVALID_ENTITY;
    rootEntity = CreateEntity();
    scene.transforms.Create(rootEntity);
    scene.names.Create(rootEntity) = name;
    scene.materials.Create(rootEntity);
    listOfEntity.push_back(rootEntity);

	SUInitialize();
	//code for import here
    SUModelRef model = SU_INVALID;
    SUModelLoadStatus status;
    SUResult res = SUModelCreateFromFileWithStatus(&model, fileName.data(), &status);

    // Get the entity container of the model.
    SUEntitiesRef entities = SU_INVALID;
    SUModelGetEntities(model, &entities);

    size_t numMaterials;
    SUModelGetNumMaterials(model, &numMaterials);
    std::vector<SUMaterialRef> materialsList(numMaterials);
    SUModelGetMaterials(model, numMaterials, &materialsList[0], &numMaterials);

    wi::scene::MaterialComponent* defaultMat = scene.materials.GetComponent(rootEntity);
    if (defaultMat != nullptr)
    {
        defaultMat->SetDoubleSided(true);
    }

    matMap["DEFAULTORION"] = rootEntity;

    int inc = 0;
    for (int i = 0; i < numMaterials;i++)
    {
        SUStringRef matName = SU_INVALID;
        SUStringCreate(&matName);
        SUMaterialGetName(materialsList.at(i), &matName);
        std::string materialName = getStdStringFromSkpString(matName);
        SUStringRelease(&matName);

        wi::ecs::Entity matEntity = scene.Entity_CreateMaterial(materialName);
        wi::scene::MaterialComponent* matWicked = scene.materials.GetComponent(matEntity);
        matWicked->SetPreferUncompressedTexturesEnabled(true);

        SUTextureRef texture = SU_INVALID;
        SUResult result = SUMaterialGetTexture(materialsList.at(i), &texture);
        if (result == SU_ERROR_NONE)
        {
            std::string fileTextureToLoad = "temp/tex" + Utils::intToStr(inc) + ".png";
            SUTextureWriteOriginalToFile(texture, fileTextureToLoad.data());

            if (!wi::resourcemanager::Contains(fileTextureToLoad))
            {
                matWicked->textures[wi::scene::MaterialComponent::TEXTURESLOT::BASECOLORMAP].resource =
                wi::resourcemanager::Load(fileTextureToLoad, wi::resourcemanager::Flags::NONE);
            }

            matWicked->textures[wi::scene::MaterialComponent::TEXTURESLOT::BASECOLORMAP].name = fileTextureToLoad;

            inc++;
        }

        SUColor color = SU_INVALID;
        double alpha = 1.0;
        SUMaterialGetColor(materialsList.at(i), &color);
        SUMaterialGetOpacity(materialsList.at(i), &alpha);
        
        matWicked->baseColor.x = color.red / 255.0f;
        matWicked->baseColor.y = color.green / 255.0f;
        matWicked->baseColor.z = color.blue / 255.0f;
        matWicked->baseColor.w = (float)alpha;

        matWicked->SetMetalness(0.1f);
        matWicked->SetReflectance(0.0f);
        matWicked->SetRoughness(0.5f);

        if (alpha < 1.0)
        {
            matWicked->userBlendMode = wi::enums::BLENDMODE_ALPHA;
            matWicked->SetMetalness(0.9f);
            matWicked->SetReflectance(0.5f);
            matWicked->SetRoughness(0.05f);
        }
        else
        {
            matWicked->userBlendMode = wi::enums::BLENDMODE_OPAQUE;
        }

        //matWicked->SetOpacity((float)alpha);
        matWicked->SetSpecularColor(XMFLOAT4(1, 1, 1, 1));

        //To avoid wrong face with skp model force double sided...Not really optimized...
        matWicked->SetDoubleSided(true);

        matWicked->SetDirty();

        matMap[materialName] = matEntity;
    }

    SUTransformation transform = SU_INVALID;
    initSkpMatrix(&transform);
    SUMaterialRef materialGroup = SU_INVALID;
    SUMaterialRef materialInstance = SU_INVALID;
    SUMaterialRef materialLayer = SU_INVALID;

    readSurfaceFromSkp(entities, transform, scene, rootEntity, listOfEntity);

    //compute bbox
    wi::primitive::AABB drawingAabb;

    wi::unordered_set<wi::ecs::Entity> allEntities;
    scene.FindAllEntities(allEntities);

    for (auto itr = allEntities.begin(); itr != allEntities.end(); ++itr)
    {
        wi::ecs::Entity entityIt = *itr;

        wi::scene::HierarchyComponent* hier = scene.hierarchy.GetComponent(entityIt);
        if (hier != nullptr)
        {
            if (hier->parentID == rootEntity)
            {
                wi::scene::MeshComponent* mesh = scene.meshes.GetComponent(entityIt);
                if (mesh != nullptr)
                {
                    drawingAabb = wi::primitive::AABB::Merge(drawingAabb, mesh->aabb);
                }
            }
        }
    }

    //move scene to point but maybe you don't need that...
    wi::scene::TransformComponent* tr = scene.transforms.GetComponent(rootEntity);
    if (tr != nullptr)
    {
        XMFLOAT3 center = drawingAabb.getCenter();
        center.x *= -1.0f;
        center.y *= -1.0f;
        center.z *= -1.0f;

        tr->Translate(center);
    }

    // Must release the model or there will be memory leaks
    res = SUModelRelease(&model);

	SUTerminate();

	return listOfEntity;
}

std::string getStdStringFromSkpString(SUStringRef stringName)
{
    size_t length = 0;
    SUStringGetUTF8Length(stringName, &length);
    std::vector<char> buffer(length + 1);
    size_t out_length = 0;
    SUStringGetUTF8(stringName, length + 1, buffer.data(), &out_length);
    assert(out_length == length);
    return std::string(begin(buffer), end(buffer));
}


void processMeshFromSkp(SUEntitiesRef entities, SUTransformation transform, wi::scene::Scene& scene, wi::ecs::Entity rootEntity, std::vector<Entity>& listOfEntity)
{
    // Get all the faces from the entities object
    size_t faceCount = 0;
    SUResult res = SUEntitiesGetNumFaces(entities, &faceCount);
    if (res != SU_ERROR_NONE)
    {
        //error("Failed getting number of faces");
    }
        
    if (faceCount > 0)
    {
        std::vector<SUFaceRef> faces(faceCount);
        res = SUEntitiesGetFaces(entities, faceCount, &faces[0], &faceCount);

        if (res == SU_ERROR_NONE)
        {
            int trisIndex = 0;
            static int t = 0;
            Entity objectEntity = scene.Entity_CreateObject("SketchupObject" + Utils::intToStr(t));
            scene.Component_Attach(objectEntity, rootEntity);
            listOfEntity.push_back(objectEntity);

            Entity meshEntity = scene.Entity_CreateMesh("SketchupMesh" + Utils::intToStr(t) + "_mesh");
            scene.Component_Attach(meshEntity, rootEntity);
            t++;

            wi::scene::ObjectComponent& object = *scene.objects.GetComponent(objectEntity);
            object.meshID = meshEntity;

            std::map<std::string, std::vector<int>> listOfSubsets;

            for (size_t i = 0; i < faceCount; i++)
            {
                //getting faces informations
                SUMeshHelperRef skpMeshHelper = SU_INVALID;
                res = SUMeshHelperCreate(&skpMeshHelper, faces[i]);

                if (res != SU_ERROR_NONE)
                {
                    //error("Failed creating mesh helper");
                }
                else
                {
                    //getting number of vertices
                    size_t nbsVertices = 0;
                    res = SUMeshHelperGetNumVertices(skpMeshHelper, &nbsVertices);
                    if (res != SU_ERROR_NONE)
                    {
                        //error("Failed getting number of vertices");
                    }
                    else
                    {
                        //getting vertices informations
                        std::vector<SUPoint3D> vertices(nbsVertices);
                        res = SUMeshHelperGetVertices(skpMeshHelper, nbsVertices, &vertices[0], &nbsVertices);
                        if (res != SU_ERROR_NONE)
                        {
                            //error("Failed getting vertices");
                        }

                        //getting number of triangles
                        size_t nbsTriangles = 0;
                        res = SUMeshHelperGetNumTriangles(skpMeshHelper, &nbsTriangles);
                        if (res != SU_ERROR_NONE)
                        {
                            //error("Failed getting number of triangles");
                        }

                        std::vector<size_t> indices(nbsTriangles * 3);
                        size_t nbsIndices = 0;
                        res = SUMeshHelperGetVertexIndices(skpMeshHelper, 3 * nbsTriangles, &indices[0], &nbsIndices);
                        if (res != SU_ERROR_NONE)
                        {
                            //error("Failed getting vertex indices");
                        }

                        std::vector<SUVector3D> normals(nbsVertices);
                        size_t nbsNormals = 0;
                        res = SUMeshHelperGetNormals(skpMeshHelper, nbsVertices, &normals[0], &nbsNormals);
                        if (res != SU_ERROR_NONE)
                        {
                            //error("Failed getting normals");
                        }

                        //getting texture coordinates informations
                        std::vector<SUPoint3D> texCoords(nbsVertices);
                        size_t nbsTCoord = 0;

                        SUMaterialRef newMat = SU_INVALID;
                        
                        //get material and texture coord from face
                        res = SUFaceGetFrontMaterial(faces[i], &newMat);
                        SUMeshHelperGetFrontSTQCoords(skpMeshHelper, nbsVertices, &texCoords[0], &nbsTCoord);

                        //revert y axis of texture
                        for (int b = 0; b < texCoords.size(); b++)
                        {
                            texCoords.at(b).y *= -1.0f;
                        }

                        if (res != SU_ERROR_NONE)
                        {
                            res = SUFaceGetBackMaterial(faces[i], &newMat);
                            SUMeshHelperGetBackSTQCoords(skpMeshHelper, nbsVertices, &texCoords[0], &nbsTCoord);
                        }

                        SUStringRef matName = SU_INVALID;
                        SUStringCreate(&matName);

                        if (newMat.ptr != nullptr)
                        {
                            res = SUMaterialGetName(newMat, &matName);
                        }
                        else if (MatREFT.ptr != nullptr)
                        {
                            res = SUMaterialGetName(MatREFT, &matName);

                            //Dont understand here why need something like this...
                            for (int b = 0; b < texCoords.size(); b++)
                            {
                                texCoords.at(b).x = 0.0f;
                                texCoords.at(b).y = 0.0f;
                            }
                        }

                        std::string materialName = getStdStringFromSkpString(matName);
                        SUStringRelease(&matName);

                        if (res == SU_ERROR_NO_DATA)
                        {
                            materialName = "DEFAULTORION";
                        }

                        //Fill wicked mesh
                        for (size_t j = 0; j < nbsTriangles; j++)
                        {
                            for (size_t k = 0; k < 3; k++)
                            {
                                XMFLOAT4X4 wickedMatrix = getWickedMatrixFromSkpTransformation(&transform);

                                size_t  index = indices[3 * j + k];

                                XMFLOAT3 vertex;
                                vertex.x = (float)vertices[index].x;
                                vertex.y = (float)vertices[index].y;
                                vertex.z = (float)vertices[index].z;

                                
                                wi::scene::MeshComponent* mesh = scene.meshes.GetComponent(meshEntity);

                                if (mesh != nullptr)
                                {
                                    XMMATRIX get = XMMatrixMultiply(XMLoadFloat4x4(&wickedMatrix), XMMatrixRotationX(1.57f));
                                    XMStoreFloat4x4(&wickedMatrix, get);
                                    XMStoreFloat3(&vertex, XMVector3Transform(XMLoadFloat3(&vertex), XMLoadFloat4x4(&wickedMatrix)));
                                    mesh->vertex_positions.push_back(vertex);

                                    XMFLOAT3 norm(0, 0, 0);
                                    norm.x = (float)normals[index].x;
                                    norm.y = (float)normals[index].y;
                                    norm.z = (float)normals[index].z;

                                    XMStoreFloat3(&norm, XMVector3Transform(XMLoadFloat3(&norm), XMLoadFloat4x4(&wickedMatrix)));
                                    mesh->vertex_normals.push_back(norm);

                                    XMFLOAT2 texC;
                                    texC.x = float(texCoords[index].x / texCoords[index].z);
                                    texC.y = float(texCoords[index].y / texCoords[index].z);
                                 
                                    mesh->vertex_uvset_0.push_back(texC);
                                    mesh->vertex_uvset_1.push_back(texC);

                                    listOfSubsets[materialName].push_back(trisIndex);
                                    trisIndex++;
                                }
                            }
                        }
                    }
                }
                SUMeshHelperRelease(&skpMeshHelper);
            }

            wi::scene::MeshComponent* mesh = scene.meshes.GetComponent(meshEntity);
            if (mesh != nullptr)
            {
                for (const auto& pair : listOfSubsets)
                {
                    int indexc = 0;
                    for (int x = 0; x < pair.second.size(); x++)
                    {
                        mesh->indices.push_back(pair.second.at(x));
                        indexc++;
                    }
                    wi::scene::MeshComponent::MeshSubset& subset = mesh->subsets.emplace_back();
                    subset.indexCount = indexc;
                    subset.indexOffset = (uint32_t)mesh->indices.size() - indexc;

                    if (matMap[pair.first] != INVALID_ENTITY)
                    {
                        subset.materialID = matMap[pair.first];
                    }
                }

                //Compute normals
                mesh->ComputeNormals(wi::scene::MeshComponent::COMPUTE_NORMALS_SMOOTH_FAST);
                mesh->SetBVHEnabled(true);
                mesh->BuildBVH();
            }
        }
    }
}

void readSurfaceFromSkp(SUEntitiesRef ents, SUTransformation transform, wi::scene::Scene& scene, wi::ecs::Entity rootEntity, std::vector<Entity>& listOfEntity)
{
    size_t count = 0;

    SUEntitiesGetNumGroups(ents, &count);
    std::vector<SUGroupRef> groups(count);
    SUEntitiesGetGroups(ents, count, groups.data(), &count);

    for (int i = 0; i < count; i++)
    {
        SUMaterialRef actMat = MatREFT;

        SUEntitiesRef ents2 = SU_INVALID;
        SUGroupGetEntities(groups[i], &ents2);

        SUTransformation trans;
        SUGroupGetTransform(groups[i], &trans);

        SUDrawingElementRef drawRef = SUGroupToDrawingElement(groups[i]);
        if (drawRef.ptr != nullptr)
        {
            SUDrawingElementGetMaterial(drawRef, &MatREFT);
        }

        SULayerRef layer = SU_INVALID;
        SUDrawingElementGetLayer(drawRef, &layer);
        bool visible = true;
        SULayerGetVisibility(layer, &visible);

        if (visible)
        {
            readSurfaceFromSkp(ents2, multiplyMatrix(trans, transform), scene, rootEntity, listOfEntity);
        }

        MatREFT = actMat;
    }

    SUEntitiesGetNumInstances(ents, &count);
    std::vector<SUComponentInstanceRef> instances(count);
    SUEntitiesGetInstances(ents, count, instances.data(), &count);

    for (int i = 0; i < count; i++)
    {
        SUMaterialRef actMat = MatREFT;

        SUTransformation trans;
        SUComponentDefinitionRef def = SU_INVALID;
        SUEntitiesRef ents2 = SU_INVALID;

        SUComponentInstanceGetTransform(instances[i], &trans);
        SUComponentInstanceGetDefinition(instances[i], &def);
        SUComponentDefinitionGetEntities(def, &ents2);

        SUDrawingElementRef drawRef = SUComponentInstanceToDrawingElement(instances[i]);
        if (drawRef.ptr != nullptr)
        {
            SUDrawingElementGetMaterial(drawRef, &MatREFT);
        }

        SULayerRef layer = SU_INVALID;
        SUDrawingElementGetLayer(drawRef, &layer);
        bool visible=true;
        SULayerGetVisibility(layer, &visible);

        if (visible)
        {
            readSurfaceFromSkp(ents2, multiplyMatrix(trans, transform), scene, rootEntity, listOfEntity);
        }

        MatREFT = actMat;
    }

    processMeshFromSkp(ents, transform, scene, rootEntity, listOfEntity);

}

SUTransformation multiplyMatrix(SUTransformation transform1, SUTransformation transform2)
{
    SUTransformation returnTransform;
    int i, j, k;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            returnTransform.values[4 * i + j] = 0;
            for (k = 0; k < 4; k++)
            {
                returnTransform.values[4 * i + j] += transform1.values[4 * i + k] * transform2.values[4 * k + j];
            }
        }
    }
    return returnTransform;
}

void initSkpMatrix(SUTransformation* transform)
{
    int i, j;

    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            transform->values[4 * i + j] = 0.;
        }
        transform->values[5 * i] = -0.0254;
    }
    return;
}

XMFLOAT4X4 getWickedMatrixFromSkpTransformation(SUTransformation* transformation)
{
    return XMFLOAT4X4{
        (float)transformation->values[0],
        (float)transformation->values[1],
        (float)transformation->values[2],
        (float)transformation->values[3],
        (float)transformation->values[4],
        (float)transformation->values[5],
        (float)transformation->values[6],
        (float)transformation->values[7],
        (float)transformation->values[8],
        (float)transformation->values[9],
        (float)transformation->values[10],
        (float)transformation->values[11],
        (float)transformation->values[12], 
        (float)transformation->values[13],
        (float)transformation->values[14],
        (float)transformation->values[15]};
}