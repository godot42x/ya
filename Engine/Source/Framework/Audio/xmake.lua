-- ya-audio: the engine's audio interface plus its SDL3 backend.
--
-- The public surface is interface-only (AudioSystem / AudioClip / handles) so a
-- later self-mixed backend (showcase-roadmap A2) can replace SDL3 without the
-- game or the script layer changing. This target is deliberately bare: it
-- depends on foundation-core for logging and on SDL3 for the device, and knows
-- nothing about ECS, Scene or Render.
target("ya-audio")
    set_kind(ya_target_kind())
    ya_std_module("YA_AUDIO_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", { public = true })
    add_packages("libsdl3")
    -- stb_vorbis decodes the .ogg clips the game ships; SDL_LoadWAV covers wav.
    add_packages("stb")
