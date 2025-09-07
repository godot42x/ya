mod app;
mod asset;
mod camera;
mod ecs;
mod scene;
mod state;

use std::sync::Arc;

pub(crate) use log::{info, warn};
use winit::{
    application::ApplicationHandler, event_loop, keyboard::NamedKey, window::WindowAttributes,
};

use crate::{
    app::{App, CustomEvent},
    asset::Vertex,
    state::State,
};

#[cfg(target_arch = "wasm32")]
use wasm_bindgen::prelude::*;

pub fn init_logger() {
    cfg_if::cfg_if! {
        if #[cfg(target_arch = "wasm32")] {
            // 使用查询字符串来获取日志级别。
            let query_string = web_sys::window().unwrap().location().search().unwrap();
            let query_level: Option<log::LevelFilter> = parse_url_query_string(&query_string, "RUST_LOG")
                .and_then(|x| x.parse().ok());

            // 将 wgpu 日志级别保持在错误级别，因为 Info 级别的日志输出非常多。
            let base_level = query_level.unwrap_or(log::LevelFilter::Info);
            let wgpu_level = query_level.unwrap_or(log::LevelFilter::Error);

            // 在 web 上，我们使用 fern，因为 console_log 没有按模块级别过滤功能。
            fern::Dispatch::new()
                .level(base_level)
                .level_for("wgpu_core", wgpu_level)
                .level_for("wgpu_hal", wgpu_level)
                .level_for("naga", wgpu_level)
                .chain(fern::Output::call(console_log::log))
                .apply()
                .unwrap();
            std::panic::set_hook(Box::new(console_error_panic_hook::hook));
        } else {
            // parse_default_env 会读取 RUST_LOG 环境变量，并在这些默认过滤器之上应用它。
            env_logger::builder()
                .filter_level(log::LevelFilter::Info)
                .filter_module("wgpu_core", log::LevelFilter::Info)
                .filter_module("wgpu_hal", log::LevelFilter::Error)
                .filter_module("naga", log::LevelFilter::Error)
                // .format(|buf, record| {
                //     use std::io::Write;

                //     // 获取当前时间
                //     let now = std::time::SystemTime::now()
                //         .duration_since(std::time::UNIX_EPOCH)
                //         .unwrap();
                //     let timestamp = format!("{:02}:{:02}:{:02}.{:03}",
                //         (now.as_secs() % 86400) / 3600,  // 小时
                //         (now.as_secs() % 3600) / 60,     // 分钟
                //         now.as_secs() % 60,              // 秒
                //         now.subsec_millis()              // 毫秒
                //     );

                //     // 获取文件名（不包含路径）
                //     let file = record.file()
                //         .and_then(|f| std::path::Path::new(f).file_name())
                //         .and_then(|f| f.to_str())
                //         .unwrap_or("unknown");

                //     // 使用 ANSI 颜色代码
                //     let (level_color, level_text) = match record.level() {
                //         log::Level::Error => ("\x1b[31m", "ERROR"),  // 红色
                //         log::Level::Warn =>  ("\x1b[33m", "WARN"),   // 黄色
                //         log::Level::Info =>  ("\x1b[32m", "INFO"),   // 绿色
                //         log::Level::Debug => ("\x1b[34m", "DEBUG"),  // 蓝色
                //         log::Level::Trace => ("\x1b[35m", "TRACE"),  // 洋红色
                //     };

                //     writeln!(
                //         buf,
                //         "\x1b[36m[{}]\x1b[0m \x1b[90m[{}:{}]\x1b[0m {}{}\x1b[0m: {}",
                //         timestamp,                        // 青色时间戳
                //         file,
                //         record.line().unwrap_or(0),     // 暗灰色文件位置
                //         level_color,                     // 级别颜色
                //         level_text,                      // 级别文本
                //         record.args()                    // 重置 + 消息
                //     )
                // })
                .parse_default_env()
                .init();
        }
    }
}

pub fn run() {
    let event_loop = event_loop::EventLoop::<CustomEvent>::with_user_event()
        .build()
        .unwrap();
    event_loop.set_control_flow(event_loop::ControlFlow::Poll); // for game loop poll, not wait in other app
    let mut app = App::default();
    event_loop.run_app(&mut app).unwrap();
    info!("Exited event loop");
}
