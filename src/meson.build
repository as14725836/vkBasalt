subdir('shader')
subdir('reshade')

vkBasalt_src = [
    'basalt.cpp',
    'buffer.cpp',
    'command_buffer.cpp',
    'config.cpp',
    'descriptor_set.cpp',
    'effect_cas.cpp',
    'effect.cpp',
    'effect_deband.cpp',
    'effect_dls.cpp',
    'effect_fxaa.cpp',
    'effect_lut.cpp',
    'effect_reshade.cpp',
    'effect_simple.cpp',
    'effect_smaa.cpp',
    'effect_transfer.cpp',
    'effect_upscale.cpp',
    'fake_swapchain.cpp',
    'format.cpp',
    'framebuffer.cpp',
    'graphics_pipeline.cpp',
    'image.cpp',
    'image_view.cpp',
    'keyboard_input.cpp',
    'logger.cpp',
    'logical_swapchain.cpp',
    'lut_cube.cpp',
    'memory.cpp',
    'renderpass.cpp',
    'reshade_uniforms.cpp',
    'sampler.cpp',
    'shader.cpp',
    'stb_image.c',
    'stb_image_resize.c',
    'util.cpp',
    'vkdispatch.cpp',
]

x11_deps = []
keyboard_input_link = []
keyboard_input_cpp_args = []
if get_option('x11')
    x11_deps = [dependency('x11')]
endif

conf_paths = configuration_data()
conf_paths.set_quoted('SYSCONFDIR', join_paths(get_option('prefix'), get_option('sysconfdir')))
conf_paths.set_quoted('DATADIR', join_paths(get_option('prefix'), get_option('datadir')))

configure_file(output: 'config_paths.hpp',
               configuration : conf_paths)

if get_option('x11')
    # X11/X.h library contains many definitions (e.g. None)
    # which conflicts with the rest codebase with with Meson --unity=on option
    keyboard_input_link = [static_library('keyboard_input_x11', 'keyboard_input_x11.cpp')]
else
    # no X11: the toggle hotkey is unavailable, everything else keeps working
    keyboard_input_cpp_args = ['-DVKBASALT_X11=0']
endif

shared_library(meson.project_name().to_lower(), 
    vkBasalt_src, shader_include,
    link_with: keyboard_input_link,
    include_directories : vkBasalt_include_path,
    cpp_args : keyboard_input_cpp_args,
    dependencies : [reshade_dep] + x11_deps,
    gnu_symbol_visibility: 'hidden',
    install : true,
    install_dir : lib_dir)
