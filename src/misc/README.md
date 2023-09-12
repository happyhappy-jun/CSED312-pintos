# Mac 에서 Boch 설치

## 원본 
Homebrew 에서 제공하는 Bochs Formula 를 개조해서 사용합니다.
[원본 링크](https://github.com/Homebrew/homebrew-core/blob/master/Formula/b/bochs.rb)

## Bochs 설치
`brew` 가 셋업 되어있다고 가정합니다
```bash
sh bochs-setup.sh
```

## 설명
`bochs.rb` 의 L64에 `--enable-gdb-stub` 옵션을 추가하였고, 
기존에 있던 `--enable-debugger` 옵션과 충돌나서 그 친구는 삭제