# 로컬 서버 실행 가이드 (Android 개발자용)

내 컴퓨터에서 백엔드 서버를 켜고, Android 앱을 연결해 테스트하는 방법입니다.
C++ 코드나 빌드 도구는 몰라도 되고, Docker만 있으면 됩니다.

## 한눈에 보기

1. Docker Desktop을 설치한다.
2. 저장소를 받고 `.env` 파일을 만든다.
3. `docker compose pull` 후 `docker compose up -d`로 서버를 켠다.
4. `docker compose logs tunnel`에 나오는 `https://...trycloudflare.com` 주소를 앱의 **설정 > 개발용 서버 연결**에 넣는다.

앱은 HTTPS 서버에만 연결되므로, 에뮬레이터에서 테스트할 때도 4번의 HTTPS 주소를 사용합니다.

## 1. 준비물 (처음 한 번)

| 도구 | 설치 방법 |
| --- | --- |
| Git | 이미 있으면 생략 |
| Docker Desktop | https://www.docker.com/products/docker-desktop 에서 설치. Windows는 설치 중 WSL 2 사용을 선택 |

설치 후 Docker Desktop을 실행해 두세요. 고래 아이콘이 떠 있어야 아래 명령어가 동작합니다.

## 2. 처음 실행

터미널(Windows는 PowerShell)에서 아래 순서대로 실행합니다.

```bash
git clone https://github.com/princess-laundry/backend.git
```

```bash
cd backend
```

설정 파일을 복사합니다. 들어 있는 값 그대로 로컬에서 동작하므로 수정할 필요가 없습니다.

macOS:

```bash
cp .env.example .env
```

Windows PowerShell:

```powershell
Copy-Item .env.example .env
```

미리 빌드된 서버 이미지를 받고 서버를 켭니다.

```bash
docker compose pull
```

```bash
docker compose up -d
```

`docker compose pull`이 서버 이미지(`ghcr.io/princess-laundry/backend`)에서 실패하면, 대신 아래 명령으로 직접 빌드합니다. 처음 한 번은 5분에서 15분 정도 걸립니다.

```bash
docker compose up -d --build
```

## 3. 잘 켜졌는지 확인

```bash
docker compose ps
```

`db`, `server`, `nginx`, `docs`, `tunnel`이 모두 `Up`이면 정상입니다. `migrate`는 DB 준비 작업만 하고 끝나는 컨테이너라 `Exited`가 정상입니다.

브라우저에서 아래 주소를 열어 보세요.

| 주소 | 내용 |
| --- | --- |
| http://localhost:8080/api/v1/health | `{"database":"ok","status":"ok",...}`가 나오면 정상 |
| http://localhost:8080/docs/ | Swagger 문서. 모든 API의 요청과 응답 형식 |

## 4. 앱에 연결할 HTTPS 주소 받기

서버를 켜면 Cloudflare 임시 터널이 HTTPS 주소를 하나 만들어 줍니다. 회원가입이나 결제는 필요 없습니다.

macOS:

```bash
docker compose logs tunnel | grep trycloudflare.com
```

Windows PowerShell:

```powershell
docker compose logs tunnel | Select-String trycloudflare.com
```

`https://단어-단어-단어-단어.trycloudflare.com` 같은 주소가 보이면 복사합니다. 아직 안 보이면 10초쯤 뒤에 다시 실행하세요.

브라우저에서 `복사한주소/api/v1/health`를 열어 정상 응답이 오는지 먼저 확인합니다. 주소가 막 만들어진 직후에는 1분 정도 "사이트에 연결할 수 없음"이 뜰 수 있는데, 잠시 기다리면 열립니다.

## 5. 앱 연결

1. 앱의 **설정 > 개발용 서버 연결**에 4번에서 복사한 `https://...trycloudflare.com` 주소를 입력합니다. 끝에 `/api/v1`은 붙여도 되고 안 붙여도 됩니다.
2. 앱이 기기 인증과 설정 조회를 자동으로 확인합니다.
3. 실제 공부 세션을 하나 끝내면 서버로 업로드됩니다. 데모 세션은 업로드되지 않습니다.

업로드가 됐는지는 DB에서 최근 세션을 조회해 확인합니다.

```bash
docker compose exec db psql -U posture -d posture -c "SELECT id, started_at, study_sec, normal_sec, fhp_sec, lean_sec FROM sessions ORDER BY id DESC LIMIT 5"
```

## 6. 자주 쓰는 명령어

모두 `backend` 폴더 안에서 실행합니다.

| 하고 싶은 것 | 명령어 |
| --- | --- |
| 서버 켜기 | `docker compose up -d` |
| 서버 끄기 (데이터는 유지) | `docker compose down` |
| 상태 보기 | `docker compose ps` |
| 서버 로그 보기 | `docker compose logs -f server` (Ctrl+C로 빠져나옴) |
| HTTPS 주소 다시 보기 | 4번 명령어 |
| 백엔드 최신 버전으로 업데이트 | `git pull` 후 `docker compose pull` 후 `docker compose up -d` |
| 저장된 데이터 전부 지우고 새로 시작 | `docker compose down -v` 후 `docker compose up -d` |

## 7. 꼭 알아둘 점

1. **HTTPS 주소는 서버를 켤 때마다 바뀝니다.** `docker compose down` 후 다시 켜거나 컴퓨터를 재시작하면 새 주소를 4번처럼 확인해서 앱에 다시 입력하세요. 앱은 주소가 바뀌면 자동으로 새로 인증합니다.
2. **컴퓨터가 꺼지거나 잠자기 상태면 서버도 멈춥니다.** 테스트하는 동안은 노트북을 켜 두세요.
3. 이 주소는 아는 사람만 접속할 수 있는 임시 공개 주소입니다. 실제 개인정보는 넣지 말고 테스트 용도로만 쓰세요.
4. `docker compose down -v`는 저장된 세션을 모두 지웁니다. 데이터를 유지하려면 `-v` 없이 끄세요.

## 8. 문제 해결

| 증상 | 해결 |
| --- | --- |
| `Cannot connect to the Docker daemon` 또는 `docker: command not found` | Docker Desktop이 실행 중인지 확인 |
| `port is already allocated` (8080) | `.env`의 `HTTP_PORT=8080`을 `8081` 등으로 바꾸고 다시 `docker compose up -d`. 이 경우 로컬 주소도 `localhost:8081`로 바뀜 |
| `docker compose pull`에서 `denied` 또는 `not found` | `docker compose up -d --build`로 직접 빌드 |
| `migrate` 로그에 `set: Illegal option -` (Windows) | 줄바꿈 문제. 폴더를 지우고 저장소를 다시 clone |
| `docker compose ps`에 `server`가 없거나 계속 재시작 | `docker compose logs server`의 마지막 줄을 백엔드 담당자에게 전달 |
| 앱에서 "HTTPS 서버 주소를 입력해 주세요" | `http://localhost`가 아니라 4번의 `https://...trycloudflare.com` 주소를 입력 |
| 앱 업로드가 400으로 실패 | `docker compose logs server`와 응답의 `error.message`를 백엔드 담당자에게 전달. 어떤 필드가 문제인지 메시지에 나옴 |
| HTTPS 주소가 계속 안 열림 | `docker compose restart tunnel` 후 4번으로 새 주소 확인 |

## 참고: 앱과 서버의 약속

1. 기기 인증 요청의 `device_uuid`, 응답의 `access_token` 이름을 서버가 그대로 지원합니다.
2. 서버의 기본 판정값은 앱 초기값과 같습니다 (`version` 1, `t1_ratio` 0.35, `t2_angle` 20, `stable_sec` 2, `analysis_fps` 5).
3. 같은 `client_session_id`로 다시 올리면 200과 기존 `session_id`를 돌려주므로 재전송해도 중복 저장되지 않습니다.
4. `DELETE /users/me`는 204를 돌려주고, 그 사용자의 세션, 구간, 통계, 설정을 모두 지웁니다.
5. 전체 API 형식은 Swagger(http://localhost:8080/docs/)를 기준으로 합니다.
