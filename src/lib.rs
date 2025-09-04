mod app;
mod asset;
mod scene;
mod state;

use std::sync::Arc;

pub(crate) use log::{error, info, warn};
use winit::{
    application::ApplicationHandler, event_loop, keyboard::NamedKey, window::WindowAttributes,
};

use crate::{
    app::{App, CustomEvent},
    asset::Vertex,
    state::State,
};

pub fn run() {
    let event_loop = event_loop::EventLoop::<CustomEvent>::with_user_event()
        .build()
        .unwrap();
    event_loop.set_control_flow(event_loop::ControlFlow::Poll); // for game loop poll, not wait in other app
    let mut app = App::default();
    event_loop.run_app(&mut app).unwrap();
    info!("Exited event loop");
}
