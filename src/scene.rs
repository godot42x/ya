pub struct Scene {
    pub objects: Vec<SceneObject>,
}

pub struct SceneObject {
    pub id: u32,
    pub name: String,
    pub transform: Transform,
}

pub struct Transform {
    pub position: [f32; 3],
    pub rotation: [f32; 3],
    pub scale: [f32; 3],
}
