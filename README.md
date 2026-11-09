# VEIL 필터

<img src="assets/brand/obs-filter.png" width="64" height="64" alt="">

CVA 아래 VEIL은 라이브 화면과 방송 조작을 다듬는 도구 제품군입니다.
VEIL 필터는 그중 얼굴·피부·메이크업과 배경을 조절하는 OBS 제품입니다.
공통 V의 오른쪽 아래 보정 빛 표식이 VEIL 필터를 구분합니다. [브랜드 자산과 적용 경계](assets/brand/README.md)는 설치 파일과 같은 원본을 사용합니다.

OBS에서 얼굴·피부·메이크업을 다듬고, 배경을 깔끔하게 정리하세요.
기존 카메라 소스에 필터를 추가해 바로 조절할 수 있습니다.

**[Windows 설치 파일 받기](https://github.com/ttaem00/CVA-VEIL-OBS-Filter/releases/latest/download/VEIL-OBS-Filter-Setup.exe)** · [최신 버전 안내](https://github.com/ttaem00/CVA-VEIL-OBS-Filter/releases/latest)

현재 버전 **0.4.7** · **Windows 64비트용** · OBS Studio **33.0.1**에서 확인했습니다.

![VEIL 기능 개요: 얼굴 디테일, 피부, 메이크업과 배경 제거를 원하는 만큼 조절합니다. 기능 안내 일러스트입니다.](docs/images/veil-overview.svg)

## 처음 시작하기

1. **OBS를 완전히 종료**하고 `VEIL-OBS-Filter-Setup.exe`를 실행합니다.
2. **VEIL 설치**를 누릅니다. 이미 설치되어 있으면 **VEIL 업데이트**가 표시됩니다.
3. OBS를 다시 열고, 사용할 **카메라 소스 → 필터**를 엽니다.
4. 필터 목록의 **＋**에서 **VEIL 필터 · 얼굴·피부·메이크업**을 추가합니다.

필터를 선택한 뒤 **조절할 항목**에서 피부, 얼굴 디테일, 메이크업, 빛·색감을 고르세요.
슬라이더를 움직이면 영상에 바로 반영됩니다. 얼굴 모양은 작은 값부터 조절하는 것을 권합니다.

업데이트할 때도 OBS를 종료한 뒤 같은 설치 파일을 실행하세요.

## 배경 제거하기

![일반 배경은 VEIL 배경 · 인물, 초록 천은 크로마키, 윤곽 부드러움은 가장자리를 선택합니다. 기능 안내 일러스트입니다.](docs/images/veil-background.svg)

VEIL 필터의 **조절할 항목 → 배경 제거 → 배경 필터 추가**를 누르세요.
왼쪽 필터 목록에 추가된 항목에서 필요한 기능을 선택합니다.

| 하고 싶은 일 | 선택할 필터 | 조절할 내용 |
| --- | --- | --- |
| 일반 배경에서 사람만 남기기 | **VEIL 배경 · 인물** | 자동 배경 제거, 윤곽의 떨림 줄이기 |
| 머리카락·어깨 윤곽을 부드럽게 다듬기 | **VEIL 배경 · 가장자리** | 윤곽 부드러움 |
| 초록 천 등 단색 배경 지우기 | **VEIL 배경 · 크로마키** | 지울 색, 허용 범위, 경계와 색 번짐 |

배경에 맞게 **인물** 또는 **크로마키** 중 하나를 켜고, 필요하면 **가장자리**를 함께 사용하세요.
각 필터 왼쪽의 **눈 아이콘**으로 효과를 켜거나 끌 수 있습니다.

새 배경으로 쓸 사진·영상은 OBS 소스 목록에서 **카메라 아래**에 놓으세요.
빈 배경을 미리 촬영하거나 등록할 필요는 없습니다.

## 내게 맞게 조절하기

- **얼굴·피부** — 얼굴 길이, 눈 사이, 코 길이, 피부 매끄러움과 질감 등을 조절합니다. 얼굴 길이·눈 사이·코 길이는 **0이 원래 모습**입니다.
- **메이크업·색감** — 입술과 볼 색, 광택, 밝기와 색감을 조절합니다. **전체 보정 강도**로 얼굴·색감 보정의 강도를 한 번에 바꿀 수 있습니다.
- **설정 보관** — **프리셋 관리**에서 설정을 파일로 저장하고 다시 불러올 수 있습니다. OBS도 장면에 현재 필터 값을 저장합니다.

**원본 확인·좌우 비교·마스크 확인 화면은 방송과 녹화에도 표시됩니다.**
비교를 마치면 **보정 영상으로 돌아가기**를 누르세요. 다른 설정 항목을 열어도 이 버튼이 보입니다.

## 도움이 필요할 때

- **필터가 목록에 없어요** — 설치가 끝난 뒤 OBS를 완전히 종료했다가 다시 열어 주세요. 이 배포는 Windows 64비트와 OBS Studio 33.0.1을 기준으로 확인했습니다.
- **배경 제거가 멈추거나 인물이 사라져요** — **VEIL 배경 · 인물 → 다시 시작**을 누르세요. 현재 상태는 **상태 새로고침**에서 확인할 수 있습니다.
- **이전 설치로 돌아가고 싶어요** — OBS를 종료하고 설치 파일의 **설치 되돌리기**를 누르세요. 업데이트 전 설치로 복원하며, 처음 설치한 경우에는 VEIL을 제거합니다.

해결되지 않으면 [문제 제보](https://github.com/ttaem00/CVA-VEIL-OBS-Filter/issues)에 OBS 버전, 카메라 모델, 문제가 생긴 순서를 알려 주세요.
더 자세한 조작은 [빠른 사용법](사용법.txt)을 참고하세요.

## 사용한 프로젝트와 라이선스

VEIL은 여러 오픈소스 프로젝트의 작업을 바탕으로 만듭니다.

- [Face Beautifier](https://github.com/ctbot000/face-beautifier) — 얼굴·피부 보정 수학과 처리 방식
- [Lumina Beauty](https://github.com/dinjufen/lumina-beauty) — 얼굴 추적
- [RobustVideoMatting](https://github.com/PeterL1n/RobustVideoMatting) — 자동 인물 배경 제거
- [Advanced Masks](https://github.com/FiniteSingularity/obs-advanced-masks) — 가장자리·크로마키 처리

전체 사용·참고 프로젝트와 적용 범위는 [CREDITS.md](CREDITS.md), 원문 고지는 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)에 정리했습니다.

라이선스는 구성요소별로 적용됩니다. VEIL 얼굴 보정 통합은 GPL-2.0-or-later,
VEIL 인물 처리 통합은 GPL-3.0-or-later이며, 외부 코드·모델은 각각의 원래 조건을 유지합니다.
**Microsoft DirectML에는 Microsoft의 별도 사용·재배포 조건이 적용됩니다.**
자세한 범위는 [LICENSE](LICENSE)와 [라이선스 안내](docs/LICENSING.md)를 확인하세요.

## 개발·소스 안내

[직접 빌드하기](docs/BUILD.md) · [원본 소스·버전·빌드 의존성](docs/DEPENDENCY-SOURCES.md)

현재 배포는 자동 인물 배경 제거를 사용합니다. 배경 사진을 등록하는 이전 기능은
[보관 기능 안내](features/registered-background/README.md)와 별도 보관 브랜치에 남겨 두었습니다.
