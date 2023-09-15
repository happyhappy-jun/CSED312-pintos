# 개발 가이드

## Git 사용 가이드
1. 새로운 일이 생겼을 때, Github Issue 를 생성한다.
2. Issue 를 수행하는 시점의 `master` 브랜치로부터 새로운 브랜치를 생성한다.
   - 브랜치의 이름은 다음과 같은 형식을 가진다. 
     - `<prefix>/<issue-number>-<issue-name/description>`
   - Github Issue Web 의 오른쪽 Development 섹션에서 브랜치를 만들면 좀 더 쉽다. 
3. 로컬 개발 환경에서 브랜치를 체크아웃한다.
```
# Github 에서 브랜치를 만들었을 때
git fetch origin
git checkout <prefix>/<issue-number>-<issue-name/description>

# 로컬에서 브랜치를 만들었을 때
git checkout -b <prefix>/<issue-number>-<issue-name/description>
```
4. 개발을 진행한다.
5. 개발을 진행 하는 중 또는 완료했을 때, Pull Request 를 생성한다. 
6. Pull Request 에서 리뷰어와 리뷰를 진행한 후, `master` 브랜치로 Merge 한다.


