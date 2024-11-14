{
    "targets": [
        {
            "target_name": "addon",
            "sources": [
                "src/formatcontext.cpp",
                "src/codeccontext.cpp",
                "src/packet.cpp",
                "src/frame.cpp",
                "src/error.cpp",
                "src/filter.cpp",
            ],
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
                    "-framework",
                    "OpenCL",
                ],
            },
            "configurations": {
                "Debug": {
                    "msvs_settings": {
                        "VCLinkerTool": {
                            "AdditionalOptions": ["/NODEFAULTLIB:libcmt.lib"]
                        },
                        "VCCLCompilerTool": {"RuntimeLibrary": "3"},  # /MDd
                    },
                },
                "Release": {
                    "msvs_settings": {
                        "VCLinkerTool": {
                            "AdditionalOptions": ["/NODEFAULTLIB:libcmt.lib"]
                        },
                        "VCCLCompilerTool": {"RuntimeLibrary": "2"},  # /MD
                    },
                },
            },
            "conditions": [
                [
                    "OS=='win'",
                    {
                        "copies": [
                            {
                                "files": [
                                    "<(module_root_dir)/FFmpeg/ffmpeg.exe",
                                ],
                                "destination": "<(PRODUCT_DIR)/",
                            }
                        ],
                        "actions": [
                            {
                                "action_name": "ffmpeg configure",
                                "inputs": ["<(module_root_dir)/FFmpeg/configure"],
                                "outputs": ["<(module_root_dir)/FFmpeg/ffmpeg.exe"],
                                "action": [
                                    "cmd.exe",
                                    "/c",
                                    "src/build-ffmpeg-windows.bat",
                                ],
                            },
                        ],
                        "include_dirs": [
                            "<!@(node -p \"require('node-addon-api').include\")",
                            "<(module_root_dir)/FFmpeg",
                        ],
                        "library_dirs": [
                            "C:\\VulkanSDK\\1.3.280.0\\Lib"
                        ],
                        "libraries": [
                            "<(module_root_dir)/FFmpeg/libavcodec/libavcodec.a",
                            "<(module_root_dir)/FFmpeg/libavfilter/libavfilter.a",
                            "<(module_root_dir)/FFmpeg/libavformat/libavformat.a",
                            "<(module_root_dir)/FFmpeg/libavutil/libavutil.a",
                            "<(module_root_dir)/FFmpeg/libswscale/libswscale.a",
                            "<(module_root_dir)/FFmpeg/libswresample/libswresample.a",
                            "<(module_root_dir)/_vplinstall/lib/vpl.lib",

                            # vulkan
                            "glslang.lib",
                            "glslang-default-resource-limits.lib",
                            "shaderc_combined.lib",

                            "Ws2_32.lib",
                            "mf.lib",
                            "Mfplat.lib",
                            "Mfuuid.lib",
                            "strmiids.lib",
                            "secur32.lib",
                            "bcrypt.lib",
                        ],
                    },
                ],
                [
                    "OS=='mac'",
                    {
                        "copies": [
                            {
                                "files": [
                                    "<(module_root_dir)/FFmpeg/ffmpeg",
                                ],
                                "destination": "<(PRODUCT_DIR)/",
                            }
                        ],
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
                        "copies": [
                            {
                                "files": [
                                    "<(module_root_dir)/FFmpeg/ffmpeg",
                                ],
                                "destination": "<(PRODUCT_DIR)/",
                            }
                        ],
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
                            "-lOpenCL",
                        ],
                        "ldflags": ["-Wl,-Bsymbolic"],
                    },
                ],
                [
                    "OS=='linux' and target_arch=='x64'",
                    {
                        "libraries": [
                            # vpl
                            "<(module_root_dir)/_vplinstall/lib/libvpl.a",
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