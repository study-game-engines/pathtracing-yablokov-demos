#include "test_scene.h"

#include <cstring>

#include "test_common.h"

#include "../RendererFactory.h"

#include "thread_pool.h"
#include "utils.h"

LogErr g_log_err;

void load_needed_textures(Ray::SceneBase &scene, Ray::shading_node_desc_t &mat_desc, const char *textures[]) {
    if (!textures) {
        return;
    }

    if (mat_desc.base_texture != Ray::InvalidTextureHandle && textures[0]) {
        int img_w, img_h;
        auto img_data = LoadTGA(textures[0], img_w, img_h);
        require(!img_data.empty());

        // drop alpha channel
        for (int i = 0; i < img_w * img_h; ++i) {
            img_data[3 * i + 0] = img_data[4 * i + 0];
            img_data[3 * i + 1] = img_data[4 * i + 1];
            img_data[3 * i + 2] = img_data[4 * i + 2];
        }

        Ray::tex_desc_t tex_desc;
        tex_desc.format = Ray::eTextureFormat::RGBA8888;
        tex_desc.data = img_data.data();
        tex_desc.w = img_w;
        tex_desc.h = img_h;
        tex_desc.generate_mipmaps = true;
        tex_desc.is_srgb = true;

        mat_desc.base_texture = scene.AddTexture(tex_desc);
    }
}

void load_needed_textures(Ray::SceneBase &scene, Ray::principled_mat_desc_t &mat_desc, const char *textures[]) {
    if (!textures) {
        return;
    }

    if (mat_desc.base_texture != Ray::InvalidTextureHandle && textures[mat_desc.base_texture._index]) {
        int img_w, img_h;
        auto img_data = LoadTGA(textures[mat_desc.base_texture._index], img_w, img_h);
        require(!img_data.empty());

        // drop alpha channel
        for (int i = 0; i < img_w * img_h; ++i) {
            img_data[3 * i + 0] = img_data[4 * i + 0];
            img_data[3 * i + 1] = img_data[4 * i + 1];
            img_data[3 * i + 2] = img_data[4 * i + 2];
        }

        Ray::tex_desc_t tex_desc;
        tex_desc.format = Ray::eTextureFormat::RGB888;
        tex_desc.data = img_data.data();
        tex_desc.w = img_w;
        tex_desc.h = img_h;
        tex_desc.generate_mipmaps = true;
        tex_desc.is_srgb = true;

        mat_desc.base_texture = scene.AddTexture(tex_desc);
    }

    if (mat_desc.normal_map != Ray::InvalidTextureHandle && textures[mat_desc.normal_map._index]) {
        int img_w, img_h;
        auto img_data = LoadTGA(textures[mat_desc.normal_map._index], img_w, img_h);
        require(!img_data.empty());

        // drop alpha channel
        for (int i = 0; i < img_w * img_h; ++i) {
            img_data[3 * i + 0] = img_data[4 * i + 0];
            img_data[3 * i + 1] = img_data[4 * i + 1];
            img_data[3 * i + 2] = img_data[4 * i + 2];
        }

        Ray::tex_desc_t tex_desc;
        tex_desc.format = Ray::eTextureFormat::RGB888;
        tex_desc.data = img_data.data();
        tex_desc.w = img_w;
        tex_desc.h = img_h;
        tex_desc.is_normalmap = true;
        tex_desc.generate_mipmaps = false;
        tex_desc.is_srgb = false;

        mat_desc.normal_map = scene.AddTexture(tex_desc);
    }

    if (mat_desc.roughness_texture != Ray::InvalidTextureHandle && textures[mat_desc.roughness_texture._index]) {
        int img_w, img_h;
        auto img_data = LoadTGA(textures[mat_desc.roughness_texture._index], img_w, img_h);
        require(!img_data.empty());

        // use only red channel
        for (int i = 0; i < img_w * img_h; ++i) {
            img_data[i] = img_data[4 * i + 0];
        }

        Ray::tex_desc_t tex_desc;
        tex_desc.format = Ray::eTextureFormat::R8;
        tex_desc.data = img_data.data();
        tex_desc.w = img_w;
        tex_desc.h = img_h;
        tex_desc.generate_mipmaps = true;
        tex_desc.is_srgb = false;

        mat_desc.roughness_texture = scene.AddTexture(tex_desc);
    }

    if (mat_desc.metallic_texture != Ray::InvalidTextureHandle && textures[mat_desc.metallic_texture._index]) {
        int img_w, img_h;
        auto img_data = LoadTGA(textures[mat_desc.metallic_texture._index], img_w, img_h);
        require(!img_data.empty());

        // use only red channel
        for (int i = 0; i < img_w * img_h; ++i) {
            img_data[i] = img_data[4 * i + 0];
        }

        Ray::tex_desc_t tex_desc;
        tex_desc.format = Ray::eTextureFormat::R8;
        tex_desc.data = img_data.data();
        tex_desc.w = img_w;
        tex_desc.h = img_h;
        tex_desc.generate_mipmaps = true;
        tex_desc.is_srgb = false;

        mat_desc.metallic_texture = scene.AddTexture(tex_desc);
    }

    if (mat_desc.alpha_texture != Ray::InvalidTextureHandle && textures[mat_desc.alpha_texture._index]) {
        int img_w, img_h;
        auto img_data = LoadTGA(textures[mat_desc.alpha_texture._index], img_w, img_h);
        require(!img_data.empty());

        // use only red channel
        for (int i = 0; i < img_w * img_h; ++i) {
            img_data[i] = img_data[4 * i + 0];
        }

        Ray::tex_desc_t tex_desc;
        tex_desc.format = Ray::eTextureFormat::R8;
        tex_desc.data = img_data.data();
        tex_desc.w = img_w;
        tex_desc.h = img_h;
        tex_desc.generate_mipmaps = false;
        tex_desc.is_srgb = false;

        mat_desc.alpha_texture = scene.AddTexture(tex_desc);
    }
}

template <typename MatDesc>
void setup_test_scene(Ray::SceneBase &scene, const bool output_sh, const bool output_base_color,
                      const bool output_depth_normals, const MatDesc &main_mat_desc, const char *textures[],
                      const eTestScene test_scene) {
    { // setup camera
        static const float view_origin_standard[] = {0.16149f, 0.294997f, 0.332965f};
        static const float view_dir_standard[] = {-0.364128768f, -0.555621922f, -0.747458696f};
        static const float view_origin_refr[] = {-0.074711f, 0.099348f, -0.049506f};
        static const float view_dir_refr[] = {0.725718915f, 0.492017448f, 0.480885535f};
        static const float view_up[] = {0.0f, 1.0f, 0.0f};

        Ray::camera_desc_t cam_desc;
        cam_desc.type = Ray::Persp;
        cam_desc.filter = Ray::Box;
        cam_desc.dtype = Ray::SRGB;
        if (test_scene == eTestScene::Refraction_Plane) {
            memcpy(&cam_desc.origin[0], &view_origin_refr[0], 3 * sizeof(float));
            memcpy(&cam_desc.fwd[0], &view_dir_refr[0], 3 * sizeof(float));
            cam_desc.fov = 45.1806f;
        } else {
            memcpy(&cam_desc.origin[0], &view_origin_standard[0], 3 * sizeof(float));
            memcpy(&cam_desc.fwd[0], &view_dir_standard[0], 3 * sizeof(float));
            cam_desc.fov = 18.1806f;
        }
        memcpy(&cam_desc.up[0], &view_up[0], 3 * sizeof(float));
        cam_desc.clamp = true;
        cam_desc.output_sh = output_sh;
        cam_desc.output_base_color = output_base_color;
        cam_desc.output_depth_normals = output_depth_normals;

        if (test_scene == eTestScene::Standard_DOF0) {
            cam_desc.sensor_height = 0.018f;
            cam_desc.focus_distance = 0.1f;
            cam_desc.fstop = 0.1f;
            cam_desc.lens_blades = 6;
            cam_desc.lens_rotation = 30.0f * 3.141592653589f / 180.0f;
            cam_desc.lens_ratio = 2.0f;
        } else if (test_scene == eTestScene::Standard_DOF1) {
            cam_desc.sensor_height = 0.018f;
            cam_desc.focus_distance = 0.4f;
            cam_desc.fstop = 0.1f;
            cam_desc.lens_blades = 0;
            cam_desc.lens_rotation = 30.0f * 3.141592653589f / 180.0f;
            cam_desc.lens_ratio = 2.0f;
        } else if (test_scene == eTestScene::Standard_GlassBall0 || test_scene == eTestScene::Standard_GlassBall1) {
            cam_desc.max_diff_depth = 8;
            cam_desc.max_spec_depth = 8;
            cam_desc.max_refr_depth = 8;
            cam_desc.max_total_depth = 9;
        }

        const Ray::CameraHandle cam = scene.AddCamera(cam_desc);
        scene.set_current_cam(cam);
    }

    MatDesc main_mat_desc_copy = main_mat_desc;
    load_needed_textures(scene, main_mat_desc_copy, textures);
    const Ray::MaterialHandle main_mat = scene.AddMaterial(main_mat_desc_copy);

    Ray::MaterialHandle floor_mat;
    {
        Ray::principled_mat_desc_t floor_mat_desc;
        floor_mat_desc.base_color[0] = 0.75f;
        floor_mat_desc.base_color[1] = 0.75f;
        floor_mat_desc.base_color[2] = 0.75f;
        floor_mat_desc.roughness = 0.0f;
        floor_mat_desc.specular = 0.0f;
        floor_mat = scene.AddMaterial(floor_mat_desc);
    }

    Ray::MaterialHandle walls_mat;
    {
        Ray::principled_mat_desc_t walls_mat_desc;
        walls_mat_desc.base_color[0] = 0.5f;
        walls_mat_desc.base_color[1] = 0.5f;
        walls_mat_desc.base_color[2] = 0.5f;
        walls_mat_desc.roughness = 0.0f;
        walls_mat_desc.specular = 0.0f;
        walls_mat = scene.AddMaterial(walls_mat_desc);
    }

    Ray::MaterialHandle white_mat;
    {
        Ray::principled_mat_desc_t white_mat_desc;
        white_mat_desc.base_color[0] = 0.64f;
        white_mat_desc.base_color[1] = 0.64f;
        white_mat_desc.base_color[2] = 0.64f;
        white_mat_desc.roughness = 0.0f;
        white_mat_desc.specular = 0.0f;
        white_mat = scene.AddMaterial(white_mat_desc);
    }

    Ray::MaterialHandle light_grey_mat;
    {
        Ray::principled_mat_desc_t light_grey_mat_desc;
        light_grey_mat_desc.base_color[0] = 0.32f;
        light_grey_mat_desc.base_color[1] = 0.32f;
        light_grey_mat_desc.base_color[2] = 0.32f;
        light_grey_mat_desc.roughness = 0.0f;
        light_grey_mat_desc.specular = 0.0f;
        light_grey_mat = scene.AddMaterial(light_grey_mat_desc);
    }

    Ray::MaterialHandle mid_grey_mat;
    {
        Ray::principled_mat_desc_t mid_grey_mat_desc;
        mid_grey_mat_desc.base_color[0] = 0.16f;
        mid_grey_mat_desc.base_color[1] = 0.16f;
        mid_grey_mat_desc.base_color[2] = 0.16f;
        mid_grey_mat_desc.roughness = 0.0f;
        mid_grey_mat_desc.specular = 0.0f;
        mid_grey_mat = scene.AddMaterial(mid_grey_mat_desc);
    }

    Ray::MaterialHandle dark_grey_mat;
    {
        Ray::principled_mat_desc_t dark_grey_mat_desc;
        dark_grey_mat_desc.base_color[0] = 0.08f;
        dark_grey_mat_desc.base_color[1] = 0.08f;
        dark_grey_mat_desc.base_color[2] = 0.08f;
        dark_grey_mat_desc.roughness = 0.0f;
        dark_grey_mat_desc.specular = 0.0f;
        dark_grey_mat = scene.AddMaterial(dark_grey_mat_desc);
    }

    Ray::MaterialHandle square_light_mat;
    {
        Ray::shading_node_desc_t square_light_mat_desc;
        square_light_mat_desc.type = Ray::EmissiveNode;
        square_light_mat_desc.strength = 20.3718f;
        square_light_mat_desc.multiple_importance = true;
        square_light_mat_desc.base_color[0] = 1.0f;
        square_light_mat_desc.base_color[1] = 1.0f;
        square_light_mat_desc.base_color[2] = 1.0f;
        square_light_mat = scene.AddMaterial(square_light_mat_desc);
    }

    Ray::MaterialHandle disc_light_mat;
    {
        Ray::shading_node_desc_t disc_light_mat_desc;
        disc_light_mat_desc.type = Ray::EmissiveNode;
        disc_light_mat_desc.strength = 81.4873f;
        disc_light_mat_desc.multiple_importance = true;
        disc_light_mat_desc.base_color[0] = 1.0f;
        disc_light_mat_desc.base_color[1] = 1.0f;
        disc_light_mat_desc.base_color[2] = 1.0f;
        disc_light_mat = scene.AddMaterial(disc_light_mat_desc);
    }

    Ray::MaterialHandle glassball_mat0;
    if (test_scene == eTestScene::Standard_GlassBall0) {
        Ray::shading_node_desc_t glassball_mat0_desc;
        glassball_mat0_desc.type = Ray::RefractiveNode;
        glassball_mat0_desc.base_color[0] = 1.0f;
        glassball_mat0_desc.base_color[1] = 1.0f;
        glassball_mat0_desc.base_color[2] = 1.0f;
        glassball_mat0_desc.roughness = 0.0f;
        glassball_mat0_desc.ior = 1.45f;
        glassball_mat0 = scene.AddMaterial(glassball_mat0_desc);
    } else {
        Ray::principled_mat_desc_t glassball_mat0_desc;
        glassball_mat0_desc.base_color[0] = 1.0f;
        glassball_mat0_desc.base_color[1] = 1.0f;
        glassball_mat0_desc.base_color[2] = 1.0f;
        glassball_mat0_desc.roughness = 0.0f;
        glassball_mat0_desc.ior = 1.45f;
        glassball_mat0_desc.transmission = 1.0f;
        glassball_mat0 = scene.AddMaterial(glassball_mat0_desc);
    }

    Ray::MaterialHandle glassball_mat1;
    if (test_scene == eTestScene::Standard_GlassBall0) {
        Ray::shading_node_desc_t glassball_mat1_desc;
        glassball_mat1_desc.type = Ray::RefractiveNode;
        glassball_mat1_desc.base_color[0] = 1.0f;
        glassball_mat1_desc.base_color[1] = 1.0f;
        glassball_mat1_desc.base_color[2] = 1.0f;
        glassball_mat1_desc.roughness = 0.0f;
        glassball_mat1_desc.ior = 1.0f;
        glassball_mat1 = scene.AddMaterial(glassball_mat1_desc);
    } else {
        Ray::principled_mat_desc_t glassball_mat1_desc;
        glassball_mat1_desc.base_color[0] = 1.0f;
        glassball_mat1_desc.base_color[1] = 1.0f;
        glassball_mat1_desc.base_color[2] = 1.0f;
        glassball_mat1_desc.roughness = 0.0f;
        glassball_mat1_desc.ior = 1.0f;
        glassball_mat1_desc.transmission = 1.0f;
        glassball_mat1 = scene.AddMaterial(glassball_mat1_desc);
    }

    Ray::MeshHandle base_mesh;
    {
        std::vector<float> base_attrs;
        std::vector<uint32_t> base_indices, base_groups;
        std::tie(base_attrs, base_indices, base_groups) = LoadBIN("test_data/meshes/mat_test/base.bin");

        Ray::mesh_desc_t base_mesh_desc;
        base_mesh_desc.prim_type = Ray::TriangleList;
        base_mesh_desc.layout = Ray::PxyzNxyzTuv;
        base_mesh_desc.vtx_attrs = &base_attrs[0];
        base_mesh_desc.vtx_attrs_count = uint32_t(base_attrs.size()) / 8;
        base_mesh_desc.vtx_indices = &base_indices[0];
        base_mesh_desc.vtx_indices_count = uint32_t(base_indices.size());
        base_mesh_desc.shapes.emplace_back(mid_grey_mat, mid_grey_mat, base_groups[0], base_groups[1]);
        base_mesh = scene.AddMesh(base_mesh_desc);
    }

    Ray::MeshHandle model_mesh;
    {
        std::vector<float> model_attrs;
        std::vector<uint32_t> model_indices, model_groups;
        if (test_scene == eTestScene::Refraction_Plane) {
            std::tie(model_attrs, model_indices, model_groups) = LoadBIN("test_data/meshes/mat_test/refr_plane.bin");
        } else {
            std::tie(model_attrs, model_indices, model_groups) = LoadBIN("test_data/meshes/mat_test/model.bin");
        }

        Ray::mesh_desc_t model_mesh_desc;
        model_mesh_desc.prim_type = Ray::TriangleList;
        model_mesh_desc.layout = Ray::PxyzNxyzTuv;
        model_mesh_desc.vtx_attrs = &model_attrs[0];
        model_mesh_desc.vtx_attrs_count = uint32_t(model_attrs.size()) / 8;
        model_mesh_desc.vtx_indices = &model_indices[0];
        model_mesh_desc.vtx_indices_count = uint32_t(model_indices.size());
        model_mesh_desc.shapes.emplace_back(main_mat, main_mat, model_groups[0], model_groups[1]);
        model_mesh = scene.AddMesh(model_mesh_desc);
    }

    Ray::MeshHandle core_mesh;
    {
        std::vector<float> core_attrs;
        std::vector<uint32_t> core_indices, core_groups;
        std::tie(core_attrs, core_indices, core_groups) = LoadBIN("test_data/meshes/mat_test/core.bin");

        Ray::mesh_desc_t core_mesh_desc;
        core_mesh_desc.prim_type = Ray::TriangleList;
        core_mesh_desc.layout = Ray::PxyzNxyzTuv;
        core_mesh_desc.vtx_attrs = &core_attrs[0];
        core_mesh_desc.vtx_attrs_count = uint32_t(core_attrs.size()) / 8;
        core_mesh_desc.vtx_indices = &core_indices[0];
        core_mesh_desc.vtx_indices_count = uint32_t(core_indices.size());
        core_mesh_desc.shapes.emplace_back(mid_grey_mat, mid_grey_mat, core_groups[0], core_groups[1]);
        core_mesh = scene.AddMesh(core_mesh_desc);
    }

    Ray::MeshHandle subsurf_bar_mesh;
    {
        std::vector<float> subsurf_bar_attrs;
        std::vector<uint32_t> subsurf_bar_indices, subsurf_bar_groups;
        std::tie(subsurf_bar_attrs, subsurf_bar_indices, subsurf_bar_groups) =
            LoadBIN("test_data/meshes/mat_test/subsurf_bar.bin");

        Ray::mesh_desc_t subsurf_bar_mesh_desc;
        subsurf_bar_mesh_desc.prim_type = Ray::TriangleList;
        subsurf_bar_mesh_desc.layout = Ray::PxyzNxyzTuv;
        subsurf_bar_mesh_desc.vtx_attrs = &subsurf_bar_attrs[0];
        subsurf_bar_mesh_desc.vtx_attrs_count = uint32_t(subsurf_bar_attrs.size()) / 8;
        subsurf_bar_mesh_desc.vtx_indices = &subsurf_bar_indices[0];
        subsurf_bar_mesh_desc.vtx_indices_count = uint32_t(subsurf_bar_indices.size());
        subsurf_bar_mesh_desc.shapes.emplace_back(white_mat, white_mat, subsurf_bar_groups[0], subsurf_bar_groups[1]);
        subsurf_bar_mesh_desc.shapes.emplace_back(dark_grey_mat, dark_grey_mat, subsurf_bar_groups[2],
                                                  subsurf_bar_groups[3]);
        subsurf_bar_mesh = scene.AddMesh(subsurf_bar_mesh_desc);
    }

    Ray::MeshHandle text_mesh;
    {
        std::vector<float> text_attrs;
        std::vector<uint32_t> text_indices, text_groups;
        std::tie(text_attrs, text_indices, text_groups) = LoadBIN("test_data/meshes/mat_test/text.bin");

        Ray::mesh_desc_t text_mesh_desc;
        text_mesh_desc.prim_type = Ray::TriangleList;
        text_mesh_desc.layout = Ray::PxyzNxyzTuv;
        text_mesh_desc.vtx_attrs = &text_attrs[0];
        text_mesh_desc.vtx_attrs_count = uint32_t(text_attrs.size()) / 8;
        text_mesh_desc.vtx_indices = &text_indices[0];
        text_mesh_desc.vtx_indices_count = uint32_t(text_indices.size());
        text_mesh_desc.shapes.emplace_back(white_mat, white_mat, text_groups[0], text_groups[1]);
        text_mesh = scene.AddMesh(text_mesh_desc);
    }

    Ray::MeshHandle env_mesh;
    {
        std::vector<float> env_attrs;
        std::vector<uint32_t> env_indices, env_groups;
        if (test_scene == eTestScene::Standard_SunLight || test_scene == eTestScene::Standard_HDRLight) {
            std::tie(env_attrs, env_indices, env_groups) = LoadBIN("test_data/meshes/mat_test/env_floor.bin");
        } else {
            std::tie(env_attrs, env_indices, env_groups) = LoadBIN("test_data/meshes/mat_test/env.bin");
        }

        Ray::mesh_desc_t env_mesh_desc;
        env_mesh_desc.prim_type = Ray::TriangleList;
        env_mesh_desc.layout = Ray::PxyzNxyzTuv;
        env_mesh_desc.vtx_attrs = &env_attrs[0];
        env_mesh_desc.vtx_attrs_count = uint32_t(env_attrs.size()) / 8;
        env_mesh_desc.vtx_indices = &env_indices[0];
        env_mesh_desc.vtx_indices_count = uint32_t(env_indices.size());
        if (test_scene == eTestScene::Standard_SunLight || test_scene == eTestScene::Standard_HDRLight) {
            env_mesh_desc.shapes.emplace_back(floor_mat, floor_mat, env_groups[0], env_groups[1]);
            env_mesh_desc.shapes.emplace_back(dark_grey_mat, dark_grey_mat, env_groups[2], env_groups[3]);
            env_mesh_desc.shapes.emplace_back(mid_grey_mat, mid_grey_mat, env_groups[4], env_groups[5]);
        } else {
            env_mesh_desc.shapes.emplace_back(floor_mat, floor_mat, env_groups[0], env_groups[1]);
            env_mesh_desc.shapes.emplace_back(walls_mat, walls_mat, env_groups[2], env_groups[3]);
            env_mesh_desc.shapes.emplace_back(dark_grey_mat, dark_grey_mat, env_groups[4], env_groups[5]);
            env_mesh_desc.shapes.emplace_back(light_grey_mat, light_grey_mat, env_groups[6], env_groups[7]);
            env_mesh_desc.shapes.emplace_back(mid_grey_mat, mid_grey_mat, env_groups[8], env_groups[9]);
        }
        env_mesh = scene.AddMesh(env_mesh_desc);
    }

    Ray::MeshHandle square_light_mesh;
    {
        std::vector<float> square_light_attrs;
        std::vector<uint32_t> square_light_indices, square_light_groups;
        std::tie(square_light_attrs, square_light_indices, square_light_groups) =
            LoadBIN("test_data/meshes/mat_test/square_light.bin");

        Ray::mesh_desc_t square_light_mesh_desc;
        square_light_mesh_desc.prim_type = Ray::TriangleList;
        square_light_mesh_desc.layout = Ray::PxyzNxyzTuv;
        square_light_mesh_desc.vtx_attrs = &square_light_attrs[0];
        square_light_mesh_desc.vtx_attrs_count = uint32_t(square_light_attrs.size()) / 8;
        square_light_mesh_desc.vtx_indices = &square_light_indices[0];
        square_light_mesh_desc.vtx_indices_count = uint32_t(square_light_indices.size());
        square_light_mesh_desc.shapes.emplace_back(square_light_mat, square_light_mat, square_light_groups[0],
                                                   square_light_groups[1]);
        square_light_mesh_desc.shapes.emplace_back(dark_grey_mat, dark_grey_mat, square_light_groups[2],
                                                   square_light_groups[3]);
        square_light_mesh = scene.AddMesh(square_light_mesh_desc);
    }

    Ray::MeshHandle disc_light_mesh;
    {
        std::vector<float> disc_light_attrs;
        std::vector<uint32_t> disc_light_indices, disc_light_groups;
        std::tie(disc_light_attrs, disc_light_indices, disc_light_groups) =
            LoadBIN("test_data/meshes/mat_test/disc_light.bin");

        Ray::mesh_desc_t disc_light_mesh_desc;
        disc_light_mesh_desc.prim_type = Ray::TriangleList;
        disc_light_mesh_desc.layout = Ray::PxyzNxyzTuv;
        disc_light_mesh_desc.vtx_attrs = &disc_light_attrs[0];
        disc_light_mesh_desc.vtx_attrs_count = uint32_t(disc_light_attrs.size()) / 8;
        disc_light_mesh_desc.vtx_indices = &disc_light_indices[0];
        disc_light_mesh_desc.vtx_indices_count = uint32_t(disc_light_indices.size());
        disc_light_mesh_desc.shapes.emplace_back(disc_light_mat, disc_light_mat, disc_light_groups[0],
                                                 disc_light_groups[1]);
        disc_light_mesh_desc.shapes.emplace_back(dark_grey_mat, dark_grey_mat, disc_light_groups[2],
                                                 disc_light_groups[3]);
        disc_light_mesh = scene.AddMesh(disc_light_mesh_desc);
    }

    Ray::MeshHandle glassball_mesh;
    {
        std::vector<float> glassball_attrs;
        std::vector<uint32_t> glassball_indices, glassball_groups;
        std::tie(glassball_attrs, glassball_indices, glassball_groups) =
            LoadBIN("test_data/meshes/mat_test/glassball.bin");

        Ray::mesh_desc_t glassball_mesh_desc;
        glassball_mesh_desc.prim_type = Ray::TriangleList;
        glassball_mesh_desc.layout = Ray::PxyzNxyzTuv;
        glassball_mesh_desc.vtx_attrs = &glassball_attrs[0];
        glassball_mesh_desc.vtx_attrs_count = uint32_t(glassball_attrs.size()) / 8;
        glassball_mesh_desc.vtx_indices = &glassball_indices[0];
        glassball_mesh_desc.vtx_indices_count = uint32_t(glassball_indices.size());
        glassball_mesh_desc.shapes.emplace_back(glassball_mat0, glassball_mat0, glassball_groups[0],
                                                glassball_groups[1]);
        glassball_mesh_desc.shapes.emplace_back(glassball_mat1, glassball_mat1, glassball_groups[2],
                                                glassball_groups[3]);
        glassball_mesh = scene.AddMesh(glassball_mesh_desc);
    }

    static const float identity[16] = {1.0f, 0.0f, 0.0f, 0.0f, // NOLINT
                                       0.0f, 1.0f, 0.0f, 0.0f, // NOLINT
                                       0.0f, 0.0f, 1.0f, 0.0f, // NOLINT
                                       0.0f, 0.0f, 0.0f, 1.0f};

    static const float model_xform[16] = {0.707106769f,  0.0f,   0.707106769f, 0.0f, // NOLINT
                                          0.0f,          1.0f,   0.0f,         0.0f, // NOLINT
                                          -0.707106769f, 0.0f,   0.707106769f, 0.0f, // NOLINT
                                          0.0f,          0.062f, 0.0f,         1.0f};

    Ray::environment_desc_t env_desc;
    env_desc.env_col[0] = env_desc.env_col[1] = env_desc.env_col[2] = 0.0f;
    env_desc.back_col[0] = env_desc.back_col[1] = env_desc.back_col[2] = 0.0f;

    if (test_scene == eTestScene::Refraction_Plane) {
        scene.AddMeshInstance(model_mesh, identity);
    } else if (test_scene == eTestScene::Standard_GlassBall0 || test_scene == eTestScene::Standard_GlassBall1) {
        static const float glassball_xform[16] = {1.0f, 0.0f,  0.0f, 0.0f, // NOLINT
                                                  0.0f, 1.0f,  0.0f, 0.0f, // NOLINT
                                                  0.0f, 0.0f,  1.0f, 0.0f, // NOLINT
                                                  0.0f, 0.05f, 0.0f, 1.0f};

        scene.AddMeshInstance(glassball_mesh, glassball_xform);
    } else {
        scene.AddMeshInstance(model_mesh, model_xform);
        scene.AddMeshInstance(base_mesh, identity);
        scene.AddMeshInstance(core_mesh, identity);
        scene.AddMeshInstance(subsurf_bar_mesh, identity);
        scene.AddMeshInstance(text_mesh, identity);
    }
    scene.AddMeshInstance(env_mesh, identity);
    if (test_scene == eTestScene::Standard_MeshLights || test_scene == eTestScene::Refraction_Plane) {
        //
        // Use mesh lights
        //
        if (test_scene != eTestScene::Refraction_Plane) {
            scene.AddMeshInstance(square_light_mesh, identity);
        }
        scene.AddMeshInstance(disc_light_mesh, identity);
    } else if (test_scene == eTestScene::Standard || test_scene == eTestScene::Standard_SphereLight ||
               test_scene == eTestScene::Standard_SpotLight || test_scene == eTestScene::Standard_DOF0 ||
               test_scene == eTestScene::Standard_DOF1 || test_scene == eTestScene::Standard_GlassBall0 ||
               test_scene == eTestScene::Standard_GlassBall1) {
        //
        // Use explicit lights sources
        //
        if (test_scene == eTestScene::Standard || test_scene == eTestScene::Standard_DOF0 ||
            test_scene == eTestScene::Standard_DOF1 || test_scene == eTestScene::Standard_GlassBall0 ||
            test_scene == eTestScene::Standard_GlassBall1) {
            { // rect light
                static const float xform[16] = {-0.425036609f, 2.24262476e-06f, -0.905176163f, 0.00000000f,
                                                -0.876228273f, 0.250873595f,    0.411444396f,  0.00000000f,
                                                0.227085724f,  0.968019843f,    -0.106628500f, 0.00000000f,
                                                -0.436484009f, 0.187178999f,    0.204932004f,  1.00000000f};

                Ray::rect_light_desc_t new_light;

                new_light.color[0] = 20.3718f;
                new_light.color[1] = 20.3718f;
                new_light.color[2] = 20.3718f;

                new_light.width = 0.162f;
                new_light.height = 0.162f;

                new_light.visible = true;
                new_light.sky_portal = false;

                scene.AddLight(new_light, xform);
            }
            { // disk light
                static const float xform[16] = {0.813511789f,  -0.536388099f, -0.224691749f, 0.00000000f,
                                                0.538244009f,  0.548162937f,  0.640164733f,  0.00000000f,
                                                -0.220209062f, -0.641720533f, 0.734644651f,  0.00000000f,
                                                0.360500991f,  0.461762011f,  0.431780994f,  1.00000000f};

                Ray::disk_light_desc_t new_light;

                new_light.color[0] = 81.4873f;
                new_light.color[1] = 81.4873f;
                new_light.color[2] = 81.4873f;

                new_light.size_x = 0.1296f;
                new_light.size_y = 0.1296f;

                new_light.visible = true;
                new_light.sky_portal = false;

                scene.AddLight(new_light, xform);
            }
        } else if (test_scene == eTestScene::Standard_SphereLight) {
            { // sphere light
                Ray::sphere_light_desc_t new_light;

                new_light.color[0] = 7.95775f;
                new_light.color[1] = 7.95775f;
                new_light.color[2] = 7.95775f;

                new_light.position[0] = -0.436484f;
                new_light.position[1] = 0.187179f;
                new_light.position[2] = 0.204932f;

                new_light.radius = 0.05f;

                new_light.visible = true;

                scene.AddLight(new_light);
            }
            { // line light
                static const float xform[16] = {0.813511789f,  -0.536388099f, -0.224691749f, 0.00000000f,
                                                0.538244009f,  0.548162937f,  0.640164733f,  0.00000000f,
                                                -0.220209062f, -0.641720533f, 0.734644651f,  0.00000000f,
                                                0.0f,          0.461762f,     0.0f,          1.00000000f};

                Ray::line_light_desc_t new_light;

                new_light.color[0] = 80.0f;
                new_light.color[1] = 80.0f;
                new_light.color[2] = 80.0f;

                new_light.radius = 0.005f;
                new_light.height = 0.2592f;

                new_light.visible = true;
                new_light.sky_portal = false;

                scene.AddLight(new_light, xform);
            }
        } else if (test_scene == eTestScene::Standard_SpotLight) {
            { // spot light
                Ray::spot_light_desc_t new_light;

                new_light.color[0] = 10.1321182f;
                new_light.color[1] = 10.1321182f;
                new_light.color[2] = 10.1321182f;

                new_light.position[0] = -0.436484f;
                new_light.position[1] = 0.187179f;
                new_light.position[2] = 0.204932f;

                new_light.direction[0] = 0.699538708f;
                new_light.direction[1] = -0.130918920f;
                new_light.direction[2] = -0.702499688f;

                new_light.radius = 0.05f;
                new_light.spot_size = 45.0f;
                new_light.spot_blend = 0.15f;

                new_light.visible = true;

                scene.AddLight(new_light);
            }
        }
    } else if (test_scene == eTestScene::Standard_SunLight) {
        Ray::directional_light_desc_t sun_desc;

        sun_desc.direction[0] = 0.541675210f;
        sun_desc.direction[1] = -0.541675210f;
        sun_desc.direction[2] = -0.642787635f;

        sun_desc.color[0] = sun_desc.color[1] = sun_desc.color[2] = 1.0f;
        sun_desc.angle = 10.0f;

        scene.AddLight(sun_desc);
    } else if (test_scene == eTestScene::Standard_HDRLight) {
        int img_w, img_h;
        auto img_data = LoadHDR("test_data/textures/studio_small_03_2k.hdr", img_w, img_h);
        require(!img_data.empty());

        Ray::tex_desc_t tex_desc;
        tex_desc.format = Ray::eTextureFormat::RGBA8888;
        tex_desc.data = img_data.data();
        tex_desc.w = img_w;
        tex_desc.h = img_h;
        tex_desc.generate_mipmaps = false;
        tex_desc.is_srgb = false;
        tex_desc.force_no_compression = true;

        env_desc.env_col[0] = env_desc.env_col[1] = env_desc.env_col[2] = 0.25f;
        env_desc.back_col[0] = env_desc.back_col[1] = env_desc.back_col[2] = 0.25f;

        env_desc.env_map = env_desc.back_map = scene.AddTexture(tex_desc);
        env_desc.env_map_rotation = env_desc.back_map_rotation = 2.35619449019f;
    } else if (test_scene == eTestScene::Standard_NoLight) {
        // nothing
    }

    scene.SetEnvironment(env_desc);

    scene.Finalize();
}

template void setup_test_scene(Ray::SceneBase &scene, bool output_sh, bool output_base_color, bool output_depth_normals,
                               const Ray::shading_node_desc_t &main_mat_desc, const char *textures[],
                               eTestScene test_scene);
template void setup_test_scene(Ray::SceneBase &scene, bool output_sh, bool output_base_color, bool output_depth_normals,
                               const Ray::principled_mat_desc_t &main_mat_desc, const char *textures[],
                               eTestScene test_scene);

void schedule_render_jobs(Ray::RendererBase &renderer, const Ray::SceneBase *scene, const Ray::settings_t &settings,
                          const int samples, const char *log_str) {
    const auto rt = renderer.type();
    const auto sz = renderer.size();

    if (rt & (Ray::RendererRef | Ray::RendererSSE2 | Ray::RendererSSE41 | Ray::RendererAVX | Ray::RendererAVX2 |
              Ray::RendererAVX512 | Ray::RendererNEON)) {
        static const int BucketSize = 16;

        std::vector<Ray::RegionContext> region_contexts;
        for (int y = 0; y < sz.second; y += BucketSize) {
            for (int x = 0; x < sz.first; x += BucketSize) {
                const auto rect =
                    Ray::rect_t{x, y, std::min(sz.first - x, BucketSize), std::min(sz.second - y, BucketSize)};
                region_contexts.emplace_back(rect);
            }
        }

        ThreadPool threads(std::thread::hardware_concurrency());

        auto render_job = [&](const int j, const int portion) {
#if defined(_WIN32)
            if (g_catch_flt_exceptions) {
                _controlfp(_EM_INEXACT | _EM_UNDERFLOW | _EM_OVERFLOW, _MCW_EM);
            }
#endif
            for (int i = 0; i < portion; ++i) {
                renderer.RenderScene(scene, region_contexts[j]);
            }
        };

        auto denoise_job = [&](const int j) {
#if defined(_WIN32)
            if (g_catch_flt_exceptions) {
                _controlfp(_EM_INEXACT | _EM_UNDERFLOW | _EM_OVERFLOW, _MCW_EM);
            }
#endif
            renderer.DenoiseImage(region_contexts[j]);
        };

        static const int SamplePortion = 16;
        for (int i = 0; i < samples; i += std::min(SamplePortion, samples - i)) {
            std::vector<std::future<void>> job_res;
            for (int j = 0; j < int(region_contexts.size()); ++j) {
                job_res.push_back(threads.Enqueue(render_job, j, std::min(SamplePortion, samples - i)));
            }
            for (auto &res : job_res) {
                res.wait();
            }
            job_res.clear();

            if (i + std::min(SamplePortion, samples - i) == samples) {
                for (int j = 0; j < int(region_contexts.size()); ++j) {
                    job_res.push_back(threads.Enqueue(denoise_job, j));
                }
                for (auto &res : job_res) {
                    res.wait();
                }
                job_res.clear();
            }

            // report progress percentage
            const float prog = 100.0f * float(i + std::min(SamplePortion, samples - i)) / float(samples);
            printf("\r%s (%6s, %s): %.1f%% ", log_str, Ray::RendererTypeName(rt), settings.use_hwrt ? "HWRT" : "SWRT",
                   prog);
            fflush(stdout);
        }
    } else {
        static const int SamplePortion = 16;

        auto region = Ray::RegionContext{{0, 0, sz.first, sz.second}};
        for (int i = 0; i < samples; ++i) {
            renderer.RenderScene(scene, region);

            if ((i % SamplePortion) == 0 || i == samples - 1) {
                // report progress percentage
                const float prog = 100.0f * float(i + 1) / float(samples);
                printf("\r%s (%6s, %s): %.1f%% ", log_str, Ray::RendererTypeName(rt),
                       settings.use_hwrt ? "HWRT" : "SWRT", prog);
                fflush(stdout);
            }
        }
        renderer.DenoiseImage(region);
    }
}