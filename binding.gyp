{
  "targets": [
    {
      "target_name": "addon",
      "sources": ["src/decode.cpp"],
      "xcode_settings": {
        "MACOSX_DEPLOYMENT_TARGET": "12.0",
        "OTHER_LDFLAGS": [
            "-framework", "AppKit",
            "-framework", "CoreMedia",
            "-framework", "CoreFoundation",
            "-framework", "Security"
        ]
      },
      "conditions": [
      [ "OS=='mac'", {
        "include_dirs": [
          "<!@(node -p \"require('node-addon-api').include\")",
          "<(module_root_dir)/FFmpeg",
        ],
        "libraries": [
            "<(module_root_dir)/FFmpeg/libavcodec/libavcodec.a",
            "<(module_root_dir)/FFmpeg/libavfilter/libavfilter.a",
            "<(module_root_dir)/FFmpeg/libavformat/libavformat.a",
            "<(module_root_dir)/FFmpeg/libavutil/libavutil.a",
            "<(module_root_dir)/FFmpeg/libswscale/libswscale.a",
        ],
      }],
      [ "OS=='linux'", {
        "include_dirs": [
          "<!@(node -p \"require('node-addon-api').include\")",
          "/usr/include"
        ],
        "libraries": [
          "-L/opt/homebrew/lib/",
          "-lavcodec",
          "-lavformat",
          "-lavfilter",
          "-lavutil"
        ],
      }]
      ],
      "cflags": ["-std=c++17"],
      'defines': [ 'NAPI_DISABLE_CPP_EXCEPTIONS' ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ]
    }
  ]
}