BUILD_FLAGS=""
BUILD_UNITY_PLUGIN=false
while getopts 'dp' OPTION; do
    case "$OPTION" in
        d) 
            BUILD_FLAGS="${BUILD_FLAGS}-g -O0"
            ;;
        p)
            BUILD_UNITY_PLUGIN=true
            ;;
        ?)
            echo "Unrecognized option ${OPTION}"
            exit 1
            ;;
    esac
done

(set -x ; clang++ -std=c++17 $BUILD_FLAGS standalone.cpp -l portaudio -o standalone.out)
if [ "$BUILD_UNITY_PLUGIN" = true ]; then
    (set -x ; clang++ -std=c++17 -shared -rdynamic -fPIC AudioPluginUtil.cpp Plugin_Howdy.cpp -o libAudioPluginHowdy.dylib)
fi