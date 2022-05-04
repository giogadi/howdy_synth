BUILD_FLAGS=""
BUILD_UNITY_PLUGIN=false
INSTALL_UNITY_PLUGIN=false
while getopts 'dpir' OPTION; do
    case "$OPTION" in
        d) 
            BUILD_FLAGS="${BUILD_FLAGS}-g -O0"
            ;;
        r)
            BUILD_FLAGS="${BUILD_FLAGS} -O3"
            ;;
        p)
            BUILD_UNITY_PLUGIN=true
            ;;
        i)
            INSTALL_UNITY_PLUGIN=true
            ;;
        ?)
            echo "Unrecognized option ${OPTION}"
            exit 1
            ;;
    esac
done

(set -x ; clang++ -std=c++17 $BUILD_FLAGS standalone.cpp -l portaudio -o standalone.out)
if [ "$BUILD_UNITY_PLUGIN" = true ]; then
    #(set -x ; clang++ -std=c++17 -shared -rdynamic -fPIC -framework CoreMIDI -framework CoreFoundation AudioPluginUtil.cpp Plugin_Howdy.cpp Plugin_UnitySynth.cpp -o libAudioPluginHowdy.dylib)
    (set -x ; clang++ -std=c++17 $BUILD_FLAGS -shared -rdynamic -fPIC AudioPluginUtil.cpp Plugin_Howdy.cpp Plugin_UnitySynth.cpp -o libAudioPluginHowdy.dylib)
fi
if [ "$BUILD_UNITY_PLUGIN" = true ]; then
    (set -x ; cp libAudioPluginHowdy.dylib ~/games/audial/Assets/Plugins/x64/libAudioPluginDemo.dylib)
fi 