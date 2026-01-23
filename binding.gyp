{
  "targets": [
    {
      "target_name": "skia_render",
      "sources": ["./src/skia_render.cc"],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")"
      ],
      "defines": [
        "NAPI_DISABLE_CPP_EXCEPTIONS",
        "GL_SILENCE_DEPRECATION"
      ],
      "cflags!": ["-fno-exceptions"],
      "cflags_cc!": ["-fno-exceptions"],
      "conditions": [
        ["OS=='mac'", {
          "xcode_settings": {
            "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
            "CLANG_CXX_LIBRARY": "libc++",
            "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
            "MACOSX_DEPLOYMENT_TARGET": "10.15",
            "OTHER_CFLAGS": ["-DGL_SILENCE_DEPRECATION"]
          },
          "link_settings": {
            "libraries": [
              "-framework OpenGL",
              "-framework Cocoa",
              "-framework CoreFoundation"
            ]
          }
        }],
        ["OS=='linux'", {
          "libraries": [
            "-lGL",
            "-lEGL"
          ]
        }],
        ["OS=='win'", {
          "libraries": [
            "opengl32.lib"
          ]
        }]
      ]
    }
  ]
}
