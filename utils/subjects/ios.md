# Building bitcode for iOS apps

This is a first draft, and I may have forgotten some steps.

This approach uses the individual bitcode files from
`Build/Intermediates.noindex`, rather than extracting them from
`__LLVM,__bundle` sections in linked files from `Build/Products`, because the
latter would be a bit complicated to implement in `bc-imitate`.

## General setup

First, install Homebrew and Xcode 13. Then run these:

```zsh
sudo gem install cocoapods
brew update
brew install carthage
brew install llvm

cat >/tmp/tmp.xcconfig <<EOF
CODE_SIGN_IDENTITY=
CODE_SIGNING_REQUIRED=NO
ENTITLEMENTS_REQUIRED=NO
BITCODE_GENERATION_MODE=bitcode
ENABLE_BITCODE=YES
IPHONEOS_DEPLOYMENT_TARGET=15.0
EOF
export XCODE_XCCONFIG_FILE=/tmp/tmp.xcconfig
```

- Note: `IPHONEOS_DEPLOYMENT_TARGET=15.0` causes only arm64 version to be
  built, not armv7.
- Todo: try `HIDE_BITCODE_SYMBOLS=NO` and
  `SWIFT_EMBED_BITCODE_SECTION_HIDE_SYMBOLS=NO`; maybe those would simplify the
  process of using the `__LLVM,__bundle` section.

## Building RIBs (Uber framework) tutorial app

Ensure `XCODE_XCCONFIG_FILE` is set as above, then:

<!-- markdownlint-disable MD013 -->

```zsh
git clone https://github.com/uber/RIBs
cd RIBs
git checkout 1592ebf5d781555e6cbdb2fd0ce0bff1f770a5a4
cd ios/tutorials/tutorial4-completed 
vim Podfile # modify RxCocoa line to use version 6.0, not 5.1
pod install
xcodebuild -workspace TicTacToe.xcworkspace -scheme TicTacToe -configuration Debug -sdk iphoneos -destination generic/platform=iOS
xcodebuild -workspace TicTacToe.xcworkspace -scheme TicTacToe -configuration Release -sdk iphoneos -destination generic/platform=iOS
cd ~/Library/Developer/Xcode/DerivedData/TicTacToe-*/Build/Intermediates.noindex
export PATH="/usr/local/opt/llvm/bin:$PATH"
mkdir ~/TicTacToe-Debug-arm64 ~/TicTacToe-Release-arm64
llvm-link TicTacToe.build/Debug-iphoneos/TicTacToe.build/Objects-normal/arm64/*.bc -o ~/TicTacToe-Debug-arm64/TicTacToe.bc
llvm-link TicTacToe.build/Release-iphoneos/TicTacToe.build/Objects-normal/arm64/*.bc -o ~/TicTacToe-Release-arm64/TicTacToe.bc
for x in RIBs RxCocoa RxRelay RxSwift SnapKit; do llvm-link Pods.build/Debug-iphoneos/${x}.build/Objects-normal/arm64/*.bc -o ~/TicTacToe-Debug-arm64/${x}.bc; done
for x in RIBs RxCocoa RxRelay RxSwift SnapKit; do llvm-link Pods.build/Release-iphoneos/${x}.build/Objects-normal/arm64/*.bc -o ~/TicTacToe-Release-arm64/${x}.bc; done
```

## Building Nextcloud

Ensure `XCODE_XCCONFIG_FILE` is set as above, then:

<!-- markdownlint-disable MD013 -->

```zsh
git clone 'https://github.com/nextcloud/ios.git' Nextcloud
cd Nextcloud
git checkout 82478079c319cc3d2f62342fb7f2629ceb035b67
carthage update --use-xcframeworks --platform iOS
xcodebuild -project Nextcloud.xcodeproj -scheme Nextcloud -sdk iphoneos -destination generic/platform=ios build 2>&1 | tee /tmp/xc.log
carthage update --use-xcframeworks --platform iOS --configuration Release
xcodebuild -project Nextcloud.xcodeproj -scheme Nextcloud -configuration Release -sdk iphoneos -destination generic/platform=ios build 2>&1 | tee /tmp/xc.log
cd ~/Library/Developer/Xcode/DerivedData/Nextcloud-*/Build/Intermediates.noindex
export PATH="/usr/local/opt/llvm/bin:$PATH"
mkdir ~/Nextcloud-Release-arm64 ~/Nextcloud-Debug-arm64
setopt extendedglob
for x in *.build/Debug-iphoneos/*.build; do [[ -n "$x"/Objects-normal/arm64/*.bc(#qN) ]] || continue; y="${x%.build}"; llvm-link "$x"/Objects-normal/arm64/*.bc -o ~/Nextcloud-Debug-arm64/"${y##*/}".bc; done
for x in *.build/Release-iphoneos/*.build; do [[ -n "$x"/Objects-normal/arm64/*.bc(#qN) ]] || continue; y="${x%.build}"; llvm-link "$x"/Objects-normal/arm64/*.bc -o ~/Nextcloud-Release-arm64/"${y##*/}".bc; done
```
