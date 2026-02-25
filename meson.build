project('oneDirectionCore', ['c', 'cpp'],
  version : '0.1',
  default_options : ['warning_level=2', 'cpp_std=gnu++20', 'c_std=gnu11', 'prefer_static=true'])

cc = meson.get_compiler('c')
inc = include_directories('.', '3rdparty/imgui', '3rdparty/imgui/backends')
thread_dep = dependency('threads')
glfw_dep = dependency('glfw3', required: false)

if host_machine.system() == 'windows'
  gl_dep = cc.find_library('opengl32')
  gdi32_dep = cc.find_library('gdi32')
  dwmapi_dep = cc.find_library('dwmapi', required: false)

  core_lib = shared_library('od_core',
    sources: [
      'core/driver/capture_windows_ext.c',
      'core/dsp/classifier_windows.c',
      'core/dsp/dsp_windows.c',
      'hardware/serial_controller_windows.c'
    ],
    dependencies: [],
    name_prefix: '',
    link_args: ['-static']
  )

  executable('ODC-overlay-win',
    sources: [
      'overlay/linux/overlay_standalone.cpp',
      'overlay/osd_radar.cpp',
      '3rdparty/imgui/imgui.cpp',
      '3rdparty/imgui/imgui_draw.cpp',
      '3rdparty/imgui/imgui_widgets.cpp',
      '3rdparty/imgui/imgui_tables.cpp',
      '3rdparty/imgui/backends/imgui_impl_glfw.cpp',
      '3rdparty/imgui/backends/imgui_impl_opengl3.cpp'
    ],
    include_directories: inc,
    dependencies: [glfw_dep, gl_dep, gdi32_dep, dwmapi_dep, thread_dep],
    link_with: [core_lib],
    link_args: ['-static']
  )
else
  pw_dep = dependency('libpipewire-0.3')
  gl_dep = dependency('gl', required: false)
  gtk_dep = dependency('gtk4', required: false)

  core_lib = static_library('od_core',
    sources: [
      'core/driver/capture_linux.c',
      'core/dsp/classifier.c',
      'core/dsp/dsp.c',
      'hardware/serial_controller.c'
    ],
    dependencies: [pw_dep]
  )

  # Linux UI Application
  executable('ODC-linux',
    sources: [
      'ui/linux/main_linux.cpp'
    ],
    include_directories: inc,
    dependencies: [gtk_dep, thread_dep],
    link_with: [core_lib],
    link_args: ['-static-libgcc', '-static-libstdc++']
  )

  # Linux Standalone Overlay
  executable('ODC-overlay',
    sources: [
      'overlay/linux/overlay_standalone.cpp',
      'overlay/osd_radar.cpp',
      'hardware/serial_controller.c',
      '3rdparty/imgui/imgui.cpp',
      '3rdparty/imgui/imgui_draw.cpp',
      '3rdparty/imgui/imgui_widgets.cpp',
      '3rdparty/imgui/imgui_tables.cpp',
      '3rdparty/imgui/backends/imgui_impl_glfw.cpp',
      '3rdparty/imgui/backends/imgui_impl_opengl3.cpp'
    ],
    include_directories: inc,
    dependencies: [glfw_dep, gl_dep, pw_dep, thread_dep],
    link_with: [core_lib],
    link_args: ['-static-libgcc', '-static-libstdc++']
  )
endif
