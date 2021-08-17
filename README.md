
# 编译webrtc应用程序
  和webrtc使用同一编译工具，解决不同编译器（g++ vs clang++）和不同c++标准库带来的兼容性问题
gn gen out/Default --args='webrtc_src_dir="" webrtc_build_dir=""'



webrtc_src_dir=""
1. cp -r $webrtc_src_dir/build ./
2. cp -r $webrtc_src_dir/buildtools ./
3. mkdir ./third_party && cp -r $webrtc_src_dir/third_party/llvm-build ./third_party
4. cp -r $webrtc_src_dir/build_overrides ./
5. cp -r $webrtc_src_dir/tools ./


6. 屏蔽libc++的依赖
   //build/config/BUILD.gn
   public_deps += [ "//buildtools/third_party/libc++" ]
   