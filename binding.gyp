{
    "targets": [
        {
            "target_name": "addon",
            "sources": ["src/decode.cpp"],
            "xcode_settings": {
                "MACOSX_DEPLOYMENT_TARGET": "12.0",
                "OTHER_LDFLAGS": [
                    "-framework",
                    "AppKit",
                    "-framework",
                    "CoreMedia",
                    "-framework",
                    "CoreFoundation",
                    "-framework",
                    "Security",
                ],
            },
            "conditions": [
                [
                    "OS=='mac'",
                    {
                        "actions": [
                            {
                                "action_name": "ffmpeg configure",
                                "inputs": ["<(module_root_dir)/FFmpeg/configure"],
                                "outputs": ["<(module_root_dir)/FFmpeg/ffmpeg"],
                                "action": [
                                    "bash",
                                    "-c",
                                    "src/build-ffmpeg-darwin.sh",
                                ],
                            },
                        ],
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
                    },
                ],
                [
                    "OS=='linux'",
                    {
                        "actions": [
                            {
                                "action_name": "ffmpeg configure",
                                "inputs": ["<(module_root_dir)/FFmpeg/configure"],
                                "outputs": ["<(module_root_dir)/FFmpeg/ffmpeg"],
                                "action": [
                                    "bash",
                                    "-c",
                                    "src/build-ffmpeg-linux.sh",
                                ],
                            },
                        ],
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
                            "-ldrm",
                            "-lva",
                            "-lva-drm",
                        ],
                        "ldflags": ["-Wl,-Bsymbolic"],
                    },
                ],
                [
                    "OS=='linux' and target_arch=='x64'",
                    {
                        "libraries": [
                            # vulkan
                            "-lglslang",
                            "-lglslang-default-resource-limits",
                            "-lshaderc_combined",
                        ]
                    },
                ],
            ],
            "cflags": ["-std=c++17"],
            "defines": ["NAPI_DISABLE_CPP_EXCEPTIONS"],
            "dependencies": ["<!(node -p \"require('node-addon-api').gyp\")"],
        }
    ]
}
