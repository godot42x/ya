fn main() {
    env_logger::Builder::from_default_env()
        .filter_level(log::LevelFilter::Info)
        .init();
    // env_logger::init();
    println!("Hello, world!");
    neon_rs::run();
}
