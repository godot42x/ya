use gltf::mesh::util::indices;

use crate::pipeline::pl_3d;

pub fn make_cube() -> (Vec<pl_3d::Vertex>, Vec<u32>) {
    make_cube_ex(1.0, 1.0, 1.0, 1.0, 1.0, 1.0)
}

pub fn make_cube_ex(
    left: f32,
    right: f32,
    top: f32,
    bottom: f32,
    front: f32,
    back: f32,
) -> (Vec<pl_3d::Vertex>, Vec<u32>) {
    let vertices_pos = [
        // Front face
        glam::vec3(-left, -top, front),   // 0: FLT
        glam::vec3(-left, bottom, front), // 1: FLB
        glam::vec3(right, bottom, front), // 2: FRB
        glam::vec3(right, -top, front),   // 3: FRT
        // Right face
        glam::vec3(right, -top, front),   // 4: FRT
        glam::vec3(right, bottom, front), // 5: FRB
        glam::vec3(right, bottom, -back), // 6: BRB
        glam::vec3(right, -top, -back),   // 7: BRT
        // Back face
        glam::vec3(right, -top, -back),   // 8: BRT
        glam::vec3(right, bottom, -back), // 9: BRB
        glam::vec3(-left, bottom, -back), // 10: BLB
        glam::vec3(-left, -top, -back),   // 11: BLT
        // Left face
        glam::vec3(-left, -top, -back),   // 12: BLT
        glam::vec3(-left, bottom, -back), // 13: BLB
        glam::vec3(-left, bottom, front), // 14: FLB
        glam::vec3(-left, -top, front),   // 15: FLT
        // Top face
        glam::vec3(-left, -top, -back), // 16: BLT
        glam::vec3(-left, -top, front), // 17: FLT
        glam::vec3(right, -top, front), // 18: FRT
        glam::vec3(right, -top, -back), // 19: BRT
        // Bottom face
        glam::vec3(right, bottom, front), // 20: FRB
        glam::vec3(-left, bottom, front), // 21: FLB
        glam::vec3(-left, bottom, -back), // 22: BLB
        glam::vec3(right, bottom, -back), // 23: BRB
    ];

    let indices = [
        0u32, 2, 1, 0, 3, 2, // front - 逆时针
        4, 6, 5, 4, 7, 6, // right - 逆时针
        8, 10, 9, 8, 11, 10, // back - 逆时针
        12, 14, 13, 12, 15, 14, // left - 逆时针
        16, 18, 17, 16, 19, 18, // top - 逆时针
        20, 22, 21, 20, 23, 22, // bottom - 逆时针
    ];

    let normals = [
        glam::vec3(0.0, 0.0, 1.0),  // front
        glam::vec3(1.0, 0.0, 0.0),  // right
        glam::vec3(0.0, 0.0, -1.0), // back
        glam::vec3(-1.0, 0.0, 0.0), // left
        glam::vec3(0.0, 1.0, 0.0),  // top
        glam::vec3(0.0, -1.0, 0.0), // bottom
    ];

    // UV coordinates for each face (4 vertices per face)
    let uvs = [
        // Front face
        [0.0, 1.0], // LT
        [0.0, 0.0], // LB
        [1.0, 0.0], // RB
        [1.0, 1.0], // RT
        // Right face
        [0.0, 1.0],
        [0.0, 0.0],
        [1.0, 0.0],
        [1.0, 1.0],
        // Back face
        [0.0, 1.0],
        [0.0, 0.0],
        [1.0, 0.0],
        [1.0, 1.0],
        // Left face
        [0.0, 1.0],
        [0.0, 0.0],
        [1.0, 0.0],
        [1.0, 1.0],
        // Top face
        [0.0, 1.0],
        [0.0, 0.0],
        [1.0, 0.0],
        [1.0, 1.0],
        // Bottom face
        [1.0, 1.0],
        [0.0, 1.0],
        [0.0, 0.0],
        [1.0, 0.0],
    ];

    let vertices: Vec<pl_3d::Vertex> = vertices_pos
        .iter()
        .enumerate()
        .map(|(i, pos)| {
            let face_index = i / 4;
            pl_3d::Vertex {
                position: pos.to_array(),
                color: [1.0, 1.0, 1.0, 1.0],
                normal: normals[face_index].to_array(),
                uv: uvs[i],
            }
        })
        .collect();

    (vertices, indices.to_vec())
}
