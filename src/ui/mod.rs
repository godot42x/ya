trait PaintableUI {
    fn paint(&self, render_api: &dyn RenderAPI);
}

struct UIElement {
    // 这里可以存储 UI 元素的相关信息，例如类型、位置、大小等
    element_type: String,

    children: Vec<Box<dyn PaintableUI>>, // 子元素列表
}

struct UIPanel {
    // 这里可以存储面板的相关信息，例如位置、大小等
    x: f32,
    y: f32,
    width: f32,
    height: f32,

    children: Vec<Box<dyn PaintableUI>>, // 面板内的 UI 元素列表
}

struct Viewport {
    // 这里可以存储视口的相关信息，例如位置、大小等
    x: f32,
    y: f32,
    width: f32,
    height: f32,

    root_element: Box<dyn PaintableUI>, // 视口内的根 UI 元素
}

pub struct UIManager {
    // 这里可以存储 UI 状态、布局信息等
    viewports: Vec<Viewport>,
}

impl UIManager {
    pub fn new() -> Self {
        Self {
            // 初始化 UI 状态
            viewports: Vec::new(),
        }
    }

    pub fn add_viewport(&mut self, viewport: Viewport) {
        self.viewports.push(viewport);
    }

    pub fn get_viewport(&self, index: usize) -> Option<&Viewport> {
        self.viewports.get(index)
    }
}

impl PaintableUI for UIElement {
    fn paint(&self, render_api: &dyn RenderAPI) {
        // 这里可以实现绘制 UI 元素的逻辑，例如使用 wgpu 绘制矩形、文本等
        println!("Painting UI Element: {} ", self.element_type);

        // 递归绘制子元素
        for child in &self.children {
            child.paint(render_api);
        }
    }
}

impl PaintableUI for UIPanel {
    fn paint(&self, render_api: &dyn RenderAPI) {
        // 这里可以实现绘制面板的逻辑，例如使用 wgpu 绘制面板背景等
        println!(
            "Painting UIPanel at ({}, {}) with size ({}, {})",
            self.x, self.y, self.width, self.height
        );
        render_api.make_quad(self.x, self.y, self.width, self.height);

        // 绘制面板内的根 UI 元素
        for child in &self.children {
            child.paint(render_api);
        }
    }
}

trait RenderAPI {
    fn make_quad(&self, x: f32, y: f32, width: f32, height: f32);
}
struct Render2D {}

impl RenderAPI for Render2D {
    fn make_quad(&self, x: f32, y: f32, width: f32, height: f32) {
        // 这里可以实现使用 wgpu 绘制一个矩形的逻辑
        println!(
            "Drawing quad at ({}, {}) with size ({}, {})",
            x, y, width, height
        );
    }
}

pub fn _test_render_ui() {
    let mut ui_manager = UIManager::new();

    // 创建一个视口
    let viewport = Viewport {
        x: 0.0,
        y: 0.0,
        width: 800.0,
        height: 600.0,
        root_element: Box::new(UIElement {
            element_type: "root".to_string(),
            children: vec![],
        }),
    };

    ui_manager.add_viewport(viewport);

    let renderer = Render2D {};
    ui_manager
        .get_viewport(0)
        .unwrap()
        .root_element
        .paint(&renderer);
}
