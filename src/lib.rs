mod app;
mod asset;
mod camera;
mod geo;
mod render_2d;
mod state;
// mod ecs;
// mod scene;
mod pipeline;

pub(crate) use log::{info, warn};
use winit::event_loop;

use crate::app::{App, AppSettings, CustomEvent};

#[cfg(target_arch = "wasm32")]
use wasm_bindgen::prelude::*;

enum EColor {
    Rest,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan,
    White,
    Black,
    BrightRed,
    BrightGreen,
    BrightYellow,
    BrightBlue,
    BrightMagenta,
    BrightCyan,
    BrightWhite,
    BrightBlack,
}

impl EColor {
    fn to_ansi_code(&self) -> &'static str {
        match self {
            EColor::Rest => "\x1b[0m",
            EColor::Black => "\x1b[30m",
            EColor::Red => "\x1b[31m",
            EColor::Green => "\x1b[32m",
            EColor::Yellow => "\x1b[33m",
            EColor::Blue => "\x1b[34m",
            EColor::Magenta => "\x1b[35m",
            EColor::Cyan => "\x1b[36m",
            EColor::White => "\x1b[37m",
            EColor::BrightBlack => "\x1b[90m",
            EColor::BrightRed => "\x1b[91m",
            EColor::BrightGreen => "\x1b[92m",
            EColor::BrightYellow => "\x1b[93m",
            EColor::BrightBlue => "\x1b[94m",
            EColor::BrightMagenta => "\x1b[95m",
            EColor::BrightCyan => "\x1b[96m",
            EColor::BrightWhite => "\x1b[97m",
        }
    }
}

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
                .format(|buf, record| {
                    use std::io::Write;

                    // 获取当前时间
                    let now = std::time::SystemTime::now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .expect("failed to get system time");

                    let timestamp = chrono::DateTime::from_timestamp(
                        now.as_secs() as i64,
                        now.subsec_nanos(),
                    )
                    .expect("Invalid system time")
                    .format("%Y-%m-%d_%H:%M:%S%.3f")
                    .to_string();

                    // 使用 ANSI 颜色代码
                    let (level_color, level_text) = match record.level() {
                        log::Level::Error => (EColor::Red.to_ansi_code(), "ERROR"),
                        log::Level::Warn =>  (EColor::Yellow.to_ansi_code(),"WARN"),
                        log::Level::Info =>  (EColor::Green.to_ansi_code(), "INFO"),
                        log::Level::Debug => (EColor::Blue.to_ansi_code(), "DEBUG"),
                        log::Level::Trace => (EColor::Magenta.to_ansi_code(), "TRACE"),
                    };

                    let filename = record
                        .file_static()
                        .and_then(|f| std::path::Path::new(f).file_name())
                        .and_then(|name| name.to_str())
                        .unwrap_or("unknown");


                    writeln!(
                        buf,
                        "[{} {}{}{} {}:{}] {}",
                        timestamp,
                        level_color, level_text, EColor::Rest.to_ansi_code(), //
                        filename, record.line().unwrap_or(0), //
                        record.args()
                    )
                })
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
                                                                // event_loop.create_proxy();
    let mut app = App::new(AppSettings::default());
    event_loop.run_app(&mut app).unwrap();
    info!("Exited event loop");
}
