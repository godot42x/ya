use log::info;

fn main() {
    println!("Hello, world!");
    let current_dir = std::env::current_dir().unwrap();
    info!("Current working directory: {}", current_dir.display());
    neon_rs::init_logger();
    neon_rs::run();
}
