# 협업을 위해 필요한 규칙

## 1. Commit Message 규칙
- 한 커밋에는 *최대한* 하나의 주제의 변경 사항만 포함한다. 
- 커밋 메세지는 한글로 적는다. 
- 커밋 메세지는 어떠한 행동을 적는다. (e.g. 추가, 삭제, A 에서 B 로 변경 등등)

## 2. Branch 규칙
- `master` 브랜치는 항상 검증된 최신 버전의 코드와 문서만을 가진다. 
- `master` 브랜치에는 직접 커밋하지 않는다.
- `master` 브랜치로부터 개발용 브랜치를 생성한다.
- 가능하면 브랜치의 생성은 Github Issue 를 통해서 한다. 
- 개발용 브랜치의 이름은 다음과 같은 형식을 가진다. 
  - `<prefix>/<issue-number>-<issue-name/description>`
- 허용되는 prefix 는 다음과 같다. 
  - docs: 문서 업데이트 
  - feat: 새로운 기능 추가
  - fix: 버그 수정

## 3. Issue 규칙
- 해야할 일들은 생각 났을 때 Issue 로 만들어 놓는다.
- Issue 는 간단한 설명을 필수적으로 남겨야한다.


## 4. Pull Request 규칙
- 아직 작업이 안 끝났더라도 PR를 만들어 놓는 것은 권장한다. 
- Review 할 준비가 된 PR 은 `Ready for Review` 라벨을 달아준다.


